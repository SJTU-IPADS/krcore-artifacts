/*
 * Copyright (c) 2015 Mellanox Technologies. All rights reserved.
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

#include "mlx4_ib.h"
#include <linux/mlx4/qp.h>

static inline int is_little_endian(void)
{
#if defined(__LITTLE_ENDIAN)
	return 1;
#elif defined(__BIG_ENDIAN)
	return 0;
#else
#error Host endianness not defined
#endif
}

void mlx4_ib_set_exp_attr_flags(struct mlx4_ib_qp *qp, struct ib_qp_init_attr *init_attr)
{
	if (qp->flags & MLX4_IB_QP_CROSS_CHANNEL)
		init_attr->create_flags |= IB_QP_CREATE_CROSS_CHANNEL;

	if (qp->flags & MLX4_IB_QP_MANAGED_SEND)
		init_attr->create_flags |= IB_QP_CREATE_MANAGED_SEND;

	if (qp->flags & MLX4_IB_QP_MANAGED_RECV)
		init_attr->create_flags |= IB_QP_CREATE_MANAGED_RECV;
}

void mlx4_ib_set_exp_qp_flags(struct mlx4_ib_qp *qp, struct ib_qp_init_attr *init_attr)
{
	if (init_attr->create_flags & IB_QP_CREATE_CROSS_CHANNEL)
		qp->flags |= MLX4_IB_QP_CROSS_CHANNEL;

	if (init_attr->create_flags & IB_QP_CREATE_MANAGED_SEND)
		qp->flags |= MLX4_IB_QP_MANAGED_SEND;

	if (init_attr->create_flags & IB_QP_CREATE_MANAGED_RECV)
		qp->flags |= MLX4_IB_QP_MANAGED_RECV;
}

struct ib_qp *mlx4_ib_create_qp_wrp(struct ib_pd *pd,
				    struct ib_qp_init_attr *init_attr,
				    struct ib_udata *udata)
{
	return mlx4_ib_create_qp(pd, init_attr, udata, 0);
}

struct ib_qp *mlx4_ib_exp_create_qp(struct ib_pd *pd,
				    struct ib_exp_qp_init_attr *init_attr,
				    struct ib_udata *udata)
{
	struct ib_qp *qp;
	struct ib_device *device;
	int use_inlr;
	int rwqe_size;
	struct mlx4_ib_dev *dev;
	struct mlx4_ib_qp *mqp;

	if ((init_attr->create_flags & IB_QP_EXP_CREATE_ATOMIC_BE_REPLY) &&
	     is_little_endian())
		return ERR_PTR(-EINVAL);


	if (init_attr->max_inl_recv && !udata)
		return ERR_PTR(-EINVAL);

	use_inlr = qp_has_rq((struct ib_qp_init_attr *)init_attr) &&
		   init_attr->max_inl_recv && pd;

	if (use_inlr) {
		rwqe_size = roundup_pow_of_two(max(1U, init_attr->cap.max_recv_sge)) *
					       sizeof(struct mlx4_wqe_data_seg);
		if (rwqe_size < init_attr->max_inl_recv) {
			dev = to_mdev(pd->device);
			init_attr->max_inl_recv = min(init_attr->max_inl_recv,
						      (u32)(dev->dev->caps.max_rq_sg *
						      sizeof(struct mlx4_wqe_data_seg)));
			init_attr->cap.max_recv_sge = roundup_pow_of_two(init_attr->max_inl_recv) /
						      sizeof(struct mlx4_wqe_data_seg);
		}
	} else {
		init_attr->max_inl_recv = 0;
	}

	device = pd ? pd->device : init_attr->xrcd->device;
	if ((init_attr->create_flags &
		(MLX4_IB_QP_CROSS_CHANNEL |
		 MLX4_IB_QP_MANAGED_SEND |
		 MLX4_IB_QP_MANAGED_RECV)) &&
	     !(to_mdev(device)->dev->caps.flags &
		MLX4_DEV_CAP_FLAG_CROSS_CHANNEL)) {
		pr_debug("%s Does not support cross-channel operations\n",
			 to_mdev(device)->ib_dev.name);
		return ERR_PTR(-EINVAL);
	}

	qp = mlx4_ib_create_qp(pd, (struct ib_qp_init_attr *)init_attr, udata, 1);

	if (IS_ERR(qp))
		return qp;

	if (use_inlr) {
		mqp = to_mqp(qp);
		mqp->max_inlr_data = 1 << mqp->rq.wqe_shift;
		init_attr->max_inl_recv = mqp->max_inlr_data;
	}

	return qp;
}

static int init_qpg_parent(struct mlx4_ib_dev *dev, struct mlx4_ib_qp *pqp,
			   struct ib_exp_qp_init_attr *attr, int *qpn)
{
	struct mlx4_ib_qpg_data *qpg_data;
	int tss_num, rss_num;
	int tss_align_num, rss_align_num;
	int tss_base, rss_base;
	int err;

	/* Parent is part of the TSS range (in SW TSS ARP is sent via parent) */
	tss_num = 1 + attr->parent_attrib.tss_child_count;
	tss_align_num = roundup_pow_of_two(tss_num);
	rss_num = attr->parent_attrib.rss_child_count;
	rss_align_num = roundup_pow_of_two(rss_num);

	if (rss_num > 1) {
		/* RSS is requested */
		if (!(dev->dev->caps.flags2 & MLX4_DEV_CAP_FLAG2_RSS))
			return -ENOSYS;
		if (rss_align_num > dev->dev->caps.max_rss_tbl_sz)
			return -EINVAL;
		/* We must work with power of two */
		attr->parent_attrib.rss_child_count = rss_align_num;
	}

	qpg_data = kzalloc(sizeof(*qpg_data), GFP_KERNEL);
	if (!qpg_data)
		return -ENOMEM;

	if (pqp->flags & MLX4_IB_QP_NETIF)
		err = mlx4_ib_steer_qp_alloc(dev, tss_align_num, &tss_base);
	else
		err = mlx4_qp_reserve_range(
			dev->dev, tss_align_num,
			tss_align_num, &tss_base, MLX4_RESERVE_ETH_BF_QP |
			((attr->qp_type == IB_QPT_RAW_PACKET) ?
			 MLX4_RESERVE_A0_QP : 0),
			MLX4_RES_USAGE_USER_VERBS);
	if (err)
		goto err1;

	if (tss_num > 1) {
		u32 alloc = BITS_TO_LONGS(tss_align_num)  * sizeof(long);

		qpg_data->tss_bitmap = kzalloc(alloc, GFP_KERNEL);
		if (!qpg_data->tss_bitmap) {
			err = -ENOMEM;
			goto err2;
		}
		bitmap_fill(qpg_data->tss_bitmap, tss_num);
		/* Note parent takes first index */
		clear_bit(0, qpg_data->tss_bitmap);
	}

	if (rss_num > 1) {
		u32 alloc = BITS_TO_LONGS(rss_align_num) * sizeof(long);

		err = mlx4_qp_reserve_range(dev->dev, rss_align_num,
					    1, &rss_base, 0,
					    MLX4_RES_USAGE_USER_VERBS);
		if (err)
			goto err3;
		qpg_data->rss_bitmap = kzalloc(alloc, GFP_KERNEL);
		if (!qpg_data->rss_bitmap) {
			err = -ENOMEM;
			goto err4;
		}
		bitmap_fill(qpg_data->rss_bitmap, rss_align_num);
	}

	qpg_data->tss_child_count = attr->parent_attrib.tss_child_count;
	qpg_data->rss_child_count = attr->parent_attrib.rss_child_count;
	qpg_data->qpg_parent = pqp;
	qpg_data->qpg_tss_mask_sz = ilog2(tss_align_num);
	qpg_data->tss_qpn_base = tss_base;
	qpg_data->rss_qpn_base = rss_base;
	qpg_data->dev = dev;
	if (pqp->flags & MLX4_IB_QP_NETIF)
		qpg_data->flags |= MLX4_IB_QP_NETIF;

	pqp->qpg_data = qpg_data;
	kref_init(&pqp->qpg_data->refcount);
	*qpn = tss_base;

	return 0;

