#include <iostream>
#include <vector>
#include <atomic>

#include <ghex/transport_layer/ucx/threads.hpp>
#include <ghex/common/timer.hpp>
#include "utils.hpp"

namespace ghex = gridtools::ghex;

#ifdef USE_MPI
    /* MPI backend */
    #ifdef USE_OPENMP
        #include <ghex/threads/atomic/primitives.hpp>
        using threading    = ghex::threads::atomic::primitives;
    #else
        #include <ghex/threads/none/primitives.hpp>
        using threading    = ghex::threads::none::primitives;
    #endif
    #include <ghex/transport_layer/mpi/context.hpp>
    using transport    = ghex::tl::mpi_tag;
#else
    /* UCX backend */
    #ifdef USE_OPENMP
        #include <ghex/threads/omp/primitives.hpp>
        using threading    = ghex::threads::omp::primitives;
    #else
        #include <ghex/threads/none/primitives.hpp>
        using threading    = ghex::threads::none::primitives;
    #endif
    #include <ghex/transport_layer/ucx3/address_db_mpi.hpp>
    #include <ghex/transport_layer/ucx/context.hpp>
    using db_type      = ghex::tl::ucx::address_db_mpi;
    using transport    = ghex::tl::ucx_tag;
#endif /* USE_MPI */
    
#include <ghex/transport_layer/message_buffer.hpp>
using context_type = ghex::tl::context<transport, threading>;
using MsgType = gridtools::ghex::tl::message_buffer<>;
using communicator_type = typename context_type::communicator_type;
using future_type = typename communicator_type::future<void>;


std::atomic<int> sent(0);
std::atomic<int> received(0);
std::atomic<int> tail_send(0);
std::atomic<int> tail_recv(0);
int last_received = 0;
int last_sent = 0;

