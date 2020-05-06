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
#ifndef INCLUDED_GHEX_TL_PMI_HPP
#define INCLUDED_GHEX_TL_PMI_HPP

namespace gridtools
{
    namespace ghex
    {
	namespace tl
	{
            namespace pmi
            {
                template<typename PMIImpl>
                class pmi;
		
                struct pmix_tag {};
            } // namespace pmi
	} // namespace tl
    } // namespace ghex
} // namespace gridtools

#endif /* INCLUDED_GHEX_TL_PMI_HPP */
