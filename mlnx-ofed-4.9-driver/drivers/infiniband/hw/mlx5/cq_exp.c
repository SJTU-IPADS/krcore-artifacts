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

#include <linux/mlx5/cq.h>
#include "mlx5_ib.h"

int mlx5_ib_exp_modify_cq(struct ib_cq *cq, struct ib_cq_attr *cq_attr,
			  int cq_attr_mask)
{
	struct mlx5_ib_dev *dev = to_mdev(cq->device);
	int inlen = MLX5_ST_SZ_BYTES(modify_cq_in);
	struct mlx5_ib_cq *mcq = to_mcq(cq);
	u32 fsel = 0;
	void *cqc;
	int err;
	u32 *in;

	in = kzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	cqc = MLX5_ADDR_OF(modify_cq_in, in, cq_context);
	MLX5_SET(modify_cq_in, in, cqn, mcq->mcq.cqn);
	if (cq_attr_mask & IB_CQ_MODERATE) {
		if (MLX5_CAP_GEN(dev->mdev, cq_moderation)) {
			fsel |= (MLX5_CQ_MODIFY_PERIOD | MLX5_CQ_MODIFY_COUNT);
			if (cq_attr->moderation.cq_period & 0xf000) {
				/* A value higher than 0xfff is required, better
				 * use the largest value possible. */
				cq_attr->moderation.cq_period = 0xfff;
				pr_info("period supported is limited to 12 bits\n");
			}

			MLX5_SET(cqc, cqc, cq_period, cq_attr->moderation.cq_period);
			MLX5_SET(cqc, cqc, cq_max_count, cq_attr->moderation.cq_count);
		} else {
			err = -ENOSYS;
			goto out;
		}
	}

	if (cq_attr_mask & IB_CQ_CAP_FLAGS) {
		if (MLX5_CAP_GEN(dev->mdev, cq_oi)) {
			fsel |= MLX5_CQ_MODIFY_OVERRUN;
			if (cq_attr->cq_cap_flags & IB_CQ_IGNORE_OVERRUN)
				MLX5_SET(cqc, cqc, oi, 1);
			else
				MLX5_SET(cqc, cqc, oi, 0);
		} else {
			err = -ENOSYS;
			goto out;
		}
	}

	MLX5_SET(modify_cq_in, in,
		 modify_field_select_resize_field_select.modify_field_select.modify_field_select,
		 fsel);

	err = mlx5_core_modify_cq(dev->mdev, &mcq->mcq, in, inlen);
out:
	kfree(in);
	if (err)
		mlx5_ib_warn(dev, "modify cq 0x%x failed\n", mcq->mcq.cqn);

	return err;
}
