/*
 * GridTools
 *
 * Copyright (c) 2014-2020, ETH Zurich
 * All rights reserved.
 *
 * Please, refer to the LICENSE file in the root directory.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

#include <vector>
#include <memory>
#include <mpi.h>
#include "../include/ghex/transport_layer/mpi/context.hpp"

namespace gridtools {
    namespace ghex {
        namespace tl {

            namespace {
                std::vector<std::unique_ptr<context<mpi_tag> > > context_repository;
                MPI_Comm clone_mpi_comm(MPI_Comm mpi_comm) {
                    // clone the communicator first to be independent of user calls to the mpi runtime
                    MPI_Comm new_comm;
                    MPI_Comm_dup(mpi_comm, &new_comm);
                    return new_comm;
                }
            }


            context<mpi_tag>& create_context(MPI_Comm mpi_comm)
            {
                auto new_comm = clone_mpi_comm(mpi_comm);
                context_repository.push_back(std::unique_ptr<context<mpi_tag>>{new context<mpi_tag>{new_comm, new_comm}});
                return *context_repository[context_repository.size()-1];
            }

            void destroy_contexts() {
                context_repository.clear();
            }
            // struct context_factory<mpi_tag>
            // {
            //     static std::unique_ptr<context<mpi_tag>> create(MPI_Comm mpi_comm)
            //     {
            //         auto new_comm = detail::clone_mpi_comm(mpi_comm);
            //         return std::unique_ptr<context<mpi_tag>>{
            //             new context<mpi_tag>{new_comm, new_comm}};
            //     }
            // };
        }
    }
}
