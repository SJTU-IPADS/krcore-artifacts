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

#include "mlx5_ib.h"
#include "user_exp.h"
#include <linux/mlx5/qp.h>
#include <linux/mlx5/qp_exp.h>
#include <rdma/ib_verbs_exp.h>

int mlx5_ib_exp_get_cmd_data(struct mlx5_ib_dev *dev,
			     struct ib_udata *udata,
			     struct mlx5_ib_create_wq_data *data)
{
	struct mlx5_ib_exp_create_wq ucmd = {};

	if (ib_copy_from_udata(&ucmd, udata, min(sizeof(ucmd),
						 udata->inlen))) {
		mlx5_ib_dbg(dev, "copy failed\n");
		return -EFAULT;
	}

	data->buf_addr = ucmd.buf_addr;
	data->db_addr = ucmd.db_addr;
	data->rq_wqe_count = ucmd.rq_wqe_count;
	data->rq_wqe_shift = ucmd.rq_wqe_shift;
	data->user_index = ucmd.user_index;
	data->flags = ucmd.flags;
	data->comp_mask = ucmd.comp_mask;

	if (ucmd.comp_mask & MLX5_EXP_CREATE_WQ_MP_RQ) {
		if (!MLX5_CAP_GEN(dev->mdev, striding_rq))
			return -EOPNOTSUPP;

		if (ucmd.mp_rq.use_shift & ~IB_MP_RQ_2BYTES_SHIFT ||
		    ucmd.mp_rq.single_stride_log_num_of_bytes <
		    MLX5_MIN_SINGLE_STRIDE_LOG_NUM_BYTES ||
		    ucmd.mp_rq.single_stride_log_num_of_bytes >
		    MLX5_MAX_SINGLE_STRIDE_LOG_NUM_BYTES)
			return -EINVAL;

		if ((ucmd.mp_rq.single_wqe_log_num_of_strides <
		     MLX5_EXT_MIN_SINGLE_WQE_LOG_NUM_STRIDES) ||
		    ((ucmd.mp_rq.single_wqe_log_num_of_strides <
		      MLX5_MIN_SINGLE_WQE_LOG_NUM_STRIDES) &&
		     !MLX5_CAP_GEN(dev->mdev, ext_stride_num_range)))
			return -EINVAL;

		if (ucmd.mp_rq.single_wqe_log_num_of_strides >
		    MLX5_MAX_SINGLE_WQE_LOG_NUM_STRIDES)
			return -EINVAL;

		data->mp_rq.use_shift = ucmd.mp_rq.use_shift;
		data->mp_rq.single_wqe_log_num_of_strides =
			ucmd.mp_rq.single_wqe_log_num_of_strides;
		data->mp_rq.single_stride_log_num_of_bytes =
			ucmd.mp_rq.single_stride_log_num_of_bytes;
	}

	if (ucmd.comp_mask & MLX5_EXP_CREATE_WQ_VLAN_OFFLOADS) {
		if ((ucmd.vlan_offloads & IB_WQ_CVLAN_STRIPPING) &&
		    (!(MLX5_CAP_GEN(dev->mdev, eth_net_offloads) &&
		       MLX5_CAP_ETH(dev->mdev, vlan_cap))))
			return -EOPNOTSUPP;
		data->vlan_offloads = ucmd.vlan_offloads;
	}

	if ((ucmd.flags & MLX5_EXP_WQ_FLAG_RX_END_PADDING) &&
	    !(MLX5_CAP_GEN(dev->mdev, end_pad)))
		return -EOPNOTSUPP;

	if ((ucmd.flags & MLX5_EXP_WQ_FLAG_SCATTER_FCS) &&
	    (!MLX5_CAP_GEN(dev->mdev, eth_net_offloads) ||
	     !MLX5_CAP_ETH(dev->mdev, scatter_fcs)))
		return -EOPNOTSUPP;

	if ((ucmd.flags & MLX5_EXP_WQ_FLAG_DELAY_DROP) &&
	    !(MLX5_CAP_GEN(dev->mdev, rq_delay_drop) &&
	      MLX5_CAP_GEN(dev->mdev, general_notification_event)))
		return -EOPNOTSUPP;

	return 0;
}