err4:
	mlx4_qp_release_range(dev->dev, rss_base, rss_align_num);

err3:
	if (tss_num > 1)
		kfree(qpg_data->tss_bitmap);

err2:
	if (pqp->flags & MLX4_IB_QP_NETIF)
		mlx4_ib_steer_qp_free(dev, tss_base, tss_align_num);
	else
		mlx4_qp_release_range(dev->dev, tss_base, tss_align_num);

err1:
	kfree(qpg_data);
	return err;
}

static void qpg_release(struct kref *ref)
{
	struct mlx4_ib_qpg_data *qpg_data;
	int align_num;

	qpg_data = container_of(ref, struct mlx4_ib_qpg_data, refcount);
	if (qpg_data->tss_child_count > 1)
		kfree(qpg_data->tss_bitmap);

	align_num = roundup_pow_of_two(1 + qpg_data->tss_child_count);
	if (qpg_data->flags & MLX4_IB_QP_NETIF)
		mlx4_ib_steer_qp_free(qpg_data->dev, qpg_data->tss_qpn_base, align_num);
	else
		mlx4_qp_release_range(qpg_data->dev->dev, qpg_data->tss_qpn_base, align_num);

	if (qpg_data->rss_child_count > 1) {
		kfree(qpg_data->rss_bitmap);
		align_num = roundup_pow_of_two(qpg_data->rss_child_count);
		mlx4_qp_release_range(qpg_data->dev->dev, qpg_data->rss_qpn_base,
				      align_num);
	}

	kfree(qpg_data);
}

