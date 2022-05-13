/*
 * Copyright (c) 2016 Mellanox Technologies. All rights reserved.
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

#include <linux/slab.h>
#include "mlx5_ib.h"
#include <rdma/ib_cmem.h>

struct ib_mr *mlx5_ib_exp_alloc_mr(struct ib_pd *pd, struct ib_mr_init_attr *attr)
{
	struct ib_dm_mr_attr dm_mr_attr = {0};

	if ((attr->mr_type == IB_MR_TYPE_DM) && attr->dm) {
		dm_mr_attr.length = attr->length;
		dm_mr_attr.offset = attr->start;
		dm_mr_attr.access_flags = attr->access_flags;

		return mlx5_ib_reg_dm_mr(pd, attr->dm, &dm_mr_attr, NULL);
	} else {
		return mlx5_ib_alloc_mr(pd, attr->mr_type, attr->max_num_sg);
	}
}

static int get_arg(unsigned long offset)
{
	return offset & ((1 << MLX5_IB_MMAP_CMD_SHIFT) - 1);
}

int get_pg_order(unsigned long offset)
{
	return get_arg(offset);
}

int mlx5_ib_exp_contig_mmap(struct ib_ucontext *ibcontext,
			    struct vm_area_struct *vma,
			    unsigned long  command)
{
	struct mlx5_ib_dev *dev = to_mdev(ibcontext->device);
	struct ib_cmem *ib_cmem;
	unsigned long total_size;
	unsigned long order;
	int err;
	int numa_node;

	if (command == MLX5_IB_EXP_MMAP_GET_CONTIGUOUS_PAGES_CPU_NUMA)
		numa_node = numa_node_id();
	else if (command == MLX5_IB_EXP_MMAP_GET_CONTIGUOUS_PAGES_DEV_NUMA)
		numa_node = dev_to_node(&dev->mdev->pdev->dev);
	else
		numa_node = -1;

	total_size = vma->vm_end - vma->vm_start;
	order = get_pg_order(vma->vm_pgoff);

	ib_cmem = ib_cmem_alloc_contiguous_pages(ibcontext, total_size,
						 order, numa_node);
	if (IS_ERR(ib_cmem))
		return PTR_ERR(ib_cmem);

	err = ib_cmem_map_contiguous_pages_to_vma(ib_cmem, vma);
	if (err) {
		ib_cmem_release_contiguous_pages(ib_cmem);
		return err;
	}

	return 0;
}

struct ib_mr *mlx5_ib_phys_addr(struct ib_pd *pd, u64 length, u64 start_addr,
				int access_flags)
{
#ifdef CONFIG_INFINIBAND_PA_MR
	return mlx5_ib_get_dma_mr_ex(pd, access_flags, start_addr, length);
#else
	pr_debug("Physical Address MR support wasn't compiled in"
		 "the RDMA subsystem. Recompile with Physical"
		 "Address MR\n");
	return ERR_PTR(-EOPNOTSUPP);
#endif /* CONFIG_INFINIBAND_PA_MR */
}

int mlx5_ib_exp_query_mkey(struct ib_mr *mr, u64 mkey_attr_mask,
			   struct ib_mkey_attr *mkey_attr)
{
	struct mlx5_ib_mr *mmr = to_mmr(mr);

	mkey_attr->max_reg_descriptors = mmr->max_descs;

	return 0;
}