void mlx5_ib_exp_set_rq_attr(struct mlx5_ib_create_wq_data *data,
			     struct mlx5_ib_rwq *rwq)
{
	if (data->comp_mask & MLX5_EXP_CREATE_WQ_MP_RQ) {
		rwq->mp_rq.single_wqe_log_num_of_strides =
			data->mp_rq.single_wqe_log_num_of_strides;
		rwq->mp_rq.single_stride_log_num_of_bytes =
			data->mp_rq.single_stride_log_num_of_bytes;
		rwq->mp_rq.use_shift = data->mp_rq.use_shift;
		rwq->mp_rq.use_mp_rq = 1;
	}
	if (data->comp_mask & MLX5_EXP_CREATE_WQ_VLAN_OFFLOADS)
		rwq->vlan_offloads = data->vlan_offloads;

	rwq->flags = data->flags;
}

void mlx5_ib_exp_set_rqc(void *rqc, struct mlx5_ib_rwq *rwq)
{
	void *wq;

	wq = MLX5_ADDR_OF(rqc, rqc, wq);
	if (rwq->mp_rq.use_mp_rq) {
		int log_num_of_strides;
		u8 twos_comp_log_num_of_strides;

		MLX5_SET(wq, wq, wq_type, MLX5_WQ_TYPE_CYCLIC_STRIDING_RQ);

		/* Normalize to device's interface values (range of (-6) - 7) */
		log_num_of_strides =
			rwq->mp_rq.single_wqe_log_num_of_strides - 9;
		/* Convert to 2's complement representation */
		twos_comp_log_num_of_strides = log_num_of_strides < 0 ?
			((u32)(~abs(log_num_of_strides)) + 1) & 0xF :
			(u8)log_num_of_strides;
		MLX5_SET(wq, wq, log_wqe_num_of_strides,
			 twos_comp_log_num_of_strides);

		MLX5_SET(wq, wq, log_wqe_stride_size,
			 (rwq->mp_rq.single_stride_log_num_of_bytes -
			  MLX5_MIN_SINGLE_STRIDE_LOG_NUM_BYTES));

		if (rwq->mp_rq.use_shift == IB_MP_RQ_2BYTES_SHIFT)
			MLX5_SET(wq, wq, two_byte_shift_en, 0x1);
	}
	if (rwq->vlan_offloads & IB_WQ_CVLAN_STRIPPING)
		MLX5_SET(rqc, rqc, vsd, 0);

	if (rwq->flags & MLX5_EXP_WQ_FLAG_RX_END_PADDING)
		MLX5_SET(wq, wq, end_padding_mode, MLX5_WQ_END_PAD_MODE_ALIGN);

	if (rwq->flags & MLX5_EXP_WQ_FLAG_SCATTER_FCS)
		MLX5_SET(rqc, rqc, scatter_fcs, 1);

	if (rwq->flags & MLX5_EXP_WQ_FLAG_DELAY_DROP)
		MLX5_SET(rqc, rqc, delay_drop_en, 1);
}

void mlx5_ib_exp_get_hash_parameters(struct ib_qp_init_attr *init_attr,
				     struct ib_rwq_ind_table **rwq_ind_tbl,
				     u64 *rx_hash_fields_mask,
				     u32 *ind_tbl_num,
				     u8 **rx_hash_key,
				     u8 *rx_hash_function,
				     u8 *rx_key_len)
{
	struct ib_exp_qp_init_attr *exp_init_attr =
		(struct ib_exp_qp_init_attr *)init_attr;
	struct ib_rx_hash_conf *conf = exp_init_attr->rx_hash_conf;

	*rx_hash_fields_mask = conf->rx_hash_fields_mask;
	*rwq_ind_tbl = conf->rwq_ind_tbl;
	*ind_tbl_num = conf->rwq_ind_tbl->ind_tbl_num;
	*rx_hash_key = conf->rx_hash_key;
	*rx_hash_function = conf->rx_hash_function;
	*rx_key_len = conf->rx_key_len;
}

bool mlx5_ib_exp_is_rss(struct ib_qp_init_attr *init_attr)
{
	if (((struct ib_exp_qp_init_attr *)init_attr)->rx_hash_conf)
		return true;
	return false;
}