static void free_qpg_parent(struct mlx4_ib_dev *dev, struct mlx4_ib_qp *pqp)
{
	kref_put(&pqp->qpg_data->refcount, qpg_release);
}

static int alloc_qpg_qpn(struct ib_exp_qp_init_attr *init_attr,
			 struct mlx4_ib_qp *pqp, int *qpn)
{
	struct mlx4_ib_qp *mqp = to_mqp(init_attr->qpg_parent);
	struct mlx4_ib_qpg_data *qpg_data = mqp->qpg_data;
	u32 idx, old;

	switch (init_attr->qpg_type) {
	case IB_QPG_CHILD_TX:
		if (qpg_data->tss_child_count == 0)
			return -EINVAL;
		do {
			/* Parent took index 0 */
			idx = find_first_bit(qpg_data->tss_bitmap,
					     qpg_data->tss_child_count + 1);
			if (idx >= qpg_data->tss_child_count + 1)
				return -ENOMEM;
			old = test_and_clear_bit(idx, qpg_data->tss_bitmap);
		} while (old == 0);
		idx += qpg_data->tss_qpn_base;
		break;
	case IB_QPG_CHILD_RX:
		if (qpg_data->rss_child_count == 0)
			return -EINVAL;
		do {
			idx = find_first_bit(qpg_data->rss_bitmap,
					     qpg_data->rss_child_count);
			if (idx >= qpg_data->rss_child_count)
				return -ENOMEM;
			old = test_and_clear_bit(idx, qpg_data->rss_bitmap);
		} while (old == 0);
		idx += qpg_data->rss_qpn_base;
		break;
	default:
		return -EINVAL;
	}

	pqp->qpg_data = qpg_data;
	kref_get(&qpg_data->refcount);
	*qpn = idx;

	return 0;
}

static void free_qpg_qpn(struct mlx4_ib_qp *mqp, int qpn)
{
	struct mlx4_ib_qpg_data *qpg_data = mqp->qpg_data;

	switch (mqp->qpg_type) {
	case IB_QPG_CHILD_TX:
		/* Do range check */
		qpn -= qpg_data->tss_qpn_base;
		set_bit(qpn, qpg_data->tss_bitmap);
		kref_put(&qpg_data->refcount, qpg_release);
		break;
	case IB_QPG_CHILD_RX:
		qpn -= qpg_data->rss_qpn_base;
		set_bit(qpn, qpg_data->rss_bitmap);
		kref_put(&qpg_data->refcount, qpg_release);
		break;
	default:
		/* error */
		pr_warn("wrong qpg type (%d)\n", mqp->qpg_type);
		break;
	}
}

int mlx4_ib_alloc_qpn_common(struct mlx4_ib_dev *dev, struct mlx4_ib_qp *qp,
			     struct ib_qp_init_attr *attr, int *qpn, int is_exp)
{
	int err = 0;
	struct ib_exp_qp_init_attr *exp_attr = NULL;
	enum ib_qpg_type        qpg_type;

	if (is_exp) {
		exp_attr = (struct ib_exp_qp_init_attr *)attr;
		qpg_type = exp_attr->qpg_type;
	} else {
		qpg_type = IB_QPG_NONE;
	}