int main(int argc, char *argv[])
{
    int niter, buff_size;
    int inflight;
    int mode;
    gridtools::ghex::timer timer, ttimer;
    
    if(argc != 4){
	std::cerr << "Usage: bench [niter] [msg_size] [inflight]" << "\n";
	std::terminate();
    }
    niter = atoi(argv[1]);
    buff_size = atoi(argv[2]);
    inflight = atoi(argv[3]);   

    int num_threads = 1;
#ifdef USE_OPENMP
    MPI_Init_thread(NULL, NULL, MPI_THREAD_MULTIPLE, &mode);
    if(mode != MPI_THREAD_MULTIPLE){
	std::cerr << "MPI_THREAD_MULTIPLE not supported by MPI, aborting\n";
	std::terminate();
    }
    #pragma omp parallel
    {
        #pragma omp master
        num_threads = omp_get_num_threads();
    }
#else
    MPI_Init_thread(NULL, NULL, MPI_THREAD_SINGLE, &mode);
#endif

    {
        #ifdef USE_MPI
        context_type contex{num_threads, MPI_COMM_WORLD};
        #else
        context_type context{num_threads, MPI_COMM_WORLD, db_type{MPI_COMM_WORLD} };
        #endif

        
        THREAD_PARALLEL_BEG() {

            auto token = context.get_token();

            auto comm = context.get_communicator(token);

            const auto rank = comm.rank();
            const auto size = comm.size();
            const auto thread_id = token.id();
            const auto num_threads = context.thread_primitives().size();
            const auto peer_rank = (rank+1)%2;

            context.thread_primitives().master(token, 
                [rank,&comm]()
                {
                    if(rank==0)
                    std::cout << "\n\nrunning test " << __FILE__ << " with communicator " << typeid(comm).name() << "\n\n";
                });

            std::vector<MsgType> smsgs(inflight);
            std::vector<MsgType> rmsgs(inflight);
            std::vector<future_type> sreqs(inflight);
            std::vector<future_type> rreqs(inflight);
            for(int j=0; j<inflight; j++)
            {
                smsgs[j].resize(buff_size);
                rmsgs[j].resize(buff_size);
                make_zero(smsgs[j]);
                make_zero(rmsgs[j]);
            }

            context.barrier(token);

            context.thread_primitives().master(token, 
                [&timer,&ttimer,rank,num_threads]() 
                { 
                    timer.tic();
                    ttimer.tic();
                    if(rank == 1) 
                        std::cout << "number of threads: " << num_threads << ", multi-threaded: " << THREAD_IS_MT << "\n";
                });
            
            context.thread_primitives().barrier(token);

            int dbg = 0, sdbg = 0, rdbg = 0;
            while(sent < niter || received < niter)
            {
                /* submit comm */
                for(int j=0; j<inflight; j++)
                {
                    if(rank==0 && thread_id==0 && sdbg>=(niter/10)) {
                        std::cout << sent << " sent\n";
                        sdbg = 0;
                    }
                    
                    if(rank==0 && thread_id==0 && rdbg>=(niter/10)) {
                        std::cout << received << " received\n";
                        rdbg = 0;
                    }
                    
                    if(thread_id == 0 && dbg >= (niter/10)) {
                        dbg = 0;
                        std::cout << rank << " total bwdt MB/s:      " 
                                  << ((double)(received-last_received + sent-last_sent)*size*buff_size/2)/timer.toc()
                                  << "\n";
                        timer.tic();
                        last_received = received;
                        last_sent = sent;
                    }
                    
                    if(rreqs[j].test()) {
                        received++;
                        rdbg+=num_threads;
                        dbg+=num_threads;
                        rreqs[j] = comm.recv(rmsgs[j], peer_rank, thread_id*inflight + j);
                    }
                    
                    if(sent < niter && sreqs[j].test()) {
                        sent++;
                        sdbg+=num_threads;
                        dbg+=num_threads;
                        sreqs[j] = comm.send(smsgs[j], peer_rank, thread_id*inflight + j);
                    }
                }
            }
            
            context.barrier(token);
            context.thread_primitives().master(token, 
                [&niter,size,buff_size,&ttimer,rank,num_threads]() 
                { 
                    if (rank==1)
                    {
                        const auto t = ttimer.toc();
                        std::cout << "time:       " << t/1000000 << "s\n";
                        std::cout << "final MB/s: " << ((double)niter*size*buff_size)/t << "\n";
                    }       
                });
            
            context.thread_primitives().barrier(token);

            // tail loops - submit RECV requests until
            // all SEND requests have been finalized.
            // This is because UCX cannot cancel SEND requests.
            // https://github.com/openucx/ucx/issues/1162
            // 
            {
                int incomplete_sends = 0;
                int send_complete = 0;

                // complete all posted sends
                do {
                    comm.progress();
                    // check if we have completed all our posted sends
                    if(!send_complete){
                        incomplete_sends = 0;
                        for(int j=0; j<inflight; j++){
                            if(!sreqs[j].test()) incomplete_sends++;
                        }
                        if(incomplete_sends == 0) {
                            // increase thread counter of threads that are done with the sends
                            tail_send++;
                            send_complete = 1;
                        }
                    }
                    // continue to re-schedule all recvs to allow the peer to complete
                    for(int j=0; j<inflight; j++){
                        if(rreqs[j].test()) {
                            rreqs[j] = comm.recv(rmsgs[j], peer_rank, thread_id*inflight + j);
                        }
                    }
                } while(tail_send!=num_threads);

                // We have all completed the sends, but the peer might not have yet.
                // Notify the peer and keep submitting recvs until we get his notification.
                future_type sf, rf;
                MsgType smsg(1), rmsg(1);
                context.thread_primitives().master(token, 
                    [&comm,&sf,&rf,&smsg,&rmsg,peer_rank]()
                    {
                        sf = comm.send(smsg, peer_rank, 0x800000);
                        rf = comm.recv(rmsg, peer_rank, 0x800000);
                    });

                while(!tail_recv.load()){
                    comm.progress();

                    // schedule all recvs to allow the peer to complete
                    for(int j=0; j<inflight; j++){
                        if(rreqs[j].test()) {
                            rreqs[j] = comm.recv(rmsgs[j], peer_rank, thread_id*inflight + j);
                        }
                    }
                    context.thread_primitives().master(token, 
                        [&rf,&tail_recv]()
                        {
                            if(rf.test()) tail_recv = 1;
                        });
                }
            }
            // peer has sent everything, so we can cancel all posted recv requests
            for(int j=0; j<inflight; j++){
                rreqs[j].cancel();
            }

        } THREAD_PARALLEL_END();
    }
    MPI_Barrier(MPI_COMM_WORLD);
    MPI_Finalize();
}