u32 mlx5_ib_atomic_mode_qp(struct mlx5_ib_qp *qp)
{
	unsigned long mask;
	unsigned long tmp;
	struct mlx5_ib_dev *dev = to_mdev(qp->ibqp.device);

	mask = (qp->ibqp.qp_type == IB_EXP_QPT_DC_INI) ?
		MLX5_CAP_ATOMIC(dev->mdev, atomic_size_dc) :
		MLX5_CAP_ATOMIC(dev->mdev, atomic_size_qp);

	tmp = mask ? __fls(mask) : 0;
	if (tmp < 2)
		return MLX5_ATOMIC_MODE_NONE;

	if (tmp == 2)
		return MLX5_ATOMIC_MODE_CX;

	return tmp << MLX5_ATOMIC_MODE_OFF;
}

int mlx5_ib_exp_is_scat_cqe_dci(struct mlx5_ib_dev *dev,
				enum ib_sig_type sig_type,
				int scqe_sz) {
	return ((sig_type == IB_SIGNAL_ALL_WR) &&
		((scqe_sz == 128) || MLX5_CAP_GEN(dev->mdev, dc_req_scat_data_cqe)));
}

int mlx5_ib_exp_max_inl_recv(struct ib_qp_init_attr *init_attr)
{
	return ((struct ib_exp_qp_init_attr *)init_attr)->max_inl_recv;
}

static inline void
print_qp_init_attr(struct ib_exp_qp_init_attr* init_attr) {
	printk(KERN_INFO, "check [ib_exp_qp_init_attr] "
			"\tcreate_flags: %d\n"
			"\tmax_inline_recv %u\n" 
			"\tcap: ms %u, mr %u, max_line %u\n" \
			, init_attr->create_flags, init_attr->max_inl_recv,\
			init_attr->cap.max_send_wr,
			init_attr->cap.max_recv_wr, 
			init_attr->cap.max_inline_data			
	);
}

  struct ib_qp* mlx5_ib_exp_create_qp(struct ib_pd* pd,
                                      struct ib_exp_qp_init_attr* init_attr,
                                      struct ib_udata* udata)
{
	printk(KERN_INFO, "[mlx5_ib_exp_create_qp] xxx\n");
	//print_qp_init_attr(init_attr);	
	if (pd) {
		struct mlx5_ib_dev *dev;
		int use_inlr;
		int scqe_sz;
		int use_inlr_dci;

		dev = to_mdev(pd->device);

		if ((init_attr->create_flags & IB_QP_EXP_CREATE_ATOMIC_BE_REPLY) &&
		    (dev->atomic_cap != IB_ATOMIC_HCA_REPLY_BE) &&
		    mlx5_host_is_le()) {
			mlx5_ib_dbg(dev, "Create QP with atomic BE REPLY is not supported\n");
			return ERR_PTR(-EINVAL);
		}

		scqe_sz = mlx5_ib_get_cqe_size(init_attr->send_cq);

		use_inlr_dci = (init_attr->qp_type == IB_EXP_QPT_DC_INI)    &&
			       init_attr->max_inl_recv			    &&
			       mlx5_ib_exp_is_scat_cqe_dci(dev,
							   init_attr->sq_sig_type,
							   scqe_sz);

		use_inlr = (init_attr->qp_type == IB_QPT_RC ||
			    init_attr->qp_type == IB_QPT_UC) &&
			    init_attr->max_inl_recv;

		if (use_inlr || use_inlr_dci) {
			int cqe_sz;

			/* DCI can receive only response messages. Hence,
			*  max_inl_recv is reported according to SCQE.
			*/
			cqe_sz = use_inlr_dci ? scqe_sz :
				mlx5_ib_get_cqe_size(init_attr->recv_cq);

			if (cqe_sz == 128)
				init_attr->max_inl_recv = 64;
			else
				init_attr->max_inl_recv = 32;
		} else {
			init_attr->max_inl_recv = 0;
		} 
 	}

	return _mlx5_ib_create_qp(pd, (struct ib_qp_init_attr *)init_attr,
				  udata, 1);
}

static void mlx5_ib_dct_event(struct mlx5_core_qp *qp, int type)
{
	struct mlx5_ib_dct *mdct = (struct mlx5_ib_dct *)qp;
	struct mlx5_ib_dc_target *dc_target = mdct->dc_target;
	struct ib_dct *ibdct = &dc_target->ibdct;
	struct ib_event event;

	if (ibdct->event_handler) {
		event.device     = ibdct->device;
		event.element.dct = ibdct;
		switch (type) {
		case MLX5_EVENT_TYPE_WQ_INVAL_REQ_ERROR:
			event.event = IB_EXP_EVENT_DCT_REQ_ERR;
			break;
		case MLX5_EVENT_TYPE_WQ_ACCESS_ERROR:
			event.event = IB_EXP_EVENT_DCT_ACCESS_ERR;
			break;
		case MLX5_EVENT_TYPE_DCT_KEY_VIOLATION:
			event.event = IB_EXP_EVENT_DCT_KEY_VIOLATION;
			break;
		default:
			pr_warn("mlx5_ib: Unexpected event type %d on DCT %06x\n",
				type, ibdct->dct_num);
			return;
		}

		ibdct->event_handler(&event, ibdct->dct_context);
	}
}