	switch (qpg_type) {
	case IB_QPG_NONE:
		/* Raw packet QPNs may not have bits 6,7 set in their qp_num;
		 * otherwise, the WQE BlueFlame setup flow wrongly causes
		 * VLAN insertion.
		 */
		if (attr->qp_type == IB_QPT_RAW_PACKET) {
			err = mlx4_qp_reserve_range(dev->dev, 1, 1, qpn,
						    (attr->cap.max_send_wr ?
						     MLX4_RESERVE_ETH_BF_QP : 0) |
						    (attr->cap.max_recv_wr ?
						     MLX4_RESERVE_A0_QP : 0),
						    qp->mqp.usage);
		} else {
			if (qp->flags & MLX4_IB_QP_NETIF)
				err = mlx4_ib_steer_qp_alloc(dev, 1, qpn);
			else
				err = mlx4_qp_reserve_range(dev->dev, 1, 1,
							    qpn, 0, qp->mqp.usage);
		}
		break;
	case IB_QPG_PARENT:
		err = init_qpg_parent(dev, qp, exp_attr, qpn);
		break;
	case IB_QPG_CHILD_TX:
	case IB_QPG_CHILD_RX:
		err = alloc_qpg_qpn(exp_attr, qp, qpn);
		break;
	default:
		err = -EINVAL;
		break;
	}
	if (err)
		return err;
	qp->qpg_type = qpg_type;
	return 0;
}

static void free_qpn_common(struct mlx4_ib_dev *dev, struct mlx4_ib_qp *qp, int qpn,
			    struct mlx4_ib_ucontext *context,
			    enum mlx4_ib_source_type src,
			    bool dirty_release)
{
	switch (qp->qpg_type) {
	case IB_QPG_NONE:
		if (qp->flags & MLX4_IB_QP_NETIF)
			mlx4_ib_steer_qp_free(dev, qpn, 1);
		else if (src == MLX4_IB_RWQ_SRC)
			mlx4_ib_release_wqn(context, qp, dirty_release);
		else
			mlx4_qp_release_range(dev->dev, qpn, 1);
		break;
	case IB_QPG_PARENT:
		free_qpg_parent(dev, qp);
		break;
	case IB_QPG_CHILD_TX:
	case IB_QPG_CHILD_RX:
		free_qpg_qpn(qp, qpn);
		break;
	default:
		break;
	}
}

/* Revert allocation on create_qp_common */
void mlx4_ib_unalloc_qpn_common(struct mlx4_ib_dev *dev, struct mlx4_ib_qp *qp, int qpn,
				struct mlx4_ib_ucontext *context,
				enum mlx4_ib_source_type src,
				bool dirty_release)
{
	free_qpn_common(dev, qp, qpn, context, src, dirty_release);
}

void mlx4_ib_release_qpn_common(struct mlx4_ib_dev *dev, struct mlx4_ib_qp *qp,
				struct mlx4_ib_ucontext *context,
				enum mlx4_ib_source_type src,
				bool dirty_release)
{
	free_qpn_common(dev, qp, qp->mqp.qpn, context, src, dirty_release);
}

int mlx4_ib_check_qpg_attr(struct ib_pd *pd, struct ib_qp_init_attr *init_attr)
{
	struct ib_device *device = pd ? pd->device : init_attr->xrcd->device;
	struct mlx4_ib_dev *dev = to_mdev(device);
	struct ib_exp_qp_init_attr *attr = (struct ib_exp_qp_init_attr *)init_attr;

	if (attr->qpg_type == IB_QPG_NONE)
		return 0;

	if (attr->qp_type != IB_QPT_UD &&
	    attr->qp_type != IB_QPT_RAW_PACKET)
		return -EINVAL;

	if (attr->qpg_type == IB_QPG_PARENT) {
		if (attr->parent_attrib.tss_child_count == 1)
			return -EINVAL; /* Doesn't make sense */
		if (attr->parent_attrib.rss_child_count == 1)
			return -EINVAL; /* Doesn't make sense */
		if ((attr->parent_attrib.tss_child_count == 0) &&
		    (attr->parent_attrib.rss_child_count == 0))
			/* Should be called with IP_QPG_NONE */
			return -EINVAL;
		if (attr->parent_attrib.rss_child_count > 1) {
			int rss_align_num;

			if (!(dev->dev->caps.flags2 & MLX4_DEV_CAP_FLAG2_RSS))
				return -ENOSYS;
			rss_align_num = roundup_pow_of_two(
					attr->parent_attrib.rss_child_count);
			if (rss_align_num > dev->dev->caps.max_rss_tbl_sz)
				return -EINVAL;
		}
	} else {
		struct mlx4_ib_qpg_data *qpg_data;

		if (!attr->qpg_parent)
			return -EINVAL;
		if (IS_ERR(attr->qpg_parent))
			return -EINVAL;
		qpg_data = to_mqp(attr->qpg_parent)->qpg_data;
		if (!qpg_data)
			return -EINVAL;
		if (attr->qpg_type == IB_QPG_CHILD_TX &&
		    !qpg_data->tss_child_count)
			return -EINVAL;
		if (attr->qpg_type == IB_QPG_CHILD_RX &&
		    !qpg_data->rss_child_count)
			return -EINVAL;
	}
	return 0;
}

