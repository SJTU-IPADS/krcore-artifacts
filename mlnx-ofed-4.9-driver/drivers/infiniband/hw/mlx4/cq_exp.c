/*
 * Copyright (c) 2007 Cisco Systems, Inc. All rights reserved.
 * Copyright (c) 2007, 2008 Mellanox Technologies. All rights reserved.
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

#include <linux/mlx4/cq.h>
#include <linux/mlx4/qp.h>
#include <linux/mlx4/srq.h>
#include <linux/slab.h>
#include <linux/mlx4/cq_exp.h>

#include <rdma/mlx4-abi.h>
#include "mlx4_ib.h"

int mlx4_ib_exp_modify_cq(struct ib_cq *cq, struct ib_cq_attr *cq_attr, int cq_attr_mask)
{
	struct mlx4_ib_cq *mcq = to_mcq(cq);
	struct mlx4_ib_dev *dev = to_mdev(cq->device);
	int err = 0;

	if (cq_attr_mask & IB_CQ_CAP_FLAGS) {
		if (cq_attr->cq_cap_flags & IB_CQ_IGNORE_OVERRUN) {
			if (dev->dev->caps.cq_flags & MLX4_DEV_CAP_CQ_FLAG_IO)
				err = mlx4_cq_ignore_overrun(dev->dev, &mcq->mcq);
			else
				err = -ENOSYS;
		}
	}

	if (!err)
		if (cq_attr_mask & IB_CQ_MODERATE)
			err = mlx4_cq_modify(dev->dev, &mcq->mcq,
					     cq_attr->moderation.cq_count,
					     cq_attr->moderation.cq_period);
	return err;
}