static struct mlx5_ib_qp *dct_create_qp(struct ib_pd *pd,
                                   struct ib_dct_init_attr *attr,
                                   u32 uidx, struct ib_udata *udata)
{
       struct ib_qp_init_attr qp_attr;
       struct ib_qp *qp;
       struct mlx5_ib_qp *mqp;
       void *dctc;
       struct mlx5_ib_dev *dev = to_mdev(pd->device);
       u8 tclass = attr->tclass;

       if (!pd->uobject &&
           dev->ooo.enabled &&
           MLX5_CAP_GEN(dev->mdev, multipath_dc_qp))
              attr->create_flags |= IB_EXP_DCT_OOO_RW_DATA_PLACEMENT;
       if ((attr->create_flags & IB_EXP_DCT_OOO_RW_DATA_PLACEMENT) &&
           !MLX5_CAP_GEN(dev->mdev, multipath_dc_qp))
              return ERR_PTR(-EINVAL);

       if ((attr->srq && attr->srq->srq_type == IB_EXP_SRQT_TAG_MATCHING) &&
           !MLX5_CAP_GEN(dev->mdev, rndv_offload_dc))
              return ERR_PTR(-EINVAL);

       qp_attr.srq = attr->srq;
       qp_attr.recv_cq = attr->cq;

       qp = mlx5_ib_create_dct(pd, &qp_attr, NULL, udata);

       if (IS_ERR(qp))
              return ERR_PTR(PTR_ERR(qp));

       mqp = to_mqp(qp);
       dctc = MLX5_ADDR_OF(create_dct_in, mqp->dct.in, dct_context_entry);
       MLX5_SET64(dctc, dctc, dc_access_key , attr->dc_key);
       MLX5_SET(dctc, dctc, counter_set_id, dev->port[attr->port - 1].cnts.set_id);
       MLX5_SET(dctc, dctc, user_index, uidx);
       if (attr->inline_size) {
	       int cqe_sz = mlx5_ib_get_cqe_size(attr->cq);

	       if (cqe_sz == 128) {
		       MLX5_SET(dctc, dctc, cs_res, MLX5_DCT_CS_RES_64);
		       attr->inline_size = 64;
	       } else {
		       attr->inline_size = 0;
	       }
       }
	
	if (dev->tcd[attr->port - 1].val >= 0)
		tclass = dev->tcd[attr->port - 1].val;
	MLX5_SET(dctc, dctc, tclass, tclass);

       if (attr->create_flags & IB_EXP_DCT_OOO_RW_DATA_PLACEMENT)
	       MLX5_SET(dctc, dctc, multipath, 1);

       if (attr->srq && attr->srq->srq_type == IB_EXP_SRQT_TAG_MATCHING)
              MLX5_SET(dctc, dctc, offload_type, MLX5_DCTC_OFFLOAD_TYPE_RNDV);
	

       return mqp;
}

static int dct_modify_qp_INIT(struct mlx5_ib_qp *mqp, struct ib_dct_init_attr *attr)
{
       struct ib_qp_attr qp_attr;
       int attr_mask = IB_QP_ACCESS_FLAGS |
                     IB_QP_PKEY_INDEX |
                     IB_QP_PORT |
                     IB_QP_STATE;
       struct ib_qp *qp = &mqp->ibqp;
       int err;

       qp_attr.qp_state = IB_QPS_INIT;
       qp_attr.qp_access_flags = attr->access_flags;
       qp_attr.port_num = attr->port;
       qp_attr.pkey_index = attr->pkey_index;

       err = mlx5_ib_modify_dct(qp, &qp_attr, attr_mask, NULL);
       return err;
}