void mlx4_ib_modify_qp_rss(struct mlx4_ib_dev *dev, struct mlx4_ib_qp *qp,
			   struct mlx4_qp_context *context)
{
	struct mlx4_ib_qpg_data *qpg_data = qp->qpg_data;
	void *rss_context_base = &context->pri_path;
	struct mlx4_rss_context *rss_context =
		(struct mlx4_rss_context *)(rss_context_base
				+ MLX4_RSS_OFFSET_IN_QPC_PRI_PATH);

	context->flags |= cpu_to_be32(1 << MLX4_RSS_QPC_FLAG_OFFSET);

	/* This should be tbl_sz_base_qpn */
	rss_context->base_qpn = cpu_to_be32(qpg_data->rss_qpn_base |
			(ilog2(qpg_data->rss_child_count) << 24));
	rss_context->default_qpn = cpu_to_be32(qpg_data->rss_qpn_base);
	/* This should be flags_hash_fn */
	rss_context->flags = MLX4_RSS_TCP_IPV6 |
			     MLX4_RSS_TCP_IPV4;
	if (dev->dev->caps.flags & MLX4_DEV_CAP_FLAG_UDP_RSS) {
		rss_context->base_qpn_udp = rss_context->default_qpn;
		rss_context->flags |= MLX4_RSS_IPV6 |
				MLX4_RSS_IPV4     |
				MLX4_RSS_UDP_IPV6 |
				MLX4_RSS_UDP_IPV4;
	}

	if (dev->dev->caps.flags2 & MLX4_DEV_CAP_FLAG2_RSS_TOP) {
		static const u32 rsskey[10] = { 0xD181C62C, 0xF7F4DB5B,
			0x1983A2FC, 0x943E1ADB, 0xD9389E6B, 0xD1039C2C,
			0xA74499AD, 0x593D56D9, 0xF3253C06, 0x2ADC1FFC};
		rss_context->hash_fn = MLX4_RSS_HASH_TOP;
		memcpy(rss_context->rss_key, rsskey,
		       sizeof(rss_context->rss_key));
	} else {
		rss_context->hash_fn = MLX4_RSS_HASH_XOR;
		memset(rss_context->rss_key, 0,
		       sizeof(rss_context->rss_key));
	}
}

static struct mlx4_uar *find_user_uar(struct mlx4_ib_ucontext *uctx, unsigned long uar_virt_add)
{
	struct mlx4_ib_user_uar *uar;

	mutex_lock(&uctx->user_uar_mutex);
	list_for_each_entry(uar, &uctx->user_uar_list, list)
		if (uar->hw_bar_info[HW_BAR_DB].vma &&
		    uar->hw_bar_info[HW_BAR_DB].vma->vm_start == uar_virt_add) {
			mutex_unlock(&uctx->user_uar_mutex);
			return &uar->uar;
		}
	mutex_unlock(&uctx->user_uar_mutex);

	return NULL;
}

int mlx4_ib_set_qp_user_uar(struct ib_pd *pd, struct mlx4_ib_qp *qp,
			    struct ib_udata *udata,
			    int is_exp)
{
	struct mlx4_exp_ib_create_qp ucmd = {};

	if (!is_exp)
		goto end;

	if (udata->inlen > offsetof(struct mlx4_exp_ib_create_qp, uar_virt_add)) {

		if (ib_copy_from_udata(&ucmd, udata, sizeof(ucmd)))
			return -EFAULT;

		if (!ucmd.uar_virt_add)
			goto end;

		qp->uar = find_user_uar(to_mucontext(pd->uobject->context),
			   (unsigned long)ucmd.uar_virt_add);
		if (!qp->uar) {
			pr_debug("failed to find user UAR with virt address 0x%llx", ucmd.uar_virt_add);
			return -EINVAL;
		}

		return 0;
	}

end:
	qp->uar = &to_mucontext(pd->uobject->context)->uar;
	return 0;
}
