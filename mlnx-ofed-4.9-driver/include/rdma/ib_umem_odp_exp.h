/*
 * Copyright (c) 2014 Mellanox Technologies. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef IB_UMEM_ODP_EXP_H
#define IB_UMEM_ODP_EXP_H

#ifdef CONFIG_INFINIBAND_ON_DEMAND_PAGING

enum ib_odp_dma_map_flags {
	IB_ODP_DMA_MAP_FOR_PREFETCH	= 1 << 0,
};

int ib_umem_odp_add_statistic_nodes(struct device *dev);

static inline void ib_umem_odp_account_fault_handled(struct ib_device *dev)
{
	atomic_inc(&dev->odp_statistics.num_page_faults);
}

static inline void ib_umem_odp_account_prefetch_handled(struct ib_device *dev)
{
	atomic_inc(&dev->odp_statistics.num_prefetches_handled);
}

static inline void ib_umem_odp_account_invalidations_fault_contentions(struct ib_device *dev)
{
	atomic_inc(&dev->odp_statistics.invalidations_faults_contentions);
}

#else

static inline int ib_umem_odp_add_statistic_nodes(struct device *dev)
{
	return 0;
}
#endif /* CONFIG_INFINIBAND_ON_DEMAND_PAGING */

#endif /* IB_UMEM_ODP_EXP_H */