static int dct_modify_qp_RTR(struct mlx5_ib_qp *mqp, struct ib_dct_init_attr *attr)
{
	struct ib_qp_attr qp_attr;
	int attr_mask = IB_QP_MIN_RNR_TIMER |
		IB_QP_AV |
		IB_QP_PATH_MTU |
		IB_QP_STATE;
	struct ib_qp *qp = &mqp->ibqp;
	int err;

	qp_attr.qp_state = IB_QPS_RTR;
	qp_attr.ah_attr.grh.flow_label = attr->flow_label;
	qp_attr.path_mtu = attr->mtu;
	qp_attr.ah_attr.grh.sgid_index = attr->gid_index;
	qp_attr.ah_attr.grh.hop_limit = attr->hop_limit;
	qp_attr.ah_attr.grh.traffic_class = attr->tclass;
	qp_attr.min_rnr_timer = attr->min_rnr_timer;

	err = mlx5_ib_modify_dct(qp, &qp_attr, attr_mask, NULL);
	return err;
}

struct ib_dct *mlx5_ib_create_dc_target(struct ib_pd *pd,
                               struct ib_dct_init_attr *attr,
                                      struct ib_udata *udata)
{
	struct mlx5_ib_create_dct ucmd;
	struct mlx5_ib_dev *dev = to_mdev(pd->device);
	struct mlx5_ib_dc_target *dct;
	int err;
	u32 uidx = 0;

	if (!rdma_is_port_valid(&dev->ib_dev, attr->port))
		return ERR_PTR(-EINVAL);

	if (mlx5_lag_is_active(dev->mdev) && !MLX5_CAP_GEN(dev->mdev, lag_dct))
		return ERR_PTR(-EOPNOTSUPP);

	if (pd && pd->uobject) {
		if (ib_copy_from_udata(&ucmd, udata, sizeof(ucmd))) {
			mlx5_ib_err(dev, "ib_copy_from_udata failed\n");
			return ERR_PTR(-EFAULT);
		}

		if (udata->inlen && MLX5_CAP_GEN(dev->mdev, cqe_version))
			uidx = ucmd.uidx;
		else
			uidx = 0xffffff;
	} else {
		uidx = 0xffffff;
	}

	dct = kzalloc(sizeof(*dct), GFP_KERNEL);
	if (!dct)
		return ERR_PTR(-ENOMEM);
	dct->qp = dct_create_qp(pd, attr, uidx, udata);
	if (IS_ERR(dct->qp)) {
		err = PTR_ERR(dct->qp);
		goto err_free;
	}
	dct->qp->ibqp.pd = pd;
	dct->qp->ibqp.device = pd->device;
	err = dct_modify_qp_INIT(dct->qp, attr);
	if (err)
		goto err_destroy;

	err = dct_modify_qp_RTR(dct->qp, attr);
	if (err)
		goto err_destroy;

	dct->ibdct.dct_num = dct->qp->dct.mdct.mqp.qpn;
	dct->qp->dct.dc_target = dct;
	dct->qp->dct.mdct.mqp.event = mlx5_ib_dct_event;

	return &dct->ibdct;
err_destroy:
	mlx5_ib_destroy_dct(dct->qp);
err_free:
        kfree(dct);
        return ERR_PTR(err);
}

int mlx5_ib_destroy_dc_target(struct ib_dct *dct, struct ib_udata *udata)
{
	struct mlx5_ib_dc_target *mdct = to_mdct(dct);
	int err;

	err = mlx5_ib_destroy_qp(&mdct->qp->ibqp, udata);
	kfree(mdct);
	
	return err;
}

int dct_to_ib_access(u32 dc_flags)
{
	u32 flags = 0;

	if (dc_flags & MLX5_DCT_BIT_RRE)
		flags |= IB_ACCESS_REMOTE_READ;
	if (dc_flags & MLX5_QP_BIT_RWE)
		flags |= IB_ACCESS_REMOTE_WRITE;
	if ((dc_flags & MLX5_ATOMIC_MODE_CX) == MLX5_ATOMIC_MODE_CX)
		flags |= IB_ACCESS_REMOTE_ATOMIC;

	return flags;
}

int mlx5_ib_query_dc_target(struct ib_dct *dct, struct ib_dct_attr *attr)
{
	struct mlx5_ib_dev *dev = to_mdev(dct->device);
	struct mlx5_ib_dc_target *mdct = to_mdct(dct);
	u32 dc_flags = 0;
	u32 *out;
	int outlen = MLX5_ST_SZ_BYTES(query_dct_out);
	void *dctc;
	int err;

	out = kzalloc(outlen, GFP_KERNEL);
	if (!out)
		return -ENOMEM;

	err = mlx5_core_dct_query(dev->mdev, &mdct->qp->dct.mdct, out, outlen);
	if (err)
		goto out;

	dctc = MLX5_ADDR_OF(query_dct_out, out, dct_context_entry);

	if (MLX5_GET(dctc, dctc, rre))
		dc_flags |= MLX5_DCT_BIT_RRE;
	if (MLX5_GET(dctc, dctc, rwe))
		dc_flags |= MLX5_DCT_BIT_RWE;
	if (MLX5_GET(dctc, dctc, rae))
		dc_flags |= MLX5_DCT_BIT_RAE;

	attr->dc_key = MLX5_GET64(dctc, dctc, dc_access_key);
	attr->port = MLX5_GET(dctc, dctc, port);
	attr->access_flags = dct_to_ib_access(dc_flags);
	attr->min_rnr_timer = MLX5_GET(dctc, dctc, min_rnr_nak);
	attr->tclass = MLX5_GET(dctc, dctc, tclass);
	attr->flow_label = MLX5_GET(dctc, dctc, flow_label);
	attr->mtu = MLX5_GET(dctc, dctc, mtu);
	attr->pkey_index = MLX5_GET(dctc, dctc, pkey_index);
	attr->gid_index = MLX5_GET(dctc, dctc, my_addr_index);
	attr->hop_limit = MLX5_GET(dctc, dctc, hop_limit);
	attr->key_violations = MLX5_GET(dctc, dctc,
					dc_access_key_violation_count);
	attr->state = MLX5_GET(dctc, dctc, state);

out:
	kfree(out);
	return err;
}

int mlx5_ib_arm_dc_target(struct ib_dct *dct, struct ib_udata *udata)
{
	struct mlx5_ib_dev *dev = to_mdev(dct->device);
	struct mlx5_ib_dc_target *mdct = to_mdct(dct);
	struct mlx5_ib_arm_dct ucmd;
	struct mlx5_ib_arm_dct_resp resp;
	int err;

	err = ib_copy_from_udata(&ucmd, udata, sizeof(ucmd));
	if (err) {
		mlx5_ib_err(dev, "copy failed\n");
		return err;
	}

	if (ucmd.reserved0 || ucmd.reserved1)
		return -EINVAL;

	err = mlx5_core_arm_dct(dev->mdev, &mdct->qp->dct.mdct);
	if (err)
		goto out;

	memset(&resp, 0, sizeof(resp));
	err = ib_copy_to_udata(udata, &resp, sizeof(resp));
	if (err)
		mlx5_ib_err(dev, "copy failed\n");

out:
	return err;
}

void mlx5_ib_set_mlx_seg(struct mlx5_mlx_seg *seg, struct mlx5_mlx_wr *wr)
{
	memset(seg, 0, sizeof(*seg));
	seg->stat_rate_sl = wr->sl & 0xf;
	seg->dlid = cpu_to_be16(wr->dlid);
	seg->flags = wr->icrc ? 8 : 0;
}

int mlx5_ib_set_qp_offload_type(struct mlx5_qp_context *context, struct ib_qp *qp,
				enum ib_qp_offload_type offload_type)
{
	switch (offload_type) {
	case IB_QP_OFFLOAD_NVMF:
		if (qp->srq &&
		    qp->srq->srq_type == IB_EXP_SRQT_NVMF) {
			context->flags |= cpu_to_be32(MLX5_QPC_OFFLOAD_TYPE_NVMF << 4);
			break;
		}
	/* Fall through */
	default:
		return -EINVAL;
	}

	return 0;
}

int mlx5_ib_set_qp_srqn(struct mlx5_qp_context *context, struct ib_qp *qp,
			u32 srqn)
{
	struct mlx5_ib_dev *dev = to_mdev(qp->device);
	struct mlx5_srq_table *table = &dev->srq_table;
	struct mlx5_core_srq *msrq;

	if (to_mqp(qp)->rq_type != MLX5_SRQ_RQ)
		return -EINVAL;

	xa_lock(&table->array);
	msrq = xa_load(&table->array, srqn);
	xa_unlock(&table->array);
	if (!msrq)
		return -EINVAL;

	qp->srq = &to_mibsrq(msrq)->ibsrq;
	context->rq_type_srqn |= cpu_to_be32(srqn & 0xffffff);

	return 0;
}
