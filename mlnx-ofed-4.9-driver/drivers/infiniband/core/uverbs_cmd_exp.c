/*
 * Copyright (c) 2015 Mellanox Technologies.  All rights reserved.
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

#include <linux/file.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/moduleparam.h>
#include <linux/printk.h>
#include <rdma/ib_addr.h>
#include <rdma/ib_umem_odp.h>
#include <linux/sched.h>

#include <asm/uaccess.h>
#include <linux/sched.h>

#include "uverbs.h"
#include "uverbs_exp.h"
#include "core_priv.h"

int ib_uverbs_exp_create_flow(struct uverbs_attr_bundle *attrs)
{
	return ib_uverbs_create_flow_common(attrs, true);
}

int ib_uverbs_exp_create_qp(struct uverbs_attr_bundle *attrs)
{
	struct ib_uqp_object           *obj;
	struct ib_device	       *device;
	struct ib_device            *ib_dev;
	struct ib_pd                   *pd = NULL;
	struct ib_xrcd		       *xrcd = NULL;
	struct ib_uobject	       *xrcd_uobj = ERR_PTR(-ENOENT);
	struct ib_cq                   *scq = NULL, *rcq = NULL;
	struct ib_srq                  *srq = NULL;
	struct ib_qp                   *qp;
	struct ib_qp                   *parentqp = NULL;
	struct ib_exp_qp_init_attr     *attr;
	struct ib_uverbs_exp_create_qp *cmd_exp;
	struct ib_uverbs_exp_create_qp_resp resp_exp;
	int                             ret;
	struct ib_rx_hash_conf	rx_hash_conf;
	struct ib_rwq_ind_table *ind_tbl = NULL;
	int rx_qp = 0;
	int i;

	cmd_exp = kzalloc(sizeof(*cmd_exp), GFP_KERNEL);
	attr = kzalloc(sizeof(*attr), GFP_KERNEL);
	if (!cmd_exp || !attr) {
		ret = -ENOMEM;
		goto err_cmd_attr;
	}

	ret = ib_copy_from_udata(cmd_exp, &attrs->ucore, sizeof(*cmd_exp));
	if (ret)
		goto err_cmd_attr;

	if (!disable_raw_qp_enforcement &&
	    cmd_exp->qp_type == IB_QPT_RAW_PACKET && !capable(CAP_NET_RAW)) {
		ret = -EPERM;
		goto err_cmd_attr;
	}

	for (i = 0; i < sizeof(cmd_exp->reserved3); i++) {
		if (cmd_exp->reserved3[i] != 0) {
			ret = -EINVAL;
			goto err_cmd_attr;
		}
	}

	obj  = (struct ib_uqp_object *)uobj_alloc(UVERBS_OBJECT_QP,
						  attrs, &ib_dev);
	if (IS_ERR(obj))
		return PTR_ERR(obj);
	obj->uxrcd = NULL;
	obj->uevent.uobject.user_handle = cmd_exp->user_handle;
	mutex_init(&obj->mcast_lock);

	rx_qp = cmd_exp->rx_hash_conf.rx_hash_function ? 1 : 0;

	if (cmd_exp->qp_type == IB_QPT_XRC_TGT) {
		xrcd_uobj = uobj_get_read(UVERBS_OBJECT_XRCD, cmd_exp->pd_handle,
					  attrs);

		if (IS_ERR(xrcd_uobj)) {
			ret = -EINVAL;
			goto err_put;
		}

		xrcd = (struct ib_xrcd *)xrcd_uobj->object;
		if (!xrcd) {
			ret = -EINVAL;
			goto err_put;
		}
		device = xrcd->device;
	} else {
		if (cmd_exp->qp_type == IB_QPT_XRC_INI ||
		    cmd_exp->qp_type == IB_EXP_QPT_DC_INI) {
		  //printk(KERN_INFO "[uverbs_cmd_exp] in dc qp setup");
			cmd_exp->max_recv_wr = 0;
			cmd_exp->max_recv_sge = 0;
		} else {
			if (cmd_exp->is_srq) {
				srq = uobj_get_obj_read(srq, UVERBS_OBJECT_SRQ,
							cmd_exp->srq_handle,
							attrs);
				if (!srq || srq->srq_type == IB_SRQT_XRC) {
					ret = -EINVAL;
					goto err_put;
				}
			}

			if (cmd_exp->recv_cq_handle != cmd_exp->send_cq_handle) {
				rcq = uobj_get_obj_read(cq, UVERBS_OBJECT_CQ,
							cmd_exp->recv_cq_handle,
							attrs);
				if (!rcq) {
					ret = -EINVAL;
					goto err_put;
				}
			}
		}

		if (!rx_qp)
			scq = uobj_get_obj_read(cq, UVERBS_OBJECT_CQ, cmd_exp->send_cq_handle,
						attrs);

		rcq = rcq ?: scq;
		pd  = uobj_get_obj_read(pd, UVERBS_OBJECT_PD,
					cmd_exp->pd_handle, attrs);
		if (!pd || (!scq && !rx_qp)) {
			ret = -EINVAL;
			goto err_put;
		}

		device = pd->device;
	}

	attr->event_handler = ib_uverbs_qp_event_handler;
	attr->qp_context    = attrs->ufile;
	attr->send_cq       = scq;
	attr->recv_cq       = rcq;
	attr->srq           = srq;
	attr->xrcd	   = xrcd;
	attr->sq_sig_type   = cmd_exp->sq_sig_all ? IB_SIGNAL_ALL_WR : IB_SIGNAL_REQ_WR;
	attr->qp_type       = cmd_exp->qp_type;
	attr->create_flags  = 0;

	attr->cap.max_send_wr     = cmd_exp->max_send_wr;
	attr->cap.max_recv_wr     = cmd_exp->max_recv_wr;
	attr->cap.max_send_sge    = cmd_exp->max_send_sge;
	attr->cap.max_recv_sge    = cmd_exp->max_recv_sge;
	attr->cap.max_inline_data = cmd_exp->max_inline_data;
	attr->rx_hash_conf = NULL;

	if (cmd_exp->comp_mask & IB_UVERBS_EXP_CREATE_QP_CAP_FLAGS) {
		if (cmd_exp->qp_cap_flags & ~IBV_UVERBS_EXP_CREATE_QP_FLAGS) {
			ret = -EINVAL;
			goto err_put;
		}

		attr->create_flags |= cmd_exp->qp_cap_flags;

		/* convert user requset to kernel matching creation flag */

		if (attr->create_flags & IB_QP_EXP_USER_CREATE_ATOMIC_BE_REPLY) {
			attr->create_flags &= ~IB_QP_EXP_USER_CREATE_ATOMIC_BE_REPLY;
			attr->create_flags |= IB_QP_EXP_CREATE_ATOMIC_BE_REPLY;
		}
		if (attr->create_flags & IB_QP_EXP_USER_CREATE_RX_END_PADDING) {
			attr->create_flags &= ~IB_QP_EXP_USER_CREATE_RX_END_PADDING;
			attr->create_flags |= IB_QP_EXP_CREATE_RX_END_PADDING;
		}

		if (attr->create_flags & IB_QP_EXP_USER_CREATE_SCATTER_FCS) {
			attr->create_flags &= ~IB_QP_EXP_USER_CREATE_SCATTER_FCS;
			attr->create_flags |= IB_QP_CREATE_SCATTER_FCS;
		}

		if (attr->create_flags & IB_QP_EXP_USER_CREATE_TUNNEL_OFFLOADS) {
			attr->create_flags &= ~IB_QP_EXP_USER_CREATE_TUNNEL_OFFLOADS;
			attr->create_flags |= IB_QP_EXP_CREATE_TUNNEL_OFFLOADS;
		}

		if (attr->create_flags &
		    IB_QP_EXP_USER_CREATE_PACKET_BASED_CREDIT_MODE) {
			attr->create_flags &=
				~IB_QP_EXP_USER_CREATE_PACKET_BASED_CREDIT_MODE;
			attr->create_flags |=
				IB_QP_EXP_CREATE_PACKET_BASED_CREDIT_MODE;
		}

	}

	if (cmd_exp->comp_mask & IB_UVERBS_EXP_CREATE_QP_QPG) {
		struct ib_uverbs_qpg *qpg;

		if (cmd_exp->qp_type != IB_QPT_RAW_PACKET &&
		    cmd_exp->qp_type != IB_QPT_UD) {
			ret = -EINVAL;
			goto err_put;
		}
		qpg = &cmd_exp->qpg;
		switch (qpg->qpg_type) {
		case IB_QPG_PARENT:
			attr->parent_attrib.rss_child_count =
				qpg->parent_attrib.rss_child_count;
			attr->parent_attrib.tss_child_count =
				qpg->parent_attrib.tss_child_count;
			break;
		case IB_QPG_CHILD_RX:
		case IB_QPG_CHILD_TX:
			parentqp = uobj_get_obj_read(qp, UVERBS_OBJECT_QP, qpg->parent_handle,
						     attrs);
			if (!parentqp) {
				ret = -EINVAL;
				goto err_put;
			}
			attr->qpg_parent = parentqp;
			break;
		default:
			ret = -EINVAL;
			goto err_put;
		}
		attr->qpg_type = qpg->qpg_type;
	}

	if (cmd_exp->comp_mask & IB_UVERBS_EXP_CREATE_QP_INL_RECV)
		attr->max_inl_recv = cmd_exp->max_inl_recv;

	/* No comp mask bit is needed, the value of rx_hash_function is used */
	if (cmd_exp->rx_hash_conf.rx_hash_function) {
		ind_tbl = uobj_get_obj_read(rwq_ind_table,
					    UVERBS_OBJECT_RWQ_IND_TBL,
					    cmd_exp->rx_hash_conf.rwq_ind_tbl_handle,
					    attrs);
		if (!ind_tbl) {
			ret = -EINVAL;
			goto err_put;
		}
		rx_hash_conf.rwq_ind_tbl = ind_tbl;
		rx_hash_conf.rx_hash_fields_mask = cmd_exp->rx_hash_conf.rx_hash_fields_mask;
		rx_hash_conf.rx_hash_function = cmd_exp->rx_hash_conf.rx_hash_function;
		rx_hash_conf.rx_hash_key = cmd_exp->rx_hash_conf.rx_hash_key;
		rx_hash_conf.rx_key_len = cmd_exp->rx_hash_conf.rx_key_len;
		attr->rx_hash_conf = &rx_hash_conf;
	}

	attr->port_num = cmd_exp->port_num;
	obj->uevent.events_reported     = 0;
	INIT_LIST_HEAD(&obj->uevent.event_list);
	INIT_LIST_HEAD(&obj->mcast_list);

	if (cmd_exp->qp_type == IB_QPT_XRC_TGT)
		qp = ib_create_qp(pd, (struct ib_qp_init_attr *)attr);
	else {
          //printk(KERN_INFO "[uverbs_cmd_exp] in exp qp");
          qp = device->ops.exp_create_qp(pd, attr, &attrs->driver_udata);
	}

	if (IS_ERR(qp)) {
		ret = PTR_ERR(qp);
		goto err_put;
	}

	if (cmd_exp->qp_type != IB_QPT_XRC_TGT) {
	  //printk(KERN_INFO "[uverbs_cmd_exp] in create qp security");
		ret = ib_create_qp_security(qp, device);
		if (ret)
			goto err_copy;

		qp->real_qp	  = qp;
		qp->device	  = device;
		qp->pd		  = pd;
		qp->send_cq	  = attr->send_cq;
		qp->recv_cq	  = attr->recv_cq;
		qp->srq		  = attr->srq;
		qp->rwq_ind_tbl   = ind_tbl;
		qp->event_handler = attr->event_handler;
		qp->qp_context	  = attr->qp_context;
		qp->qp_type	  = attr->qp_type;
		atomic_set(&qp->usecnt, 0);
		atomic_inc(&pd->usecnt);
		if (!rx_qp)
			atomic_inc(&attr->send_cq->usecnt);
		if (attr->recv_cq)
			atomic_inc(&attr->recv_cq->usecnt);
		if (attr->srq)
			atomic_inc(&attr->srq->usecnt);
		if (ind_tbl)
			atomic_inc(&ind_tbl->usecnt);
	}
	qp->uobject = obj;

	/*
	 * We don't track XRC QPs for now, because they don't have PD
	 * and more importantly they are created internaly by driver,
	 * see mlx5 create_dev_resources() as an example.
	 */
	qp->res.type = RDMA_RESTRACK_QP;
	if (attr->qp_type < IB_QPT_XRC_INI)
		rdma_restrack_uadd(&qp->res);
	else
		rdma_restrack_dontrack(&qp->res);

	obj->uevent.uobject.object = qp;

	memset(&resp_exp, 0, sizeof(resp_exp));
	resp_exp.qpn             = qp->qp_num;
	resp_exp.qp_handle       = obj->uevent.uobject.id;
	resp_exp.max_recv_sge    = attr->cap.max_recv_sge;
	resp_exp.max_send_sge    = attr->cap.max_send_sge;
	resp_exp.max_recv_wr     = attr->cap.max_recv_wr;
	resp_exp.max_send_wr     = attr->cap.max_send_wr;
	resp_exp.max_inline_data = attr->cap.max_inline_data;

	if (cmd_exp->comp_mask & IB_UVERBS_EXP_CREATE_QP_INL_RECV) {
		resp_exp.comp_mask |= IB_UVERBS_EXP_CREATE_QP_RESP_INL_RECV;
		resp_exp.max_inl_recv = attr->max_inl_recv;
	}


	ret = ib_copy_to_udata(&attrs->ucore, &resp_exp, sizeof(resp_exp));
	if (ret)
		goto err_copy;

	if (xrcd) {
		obj->uxrcd = container_of(xrcd_uobj, struct ib_uxrcd_object, uobject);
		atomic_inc(&obj->uxrcd->refcnt);
		uobj_put_read(xrcd_uobj);
	}

	if (pd)
		uobj_put_obj_read(pd);
	if (scq)
		rdma_lookup_put_uobject(&scq->uobject->uevent.uobject,
					UVERBS_LOOKUP_READ);
	if (rcq && rcq != scq)
		rdma_lookup_put_uobject(&rcq->uobject->uevent.uobject,
					UVERBS_LOOKUP_READ);
	if (srq)
		rdma_lookup_put_uobject(&srq->uobject->uevent.uobject,
					UVERBS_LOOKUP_READ);
	if (parentqp)
		rdma_lookup_put_uobject(&parentqp->uobject->uevent.uobject,
					UVERBS_LOOKUP_READ);
	if (ind_tbl)
		uobj_put_obj_read(ind_tbl);

	kfree(attr);
	kfree(cmd_exp);

	rdma_alloc_commit_uobject(&obj->uevent.uobject, attrs);
	return 0;

err_copy:
	ib_destroy_qp_user(qp, uverbs_get_cleared_udata(attrs));

err_put:
	if (!IS_ERR(xrcd_uobj))
		uobj_put_read(xrcd_uobj);
	if (pd)
		uobj_put_obj_read(pd);
	if (scq)
		rdma_lookup_put_uobject(&scq->uobject->uevent.uobject,
					UVERBS_LOOKUP_READ);
	if (rcq && rcq != scq)
		rdma_lookup_put_uobject(&rcq->uobject->uevent.uobject,
					UVERBS_LOOKUP_READ);
	if (srq)
		rdma_lookup_put_uobject(&srq->uobject->uevent.uobject,
					UVERBS_LOOKUP_READ);
	if (parentqp)
		rdma_lookup_put_uobject(&parentqp->uobject->uevent.uobject,
					UVERBS_LOOKUP_READ);
	if (ind_tbl)
		uobj_put_obj_read(ind_tbl);

	uobj_alloc_abort(&obj->uevent.uobject, attrs);

err_cmd_attr:
	kfree(attr);
	kfree(cmd_exp);
	return ret;
}

int ib_uverbs_exp_modify_cq(struct uverbs_attr_bundle *attrs)
{
	struct ib_uverbs_exp_modify_cq cmd;
	struct ib_cq               *cq;
	struct ib_cq_attr           attr;
	int                         ret;

	memset(&cmd, 0, sizeof(cmd));
	ret = ib_copy_from_udata(&cmd, &attrs->ucore, sizeof(cmd));
	if (ret)
		return ret;

	if (cmd.comp_mask >= IB_UVERBS_EXP_CQ_ATTR_RESERVED)
		return -ENOSYS;

	cq = uobj_get_obj_read(cq, UVERBS_OBJECT_CQ, cmd.cq_handle, attrs);
	if (!cq)
		return -EINVAL;

	attr.moderation.cq_count  = cmd.cq_count;
	attr.moderation.cq_period = cmd.cq_period;
	attr.cq_cap_flags         = cmd.cq_cap_flags;

	ret = ib_exp_modify_cq(cq, &attr, cmd.attr_mask);

	rdma_lookup_put_uobject(&cq->uobject->uevent.uobject, UVERBS_LOOKUP_READ);
	return ret;
}

int ib_uverbs_exp_query_device(struct uverbs_attr_bundle *attrs)
{
	struct ib_uverbs_exp_query_device_resp *resp;
	struct ib_uverbs_exp_query_device cmd;
	struct ib_exp_device_attr              *exp_attr;
	int                                    ret;
	struct ib_ucontext *ucontext;

	ucontext = ib_uverbs_get_ucontext(attrs);
	if (IS_ERR(ucontext))
		return PTR_ERR(ucontext);

	ret = ib_copy_from_udata(&cmd,  &attrs->ucore, sizeof(cmd));
	if (ret)
		return ret;

	resp = kzalloc(sizeof(*resp), GFP_KERNEL);
	exp_attr = kzalloc(sizeof(*exp_attr), GFP_KERNEL);
	if (!exp_attr || !resp) {
		ret = -ENOMEM;
		goto out;
	}

	ret = ib_exp_query_device(ucontext->device, exp_attr, &attrs->driver_udata);
	if (ret)
		goto out;

	memset(resp, 0, sizeof(*resp));
	copy_query_dev_fields(ucontext, &resp->base, &exp_attr->base);

	resp->comp_mask = 0;
	resp->device_cap_flags2 = 0;

	if (exp_attr->exp_comp_mask & IB_EXP_DEVICE_ATTR_WITH_TIMESTAMP_MASK) {
		resp->timestamp_mask = exp_attr->base.timestamp_mask;
		resp->comp_mask |= IB_EXP_DEVICE_ATTR_WITH_TIMESTAMP_MASK;
	}

	if (exp_attr->exp_comp_mask & IB_EXP_DEVICE_ATTR_WITH_HCA_CORE_CLOCK) {
		resp->hca_core_clock = exp_attr->base.hca_core_clock;
		resp->comp_mask |= IB_EXP_DEVICE_ATTR_WITH_HCA_CORE_CLOCK;
	}

	if (exp_attr->exp_comp_mask & IB_EXP_DEVICE_ATTR_DC_REQ_RD) {
		resp->dc_rd_req = exp_attr->dc_rd_req;
		resp->comp_mask |= IB_EXP_DEVICE_ATTR_DC_REQ_RD;
	}

	if (exp_attr->exp_comp_mask & IB_EXP_DEVICE_ATTR_DC_RES_RD) {
		resp->dc_rd_res = exp_attr->dc_rd_res;
		resp->comp_mask |= IB_EXP_DEVICE_ATTR_DC_RES_RD;
	}

	if (exp_attr->exp_comp_mask & IB_EXP_DEVICE_ATTR_MAX_DCT) {
		resp->max_dct = exp_attr->max_dct;
		resp->comp_mask |= IB_EXP_DEVICE_ATTR_MAX_DCT;
	}

	if (exp_attr->exp_comp_mask & IB_EXP_DEVICE_ATTR_MAX_DEVICE_CTX) {
		resp->max_device_ctx = exp_attr->max_device_ctx;
		resp->comp_mask |= IB_EXP_DEVICE_ATTR_MAX_DEVICE_CTX;
	}

	/* Handle experimental attr fields */
	if (exp_attr->exp_comp_mask & IB_EXP_DEVICE_ATTR_CAP_FLAGS2 ||
	    exp_attr->base.device_cap_flags & IB_EXP_DEVICE_MASK) {
		resp->device_cap_flags2 = exp_attr->device_cap_flags2;
		resp->comp_mask |= IB_EXP_DEVICE_ATTR_CAP_FLAGS2;
		resp->device_cap_flags2 |= IB_EXP_DEVICE_MASK & exp_attr->base.device_cap_flags;
		resp->base.device_cap_flags &= ~IB_EXP_DEVICE_MASK;
		if (resp->device_cap_flags2 & IB_DEVICE_CROSS_CHANNEL) {
			resp->device_cap_flags2 &= ~IB_DEVICE_CROSS_CHANNEL;
			resp->device_cap_flags2 |= IB_EXP_DEVICE_CROSS_CHANNEL;
		}
	}

	if (exp_attr->exp_comp_mask & IB_EXP_DEVICE_ATTR_RSS_TBL_SZ) {
		resp->max_rss_tbl_sz = exp_attr->max_rss_tbl_sz;
		resp->comp_mask |= IB_EXP_DEVICE_ATTR_RSS_TBL_SZ;
	}

	if (exp_attr->exp_comp_mask & IB_EXP_DEVICE_ATTR_UMR) {
		resp->umr_caps.max_reg_descriptors = exp_attr->umr_caps.max_reg_descriptors;
		resp->umr_caps.max_send_wqe_inline_klms = exp_attr->umr_caps.max_send_wqe_inline_klms;
		resp->umr_caps.max_umr_recursion_depth = exp_attr->umr_caps.max_umr_recursion_depth;
		resp->umr_caps.max_umr_stride_dimenson = exp_attr->umr_caps.max_umr_stride_dimenson;
		resp->comp_mask |= IB_EXP_DEVICE_ATTR_UMR;
	}

	if (exp_attr->exp_comp_mask & IB_EXP_DEVICE_ATTR_ODP) {
		resp->comp_mask |= IB_EXP_DEVICE_ATTR_ODP;
		resp->odp_caps.general_odp_caps = exp_attr->odp_caps.general_odp_caps;
		resp->odp_caps.per_transport_caps.rc_odp_caps =
			exp_attr->odp_caps.per_transport_caps.rc_odp_caps;
		resp->odp_caps.per_transport_caps.uc_odp_caps =
			exp_attr->odp_caps.per_transport_caps.uc_odp_caps;
		resp->odp_caps.per_transport_caps.ud_odp_caps =
			exp_attr->odp_caps.per_transport_caps.ud_odp_caps;
		resp->odp_caps.per_transport_caps.dc_odp_caps =
			exp_attr->odp_caps.per_transport_caps.dc_odp_caps;
		resp->odp_caps.per_transport_caps.xrc_odp_caps =
			exp_attr->odp_caps.per_transport_caps.xrc_odp_caps;
		resp->odp_caps.per_transport_caps.raw_eth_odp_caps =
			exp_attr->odp_caps.per_transport_caps.raw_eth_odp_caps;
	}

	if (exp_attr->exp_comp_mask & IB_EXP_DEVICE_ATTR_INLINE_RECV_SZ) {
		resp->inline_recv_sz = exp_attr->inline_recv_sz;
		resp->comp_mask |= IB_EXP_DEVICE_ATTR_INLINE_RECV_SZ;
	}

	if (exp_attr->exp_comp_mask & IB_EXP_DEVICE_ATTR_EXT_ATOMIC_ARGS) {
		resp->comp_mask |= IB_EXP_DEVICE_ATTR_EXT_ATOMIC_ARGS;
		resp->atomic_arg_sizes = exp_attr->atomic_arg_sizes;
		resp->max_fa_bit_boudary = exp_attr->max_fa_bit_boudary;
		resp->log_max_atomic_inline_arg = exp_attr->log_max_atomic_inline_arg;
	}

	if (exp_attr->exp_comp_mask & IB_EXP_DEVICE_ATTR_MAX_CTX_RES_DOMAIN) {
		resp->max_ctx_res_domain = exp_attr->max_ctx_res_domain;
		resp->comp_mask |= IB_EXP_DEVICE_ATTR_MAX_CTX_RES_DOMAIN;
	}

	if (exp_attr->exp_comp_mask & IB_EXP_DEVICE_ATTR_EXT_MASKED_ATOMICS) {
		struct ib_uverbs_exp_masked_atomic_caps *resp_atom;
		struct ib_exp_masked_atomic_caps *atom_caps;

		resp_atom = &resp->masked_atomic_caps;
		atom_caps = &exp_attr->masked_atomic_caps;
		resp_atom->masked_log_atomic_arg_sizes =
			atom_caps->masked_log_atomic_arg_sizes;
		resp_atom->masked_log_atomic_arg_sizes_network_endianness =
			atom_caps->masked_log_atomic_arg_sizes_network_endianness;
		resp_atom->max_fa_bit_boudary = exp_attr->max_fa_bit_boudary;
		resp_atom->log_max_atomic_inline_arg =
			exp_attr->log_max_atomic_inline_arg;
		resp->comp_mask |= IB_EXP_DEVICE_ATTR_EXT_MASKED_ATOMICS;
	}

	if (exp_attr->exp_comp_mask & IB_EXP_DEVICE_ATTR_TSO_CAPS) {
		resp->tso_caps.max_tso = exp_attr->tso_caps.max_tso;
		resp->tso_caps.supported_qpts = exp_attr->tso_caps.supported_qpts;
		resp->comp_mask |= IB_EXP_DEVICE_ATTR_TSO_CAPS;
	}

	if (exp_attr->exp_comp_mask & IB_EXP_DEVICE_ATTR_MAX_DEVICE_CTX) {
		resp->max_device_ctx = exp_attr->max_device_ctx;
		resp->comp_mask |= IB_EXP_DEVICE_ATTR_MAX_DEVICE_CTX;
	}
	if (exp_attr->exp_comp_mask & IB_EXP_DEVICE_ATTR_MAX_WQ_TYPE_RQ) {
		resp->max_wq_type_rq = exp_attr->max_wq_type_rq;
		resp->comp_mask |= IB_EXP_DEVICE_ATTR_MAX_WQ_TYPE_RQ;
	}

	if (exp_attr->exp_comp_mask & IB_EXP_DEVICE_ATTR_RX_HASH) {
		resp->rx_hash.max_rwq_indirection_tables = exp_attr->rx_hash_caps.max_rwq_indirection_tables;
		resp->rx_hash.max_rwq_indirection_table_size = exp_attr->rx_hash_caps.max_rwq_indirection_table_size;
		resp->rx_hash.supported_packet_fields = exp_attr->rx_hash_caps.supported_packet_fields;
		resp->rx_hash.supported_qps = exp_attr->rx_hash_caps.supported_qps;
		resp->rx_hash.supported_hash_functions = exp_attr->rx_hash_caps.supported_hash_functions;
		resp->comp_mask |= IB_EXP_DEVICE_ATTR_RX_HASH;
	}

	if (exp_attr->exp_comp_mask & IB_EXP_DEVICE_ATTR_MP_RQ) {
		resp->mp_rq_caps.allowed_shifts =  exp_attr->mp_rq_caps.allowed_shifts;
		resp->mp_rq_caps.supported_qps =  exp_attr->mp_rq_caps.supported_qps;
		resp->mp_rq_caps.max_single_stride_log_num_of_bytes =  exp_attr->mp_rq_caps.max_single_stride_log_num_of_bytes;
		resp->mp_rq_caps.min_single_stride_log_num_of_bytes =  exp_attr->mp_rq_caps.min_single_stride_log_num_of_bytes;
		resp->mp_rq_caps.max_single_wqe_log_num_of_strides =  exp_attr->mp_rq_caps.max_single_wqe_log_num_of_strides;
		resp->mp_rq_caps.min_single_wqe_log_num_of_strides =  exp_attr->mp_rq_caps.min_single_wqe_log_num_of_strides;
		resp->comp_mask |= IB_EXP_DEVICE_ATTR_MP_RQ;
	}

	if (exp_attr->exp_comp_mask & IB_EXP_DEVICE_ATTR_VLAN_OFFLOADS) {
		resp->vlan_offloads = exp_attr->vlan_offloads;
		resp->comp_mask |= IB_EXP_DEVICE_ATTR_VLAN_OFFLOADS;
	}

	if (exp_attr->exp_comp_mask & IB_EXP_DEVICE_ATTR_RX_PAD_END_ALIGN) {
		resp->rx_pad_end_addr_align = exp_attr->rx_pad_end_addr_align;
		resp->comp_mask |= IB_EXP_DEVICE_ATTR_RX_PAD_END_ALIGN;
	}

	if (exp_attr->exp_comp_mask & IB_EXP_DEVICE_ATTR_EC_CAPS) {
		resp->ec_caps.max_ec_data_vector_count =
				exp_attr->ec_caps.max_ec_data_vector_count;
		resp->ec_caps.max_ec_calc_inflight_calcs =
				exp_attr->ec_caps.max_ec_calc_inflight_calcs;
		resp->ec_w_mask = exp_attr->ec_w_mask;
		resp->comp_mask |= IB_EXP_DEVICE_ATTR_EC_CAPS;
	}

	if (exp_attr->exp_comp_mask & IB_EXP_DEVICE_ATTR_PACKET_PACING_CAPS) {
		resp->packet_pacing_caps.qp_rate_limit_min =
			exp_attr->packet_pacing_caps.qp_rate_limit_min;
		resp->packet_pacing_caps.qp_rate_limit_max =
			exp_attr->packet_pacing_caps.qp_rate_limit_max;
		resp->packet_pacing_caps.supported_qpts =
			exp_attr->packet_pacing_caps.supported_qpts;
		resp->packet_pacing_caps.cap_flags =
			exp_attr->packet_pacing_caps.cap_flags;
		resp->comp_mask |= IB_EXP_DEVICE_ATTR_PACKET_PACING_CAPS;
	}

	if (exp_attr->exp_comp_mask & IB_EXP_DEVICE_ATTR_OOO_CAPS) {
		resp->ooo_caps.rc_caps = exp_attr->ooo_caps.rc_caps;
		resp->ooo_caps.xrc_caps = exp_attr->ooo_caps.xrc_caps;
		resp->ooo_caps.dc_caps = exp_attr->ooo_caps.dc_caps;
		resp->comp_mask |= IB_EXP_DEVICE_ATTR_OOO_CAPS;
	}

	if (exp_attr->exp_comp_mask & IB_EXP_DEVICE_ATTR_SW_PARSING_CAPS) {
		resp->sw_parsing_caps.sw_parsing_offloads =
			exp_attr->sw_parsing_caps.sw_parsing_offloads;
		resp->sw_parsing_caps.supported_qpts =
			exp_attr->sw_parsing_caps.supported_qpts;
		resp->comp_mask |= IB_EXP_DEVICE_ATTR_SW_PARSING_CAPS;
	}

	if (exp_attr->exp_comp_mask & IB_EXP_DEVICE_ATTR_ODP_MAX_SIZE) {
		resp->odp_mr_max_size = exp_attr->odp_caps.max_size;
		resp->comp_mask |= IB_EXP_DEVICE_ATTR_ODP_MAX_SIZE;
	}
	
	if (exp_attr->exp_comp_mask & IB_EXP_DEVICE_ATTR_TM_CAPS) {
		resp->tm_caps.max_rndv_hdr_size = exp_attr->tm_caps.max_rndv_hdr_size;
		resp->tm_caps.max_num_tags = exp_attr->tm_caps.max_num_tags;
		resp->tm_caps.capability_flags = exp_attr->tm_caps.capability_flags;
		resp->tm_caps.max_ops = exp_attr->tm_caps.max_ops;
		resp->tm_caps.max_sge = exp_attr->tm_caps.max_sge;
		resp->comp_mask |= IB_EXP_DEVICE_ATTR_TM_CAPS;

	}

	if (exp_attr->exp_comp_mask & IB_EXP_DEVICE_ATTR_TUNNEL_OFFLOADS_CAPS) {
		resp->tunnel_offloads_caps = exp_attr->tunnel_offloads_caps;
		resp->comp_mask |= IB_EXP_DEVICE_ATTR_TUNNEL_OFFLOADS_CAPS;
	}

	if (exp_attr->exp_comp_mask & IB_EXP_DEVICE_ATTR_MAX_DM_SIZE) {
		resp->max_dm_size = exp_attr->max_dm_size;
		resp->comp_mask |= IB_EXP_DEVICE_ATTR_MAX_DM_SIZE;
	}

	if (exp_attr->exp_comp_mask & IB_EXP_DEVICE_ATTR_TUNNELED_ATOMIC) {
		resp->tunneled_atomic_caps  = exp_attr->tunneled_atomic_caps;
		resp->comp_mask |= IB_EXP_DEVICE_ATTR_TUNNELED_ATOMIC;
	}

	if (exp_attr->exp_comp_mask & IB_EXP_DEVICE_ATTR_COMP_MASK_2) {
		if (exp_attr->exp_comp_mask_2 & IB_EXP_DEVICE_ATTR_UMR_FIXED_SIZE_CAPS) {
			resp->umr_fixed_size_caps.max_entity_size = exp_attr->umr_fixed_size_caps.max_entity_size;
			resp->comp_mask_2 |= IB_EXP_DEVICE_ATTR_UMR_FIXED_SIZE_CAPS;
		}

		if (exp_attr->exp_comp_mask_2 & IB_EXP_DEVICE_ATTR_PCI_ATOMIC_CAPS) {
			resp->pci_atomic_caps.fetch_add = exp_attr->pci_atomic_caps.fetch_add;
			resp->pci_atomic_caps.swap = exp_attr->pci_atomic_caps.swap;
			resp->pci_atomic_caps.compare_swap = exp_attr->pci_atomic_caps.compare_swap;
			resp->comp_mask_2 |= IB_EXP_DEVICE_ATTR_PCI_ATOMIC_CAPS;
		}

		resp->comp_mask |= IB_EXP_DEVICE_ATTR_COMP_MASK_2;
	}

	ret = ib_copy_to_udata(&attrs->ucore, resp, min_t(size_t, sizeof(*resp), attrs->ucore.outlen));
out:
	kfree(exp_attr);
	kfree(resp);
	return ret;
}

static u32 mr_create_flag_to_mr_type(u32 create_flags)
{
	switch (create_flags) {
	case IB_EXP_MR_SIGNATURE_EN:
		return IB_MR_TYPE_INTEGRITY;
	case IB_EXP_MR_INDIRECT_KLMS:
		return IB_MR_INDIRECT_REG;
	case IB_EXP_MR_FIXED_BUFFER_SIZE:
		return IB_MR_TYPE_FIXED_SIZE;
	default:
		return 0;
	}
}

int ib_uverbs_exp_create_mr(struct uverbs_attr_bundle *attrs)
{
	struct ib_uverbs_exp_create_mr          cmd_exp;
	struct ib_uverbs_exp_create_mr_resp     resp_exp;
	struct ib_device *ib_dev;
	struct ib_mr_init_attr			attr = {0};
	struct ib_pd                            *pd = NULL;
	struct ib_mr                            *mr = NULL;
	struct ib_uobject                       *uobj = NULL;
	int ret;

	if (attrs->ucore.outlen + attrs->driver_udata.outlen < sizeof(resp_exp))
		return -ENOSPC;

	ret = ib_copy_from_udata(&cmd_exp, &attrs->ucore, sizeof(cmd_exp));
	if (ret)
		return ret;

	uobj  = uobj_alloc(UVERBS_OBJECT_MR, attrs, &ib_dev);
	if (IS_ERR(uobj))
		return PTR_ERR(uobj);

	pd = uobj_get_obj_read(pd, UVERBS_OBJECT_PD, cmd_exp.pd_handle, attrs);
	if (!pd) {
		ret = -EINVAL;
		goto err_free;
	}

	attr.mr_type = mr_create_flag_to_mr_type(cmd_exp.create_flags);
	attr.max_num_sg = cmd_exp.max_reg_descriptors;
	mr = ib_exp_alloc_mr(pd, &attr);
	if (IS_ERR(mr)) {
		ret = PTR_ERR(mr);
		goto err_put;
	}

	mr->device  = pd->device;
	mr->pd      = pd;
	mr->uobject = uobj;

	uobj->object = mr;

	memset(&resp_exp, 0, sizeof(resp_exp));
	resp_exp.lkey = mr->lkey;
	resp_exp.rkey = mr->rkey;
	resp_exp.handle = uobj->id;

	ret = ib_copy_to_udata(&attrs->ucore, &resp_exp, sizeof(resp_exp));
	if (ret)
		goto err_copy;

	uobj_put_obj_read(pd);

	rdma_alloc_commit_uobject(uobj, attrs);
	return 0;

err_copy:
	ib_dereg_mr(mr);

err_put:
	uobj_put_obj_read(pd);

err_free:
	uobj_alloc_abort(uobj, attrs);
	return ret;
}

int ib_uverbs_exp_query_mkey(struct uverbs_attr_bundle *attrs)
{
	struct ib_uverbs_exp_query_mkey cmd_exp;
	struct ib_uverbs_exp_query_mkey_resp resp_exp;
	struct ib_mr *mr;
	struct ib_mkey_attr mkey_attr;
	int ret;

	memset(&cmd_exp, 0, sizeof(cmd_exp));
	ret = ib_copy_from_udata(&cmd_exp, &attrs->ucore, sizeof(cmd_exp));
	if (ret)
		return ret;

	mr = uobj_get_obj_read(mr, UVERBS_OBJECT_MR, cmd_exp.handle, attrs);
	if (!mr)
		return -EINVAL;

	ret = ib_exp_query_mkey(mr, 0, &mkey_attr);
	if (ret) {
		uobj_put_obj_read(mr);
		return ret;
	}

	uobj_put_obj_read(mr);

	memset(&resp_exp, 0, sizeof(resp_exp));
	resp_exp.max_reg_descriptors = mkey_attr.max_reg_descriptors;

	ret = ib_copy_to_udata(&attrs->ucore, &resp_exp, sizeof(resp_exp));
	if (ret)
		return ret;

	return 0;
}
EXPORT_SYMBOL(ib_uverbs_exp_query_mkey);

enum ib_uverbs_cq_exp_create_flags {
	IB_UVERBS_CQ_EXP_TIMESTAMP	= 1 << 1,
};

static u32 create_cq_exp_flags_to_ex_flags(__u64 create_flags)
{
	switch (create_flags) {
	case IB_UVERBS_CQ_EXP_TIMESTAMP:
		return IB_UVERBS_CQ_FLAGS_TIMESTAMP_COMPLETION;
	default:
		return 0;
	}
}

#define KEEP_ACCESS_FLAGS (IB_ACCESS_LOCAL_WRITE | IB_ACCESS_REMOTE_WRITE | \
			   IB_ACCESS_REMOTE_READ | IB_ACCESS_REMOTE_ATOMIC | \
			   IB_ACCESS_MW_BIND | IB_ZERO_BASED | \
			   IB_ACCESS_ON_DEMAND)
static int translate_exp_access_flags(u64 exp_access_flags)
{
	int access_flags = exp_access_flags & KEEP_ACCESS_FLAGS;

	if (exp_access_flags & IB_UVERBS_EXP_ACCESS_ON_DEMAND)
		access_flags |= IB_ACCESS_ON_DEMAND;
	if (exp_access_flags & IB_UVERBS_EXP_ACCESS_PHYSICAL_ADDR)
		access_flags |= IB_EXP_ACCESS_PHYSICAL_ADDR;
	if (exp_access_flags & IB_UVERBS_EXP_ACCESS_TUNNELED_ATOMIC)
		access_flags |= IB_EXP_ACCESS_TUNNELED_ATOMIC;
	if (exp_access_flags & IB_UVERBS_EXP_ACCESS_RELAXED_ORDERING)
		access_flags |= IB_ACCESS_RELAXED_ORDERING;

	return access_flags;
}

int ib_uverbs_exp_reg_mr(struct uverbs_attr_bundle *attrs)
{
	struct ib_uverbs_exp_reg_mr cmd;
	struct ib_uverbs_exp_reg_mr_resp resp;
	struct ib_mr_init_attr attr = {0};
	struct ib_uobject *uobj;
	struct ib_device *ib_dev;
	struct ib_pd      *pd;
	struct ib_mr      *mr;
	struct ib_dm	  *dm = NULL;
	int access_flags;
	int                ret;
	const int min_cmd_size = offsetof(typeof(cmd), comp_mask) +
					  sizeof(cmd.comp_mask);

	if (attrs->ucore.inlen < min_cmd_size) {
		pr_debug("%s: command input length too short\n", __func__);
		return -EINVAL;
	}

	ret = ib_copy_from_udata(&cmd, &attrs->ucore, sizeof(cmd));
	if (ret)
		return ret;

	if (cmd.comp_mask >= IB_UVERBS_EXP_REG_MR_EX_RESERVED) {
		pr_debug("%s: invalid bit in command comp_mask field\n",
			 __func__);
		return -EINVAL;
	}

	if ((cmd.start & ~PAGE_MASK) != (cmd.hca_va & ~PAGE_MASK)) {
		pr_debug("%s: HCA virtual address doesn't match host address\n",
			 __func__);
		return -EINVAL;
	}

	access_flags = translate_exp_access_flags(cmd.exp_access_flags);

	ret = ib_check_mr_access(access_flags);
	if (ret)
		return ret;

	uobj  = uobj_alloc(UVERBS_OBJECT_MR, attrs, &ib_dev);
	if (IS_ERR(uobj))
		return PTR_ERR(uobj);

	pd = uobj_get_obj_read(pd, UVERBS_OBJECT_PD, cmd.pd_handle, attrs);
	if (!pd) {
		pr_debug("ib_uverbs_reg_mr: invalid PD\n");
		ret = -EINVAL;
		goto err_free;
	}

	if (attrs->ucore.inlen >= offsetof(typeof(cmd), dm_handle) +
		      sizeof(cmd.dm_handle) &&
		      (cmd.comp_mask & IB_UVERBS_EXP_REG_MR_EX_DM_HANDLE)) {
		dm = uobj_get_obj_read(dm, UVERBS_OBJECT_DM, cmd.dm_handle, attrs);
		if (!dm) {
			ret = -EINVAL;
			goto err_put;
		}

		if (cmd.start > dm->length ||
		    cmd.length > dm->length - cmd.start) {
			ret = -EFAULT;
			goto err_put;
		}

		attr.mr_type = IB_MR_TYPE_DM;
	}

	if (cmd.exp_access_flags & IB_UVERBS_EXP_ACCESS_ON_DEMAND) {
#ifdef CONFIG_INFINIBAND_ON_DEMAND_PAGING
		struct ib_exp_device_attr exp_attr;

		ret = ib_exp_query_device(pd->device, &exp_attr, &attrs->driver_udata);
		if (ret || !(exp_attr.device_cap_flags2 &
			     IB_EXP_DEVICE_ODP)) {
			pr_debug("ib_uverbs_reg_mr: ODP requested on device without ODP support\n");
			ret = -EINVAL;
			goto err_put;
		}
#else
		pr_debug("ib_uverbs_reg_mr: ODP requested but the RDMA subsystem was compiled without ODP support\n");
		ret = -EINVAL;
		goto err_put;
#endif
	}

	attr.start = cmd.start;
	attr.length = cmd.length;
	attr.hca_va = cmd.hca_va;
	attr.access_flags = access_flags;
	attr.dm = dm;

	mr = pd->device->ops.reg_user_mr(pd, &attr, &attrs->driver_udata);
	if (IS_ERR(mr)) {
		ret = PTR_ERR(mr);
		goto err_put;
	}

	mr->device  = pd->device;
	mr->pd      = pd;
	mr->dm	    = dm;
	mr->uobject = uobj;
	atomic_inc(&pd->usecnt);

	uobj->object = mr;

	memset(&resp, 0, sizeof(resp));
	resp.lkey      = mr->lkey;
	resp.rkey      = mr->rkey;
	resp.mr_handle = uobj->id;

	ret = ib_copy_to_udata(&attrs->ucore, &resp, sizeof(resp));
	if (ret)
		goto err_copy;

	if (dm) {
		atomic_inc(&dm->usecnt);
		uobj_put_obj_read(dm);
	}

	uobj_put_obj_read(pd);

	rdma_alloc_commit_uobject(uobj, attrs);
	return 0;

err_copy:
	ib_dereg_mr(mr);

err_put:
	uobj_put_obj_read(pd);
	if (dm)
		uobj_put_obj_read(dm);


err_free:
	uobj_alloc_abort(uobj, attrs);
	return ret;
}

#ifdef CONFIG_INFINIBAND_ON_DEMAND_PAGING
int ib_uverbs_exp_prefetch_mr(struct uverbs_attr_bundle *attrs)
{
	struct ib_uverbs_exp_prefetch_mr  cmd;
	struct ib_mr                     *mr;
	int                               ret = -EINVAL;

	if (attrs->ucore.inlen < sizeof(cmd))
		return -EINVAL;

	ret = ib_copy_from_udata(&cmd, &attrs->ucore, sizeof(cmd));
	if (ret)
		return ret;

	attrs->ucore.inbuf += sizeof(cmd);
	attrs->ucore.inlen -= sizeof(cmd);

	if (cmd.comp_mask)
		return -EINVAL;

	mr = uobj_get_obj_read(mr, UVERBS_OBJECT_MR, cmd.mr_handle, attrs);
	if (!mr)
		return -EINVAL;

	if (!mr->device->ops.exp_prefetch_mr) {
		ret = -ENOSYS;
		goto out;
	}

	ret = mr->device->ops.exp_prefetch_mr(mr, cmd.start, cmd.length, cmd.flags);
	if (ret)
		goto out;

	ib_umem_odp_account_prefetch_handled(mr->device);

out:
	uobj_put_read(mr->uobject);
	return ret;
}

#endif /* CONFIG_INFINIBAND_ON_DEMAND_PAGING */

int ib_uverbs_exp_create_cq(struct uverbs_attr_bundle *attrs)
{
	int out_len = attrs->ucore.outlen + attrs->driver_udata.outlen;
	struct ib_uverbs_exp_create_cq cmd_exp;
	struct ib_uverbs_ex_create_cq	cmd_ex;
	struct ib_uverbs_create_cq_resp resp;
	int ret;
	struct ib_ucq_object           *obj;

	if (out_len < sizeof(resp))
		return -ENOSPC;

	ret = ib_copy_from_udata(&cmd_exp, &attrs->ucore, sizeof(cmd_exp));
	if (ret)
		return ret;

	if (cmd_exp.comp_mask >= IB_UVERBS_EXP_CREATE_CQ_ATTR_RESERVED)
		return -ENOSYS;

	if (cmd_exp.comp_mask & IB_UVERBS_EXP_CREATE_CQ_CAP_FLAGS &&
	   /* Check that there is no bit that is not supported */
	    cmd_exp.create_flags & ~(IB_UVERBS_CQ_EXP_TIMESTAMP))
		return -EINVAL;

	memset(&cmd_ex, 0, sizeof(cmd_ex));
	cmd_ex.user_handle = cmd_exp.user_handle;
	cmd_ex.cqe = cmd_exp.cqe;
	cmd_ex.comp_vector = cmd_exp.comp_vector;
	cmd_ex.comp_channel = cmd_exp.comp_channel;
	cmd_ex.flags = create_cq_exp_flags_to_ex_flags(cmd_exp.create_flags);

	obj = create_cq(attrs, &cmd_ex);

	if (IS_ERR(obj))
		return PTR_ERR(obj);

	return 0;
}

static void set_dc_ini_ah_fields(struct ib_device *device, struct rdma_ah_attr *ah)
{
	ah->static_rate = IB_RATE_PORT_CURRENT;
	ah->port_num = rdma_start_port(device);
}

int ib_uverbs_exp_modify_qp(struct uverbs_attr_bundle *attrs)
{
	struct ib_uverbs_exp_modify_qp	cmd = {};
	int ret;
	struct ib_qp		       *qp;
	struct ib_qp_attr	       *attr;
	u32				exp_mask;

	if (attrs->ucore.inlen < offsetof(typeof(cmd), comp_mask) +  sizeof(cmd.comp_mask))
		return -EINVAL;

	ret = ib_copy_from_udata(&cmd, &attrs->ucore, min(sizeof(cmd), attrs->ucore.inlen));
	if (ret)
		return ret;

	if (cmd.comp_mask >= IB_UVERBS_EXP_QP_ATTR_RESERVED)
		return -ENOSYS;

	/* Verify that upper & lower 32 bits from user can fit into qp_attr_mask which is 32 bits
	 * and there is no overflow.
	*/
	if ((cmd.exp_attr_mask << IBV_EXP_ATTR_MASK_SHIFT >= 1ULL << 32) ||
	    (cmd.attr_mask >= IBV_EXP_QP_ATTR_FIRST))
		return -EINVAL;

	attr = kzalloc(sizeof(*attr), GFP_KERNEL);
	if (!attr)
		return -ENOMEM;

	qp = uobj_get_obj_read(qp, UVERBS_OBJECT_QP, cmd.qp_handle,
			       attrs);
	if (!qp) {
		kfree(attr);
		return -EINVAL;
	}

	attr->qp_state            = cmd.qp_state;
	attr->cur_qp_state        = cmd.cur_qp_state;
	attr->path_mtu            = cmd.path_mtu;
	attr->path_mig_state      = cmd.path_mig_state;
	attr->qkey                = cmd.qkey;
	attr->rq_psn              = cmd.rq_psn & 0xffffff;
	attr->sq_psn              = cmd.sq_psn & 0xffffff;
	attr->dest_qp_num         = cmd.dest_qp_num;
	attr->qp_access_flags     = cmd.qp_access_flags;
	attr->pkey_index          = cmd.pkey_index;
	attr->alt_pkey_index      = cmd.alt_pkey_index;
	attr->en_sqd_async_notify = cmd.en_sqd_async_notify;
	attr->max_rd_atomic       = cmd.max_rd_atomic;
	attr->max_dest_rd_atomic  = cmd.max_dest_rd_atomic;
	attr->min_rnr_timer       = cmd.min_rnr_timer;
	attr->port_num            = cmd.port_num;
	attr->timeout             = cmd.timeout;
	attr->retry_cnt           = cmd.retry_cnt;
	attr->rnr_retry           = cmd.rnr_retry;
	attr->alt_port_num        = cmd.alt_port_num;
	attr->alt_timeout         = cmd.alt_timeout;
	attr->dct_key             = cmd.dct_key;
	attr->rate_limit	  = cmd.rate_limit;
	if (cmd.comp_mask & IB_UVERBS_EXP_QP_ATTR_BURST_INFO) {
		if (!attr->rate_limit){
			ret = -EINVAL;
			goto out;
		}

		if (offsetof(typeof(cmd), burst_info) + sizeof(cmd.burst_info) <=
		    attrs->ucore.inlen) {
			attr->burst_info.max_burst_sz = cmd.burst_info.max_burst_sz;
			attr->burst_info.typical_pkt_sz = cmd.burst_info.typical_pkt_sz;
			if (cmd.burst_info.reserved){
				ret = -EINVAL;
				goto out;
			}
		} else {
			ret = -EINVAL;
			goto out;
		}
	}

	if (cmd.attr_mask & IB_QP_AV) {
		copy_ah_attr_from_uverbs(qp, &attr->ah_attr, &cmd.dest);
		/* When modifying a DC INI from INIT to RTR, only the SL is valid.
		 * Since old applications set zeros or invalid values for the rest
		 * of the fields, we might fail down the road in rdma_check_ah_attr.
		 * Change those values to avoid failures and backward compatibility issues.
		 */
		if (qp->qp_type == IB_EXP_QPT_DC_INI) {
		  //printk(KERN_INFO "[uverbs_cmd_exp] in dct modifyt AV");
		  set_dc_ini_ah_fields(qp->device, &attr->ah_attr);
		}
	}
	if (cmd.attr_mask & IB_QP_ALT_PATH)
		copy_ah_attr_from_uverbs(qp, &attr->alt_ah_attr,
					 &cmd.alt_dest);

	if (cmd.comp_mask & IB_UVERBS_EXP_QP_ATTR_FLOW_ENTROPY) {
		if (offsetof(typeof(cmd), flow_entropy) + sizeof(cmd.flow_entropy) <= attrs->ucore.inlen) {
			attr->flow_entropy = cmd.flow_entropy;
		} else {
			ret = -EINVAL;
			goto out;
		}
	}

	if ((cmd.exp_attr_mask << IBV_EXP_ATTR_MASK_SHIFT) &
	    IB_EXP_QP_RATE_LIMIT)
		cmd.attr_mask |= IB_QP_RATE_LIMIT;

	exp_mask = (cmd.exp_attr_mask << IBV_EXP_ATTR_MASK_SHIFT) & IBV_EXP_QP_ATTR_MASK;

	ret = ib_modify_qp_with_udata(qp, attr,
				      modify_qp_mask(qp->qp_type, cmd.attr_mask | exp_mask),
				      &attrs->driver_udata);

	if (!ret && cmd.attr_mask & IB_QP_PORT)
		qp->port = attr->port_num;

out:
	rdma_lookup_put_uobject(&qp->uobject->uevent.uobject, UVERBS_LOOKUP_READ);
	kfree(attr);

	return ret;
}

int ib_uverbs_exp_create_dct(struct uverbs_attr_bundle *attrs)
{
	int out_len			= attrs->ucore.outlen + attrs->driver_udata.outlen;
	struct ib_uverbs_create_dct	 *cmd;
	struct ib_uverbs_create_dct_resp resp;
	struct ib_udct_object		*obj;
	struct ib_dct			*dct;
	int                             ret;
	struct ib_dct_init_attr		*attr;
	struct ib_pd			*pd = NULL;
	struct ib_cq			*cq = NULL;
	struct ib_srq			*srq = NULL;
	struct ib_device *ib_dev;

	if (out_len < sizeof(resp))
		return -ENOSPC;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	attr = kzalloc(sizeof(*attr), GFP_KERNEL);
	if (!attr || !cmd) {
		ret = -ENOMEM;
		goto err_cmd_attr;
	}

	ret = ib_copy_from_udata(cmd, &attrs->ucore, sizeof(*cmd));
	if (ret)
		goto err_cmd_attr;

	obj  = (struct ib_udct_object *)uobj_alloc(UVERBS_OBJECT_DCT, attrs, &ib_dev);
	if (IS_ERR(obj))
		return PTR_ERR(obj);

	pd = uobj_get_obj_read(pd, UVERBS_OBJECT_PD, cmd->pd_handle, attrs);
	if (!pd) {
		ret = -EINVAL;
		goto err_pd;
	}

	cq = uobj_get_obj_read(cq, UVERBS_OBJECT_CQ, cmd->cq_handle, attrs);
	if (!cq) {
		ret = -EINVAL;
		goto err_put;
	}

	srq = uobj_get_obj_read(srq, UVERBS_OBJECT_SRQ, cmd->srq_handle, attrs);
	if (!srq) {
		ret = -EINVAL;
		goto err_put;
	}

	if (cmd->create_flags & ~IB_DCT_CREATE_FLAGS_MASK) {
		ret = -EINVAL;
		goto err_put;
	}

	attr->cq = cq;
	attr->access_flags = cmd->access_flags;
	attr->min_rnr_timer = cmd->min_rnr_timer;
	attr->srq = srq;
	attr->tclass = cmd->tclass;
	attr->flow_label = cmd->flow_label;
	attr->dc_key = cmd->dc_key;
	attr->mtu = cmd->mtu;
	attr->port = cmd->port;
	attr->pkey_index = cmd->pkey_index;
	attr->gid_index = cmd->gid_index;
	attr->hop_limit = cmd->hop_limit;
	attr->create_flags = cmd->create_flags;
	attr->inline_size = cmd->inline_size;
	attr->event_handler = ib_uverbs_dct_event_handler;
	attr->dct_context   = attrs->ufile;

	obj->uevent.uobject.user_handle = cmd->user_handle;
	obj->uevent.events_reported = 0;
	INIT_LIST_HEAD(&obj->uevent.event_list);
	dct = ib_exp_create_dct(pd, attr, &attrs->driver_udata);
	if (IS_ERR(dct)) {
		ret = PTR_ERR(dct);
		goto err_put;
	}

	dct->device        = ib_dev;
	dct->uobject       = obj;
	dct->event_handler = attr->event_handler;
	dct->dct_context   = attr->dct_context;

	obj->uevent.uobject.object = dct;

	memset(&resp, 0, sizeof(resp));
	resp.dct_handle = obj->uevent.uobject.id;
	resp.dctn = dct->dct_num;
	resp.inline_size = attr->inline_size;

	ret = ib_copy_to_udata(&attrs->ucore, &resp, sizeof(resp));
	if (ret)
		goto err_copy;

	rdma_lookup_put_uobject(&srq->uobject->uevent.uobject,
				UVERBS_LOOKUP_READ);
	rdma_lookup_put_uobject(&cq->uobject->uevent.uobject,
				UVERBS_LOOKUP_READ);
	uobj_put_obj_read(pd);

	kfree(attr);
	kfree(cmd);

	rdma_alloc_commit_uobject(&obj->uevent.uobject, attrs);
	return 0;

err_copy:
	ib_exp_destroy_dct(dct, &attrs->driver_udata);

err_put:
	if (srq)
		rdma_lookup_put_uobject(&srq->uobject->uevent.uobject,
					UVERBS_LOOKUP_READ);

	if (cq)
		rdma_lookup_put_uobject(&cq->uobject->uevent.uobject,
					UVERBS_LOOKUP_READ);

	uobj_put_obj_read(pd);

err_pd:
	uobj_alloc_abort(&obj->uevent.uobject, attrs);

err_cmd_attr:
	kfree(attr);
	kfree(cmd);
	return ret;
}

int ib_uverbs_exp_destroy_dct(struct uverbs_attr_bundle *attrs)
{
	int out_len				= attrs->ucore.outlen + attrs->driver_udata.outlen;
	struct ib_uverbs_destroy_dct		cmd;
	struct ib_uverbs_destroy_dct_resp	resp;
	struct ib_uobject		       *uobj;
	struct ib_udct_object		       *obj;
	int					ret;

	if (out_len < sizeof(resp))
		return -ENOSPC;

	ret = ib_copy_from_udata(&cmd, &attrs->ucore, sizeof(cmd));
	if (ret)
		return ret;

	uobj = uobj_get_destroy(UVERBS_OBJECT_DCT, cmd.dct_handle, attrs);
	if (IS_ERR(uobj))
		return PTR_ERR(uobj);

	obj = container_of(uobj, struct ib_udct_object, uevent.uobject);

	resp.events_reported = obj->uevent.events_reported;
	uobj_put_destroy(uobj);

	ret = ib_copy_to_udata(&attrs->ucore, &resp, sizeof(resp));
	if (ret)
		return ret;

	return 0;
}

int ib_uverbs_exp_arm_dct(struct uverbs_attr_bundle *attrs)
{
	int out_len			= attrs->ucore.outlen + attrs->driver_udata.outlen;
	struct ib_uverbs_arm_dct	cmd;
	struct ib_uverbs_arm_dct_resp	resp;
	struct ib_dct		       *dct;
	int				err;

	if (out_len < sizeof(resp))
		return -ENOSPC;

	err = ib_copy_from_udata(&cmd, &attrs->ucore, sizeof(cmd));
	if (err)
		return err;

	dct = uobj_get_obj_read(dct, UVERBS_OBJECT_DCT, cmd.dct_handle, attrs);
	if (!dct)
		return -EINVAL;

	err = dct->device->ops.exp_arm_dct(dct, &attrs->driver_udata);
	rdma_lookup_put_uobject(&dct->uobject->uevent.uobject,
				UVERBS_LOOKUP_READ);
	if (err)
		return err;

	memset(&resp, 0, sizeof(resp));
	err = ib_copy_to_udata(&attrs->ucore, &resp, sizeof(resp));

	return err;
}

int ib_uverbs_exp_query_dct(struct uverbs_attr_bundle *attrs)
{
	int out_len			= attrs->ucore.outlen + attrs->driver_udata.outlen;
	struct ib_uverbs_query_dct	cmd;
	struct ib_uverbs_query_dct_resp	resp;
	struct ib_dct		       *dct;
	struct ib_dct_attr	       *attr;
	int				err;

	if (out_len < sizeof(resp))
		return -ENOSPC;

	err = ib_copy_from_udata(&cmd, &attrs->ucore, sizeof(cmd));
	if (err)
		return err;

	attr = kmalloc(sizeof(*attr), GFP_KERNEL);
	if (!attr) {
		err = -ENOMEM;
		goto out;
	}

	dct = uobj_get_obj_read(dct, UVERBS_OBJECT_DCT, cmd.dct_handle, attrs);
	if (!dct) {
		err = -EINVAL;
		goto out;
	}

	err = ib_exp_query_dct(dct, attr);

	rdma_lookup_put_uobject(&dct->uobject->uevent.uobject,
				UVERBS_LOOKUP_READ);

	if (err)
		goto out;

	memset(&resp, 0, sizeof(resp));

	resp.dc_key = attr->dc_key;
	resp.access_flags = attr->access_flags;
	resp.flow_label = attr->flow_label;
	resp.key_violations = attr->key_violations;
	resp.port = attr->port;
	resp.min_rnr_timer = attr->min_rnr_timer;
	resp.tclass = attr->tclass;
	resp.mtu = attr->mtu;
	resp.pkey_index = attr->pkey_index;
	resp.gid_index = attr->gid_index;
	resp.hop_limit = attr->hop_limit;
	resp.state = attr->state;

	err = ib_copy_to_udata(&attrs->ucore, &resp, sizeof(resp));

out:
	kfree(attr);

	return err;
}

int ib_uverbs_exp_create_wq(struct uverbs_attr_bundle *attrs)
{
	return ib_uverbs_ex_create_wq(attrs);
}

int ib_uverbs_exp_modify_wq(struct uverbs_attr_bundle *attrs)
{
	return ib_uverbs_ex_modify_wq(attrs);
}

int ib_uverbs_exp_destroy_wq(struct uverbs_attr_bundle *attrs)
{
	return ib_uverbs_ex_destroy_wq(attrs);
}

int ib_uverbs_exp_create_rwq_ind_table(struct uverbs_attr_bundle *attrs)
{
	return ib_uverbs_ex_create_rwq_ind_table(attrs);
}

int ib_uverbs_exp_destroy_rwq_ind_table(struct uverbs_attr_bundle *attrs)
{
	return ib_uverbs_ex_destroy_rwq_ind_table(attrs);
}


int ib_uverbs_exp_set_context_attr(struct uverbs_attr_bundle *attrs)
{
	struct ib_uverbs_exp_set_context_attr cmd = {};
	struct ib_device *device;
	struct ib_exp_context_attr attr = {};
	size_t required_cmd_sz;
	struct ib_ucontext *ucontext;
	int ret;

	ucontext = ib_uverbs_get_ucontext(attrs);
	if (IS_ERR(ucontext))
		return PTR_ERR(ucontext);

	device = ucontext->device;
	required_cmd_sz = offsetof(typeof(cmd), peer_name) + sizeof(cmd.peer_name);

	if (attrs->ucore.inlen < required_cmd_sz)
		return -EINVAL;

	if (attrs->ucore.inlen > sizeof(cmd) &&
	    !ib_is_udata_cleared(&attrs->ucore, sizeof(cmd),
				 attrs->ucore.inlen - sizeof(cmd)))
		return -EOPNOTSUPP;

	ret = ib_copy_from_udata(&cmd, &attrs->ucore, min(sizeof(cmd), attrs->ucore.inlen));
	if (ret)
		return ret;

	if (!cmd.comp_mask)
		return -EINVAL;

	if (cmd.comp_mask >= IB_UVERBS_EXP_SET_CONTEXT_ATTR_RESERVED)
		return -ENOSYS;

	attr.comp_mask = cmd.comp_mask;
	if (attr.comp_mask & IB_UVERBS_EXP_SET_CONTEXT_PEER_INFO) {
		attr.peer_id = cmd.peer_id;
		attr.peer_name = cmd.peer_name;
	}

	ret = device->ops.exp_set_context_attr(device, ucontext, &attr);

	return ret;
}

int ib_uverbs_exp_create_srq(struct uverbs_attr_bundle *attrs)
{
	struct ib_uverbs_create_xsrq xcmd = {};
	struct ib_uverbs_exp_create_srq cmd;
	size_t required_cmd_sz;
	int err;

	required_cmd_sz = offsetof(typeof(cmd), reserved) +
			  sizeof(cmd.reserved);

	if (attrs->ucore.inlen < required_cmd_sz)
		return -EINVAL;

	if (attrs->ucore.inlen > sizeof(cmd) &&
	    !ib_is_udata_cleared(&attrs->ucore, sizeof(cmd),
				 attrs->ucore.inlen - sizeof(cmd)))
		return -EOPNOTSUPP;

	if (attrs->ucore.outlen < sizeof(struct ib_uverbs_create_srq_resp))
		return -ENOSPC;

	err = ib_copy_from_udata(&cmd, &attrs->ucore, min(sizeof(cmd), attrs->ucore.inlen));
	if (err)
		return err;

	if (cmd.comp_mask || cmd.reserved)
		return -EINVAL;

	xcmd.response    = (u64)&attrs->ucore;
	xcmd.user_handle = cmd.user_handle;
	xcmd.srq_type	 = cmd.srq_type;
	xcmd.pd_handle	 = cmd.pd_handle;
	xcmd.max_wr	 = cmd.max_wr;
	xcmd.max_sge	 = cmd.max_sge;
	xcmd.srq_limit	 = cmd.srq_limit;
	xcmd.cq_handle   = cmd.cq_handle;
	xcmd.xrcd_handle = cmd.xrcd_handle;

	return __uverbs_create_xsrq(attrs,  &xcmd, &attrs->driver_udata);
}

int ib_uverbs_exp_create_srq_resp(struct ib_uverbs_create_srq_resp *resp,
				  u64 response)
{
	struct ib_udata *ucore = (struct ib_udata *)response;
	struct ib_uverbs_exp_create_srq_resp resp_exp;

	resp_exp.base = *resp;
	resp_exp.comp_mask = 0;
	resp_exp.response_length = min(ucore->outlen, sizeof(resp_exp));

	return ib_copy_to_udata(ucore, &resp_exp, resp_exp.response_length);
}

int ib_uverbs_exp_alloc_dm(struct uverbs_attr_bundle *attrs)
{
	int out_len = attrs->ucore.outlen + attrs->driver_udata.outlen;
	struct ib_uverbs_exp_alloc_dm      cmd;
	struct ib_device            *ib_dev;
	struct ib_uverbs_exp_alloc_dm_resp resp;
	struct ib_uobject *uobj;
	struct ib_dm *dm;
	int ret;

	if (attrs->ucore.inlen < sizeof(cmd))
		return -EINVAL;

	if (out_len < sizeof(resp))
		return -ENOSPC;

	ret = ib_copy_from_udata(&cmd, &attrs->ucore, sizeof(cmd));
	if (ret)
		return ret;

	if (cmd.uaddr & ~PAGE_MASK)
		return -EINVAL;

	if (cmd.comp_mask)
		return -EINVAL;

	uobj = uobj_alloc(UVERBS_OBJECT_DM, attrs, &ib_dev);
	if (IS_ERR(uobj))
		return PTR_ERR(uobj);

	dm = ib_dev->ops.exp_alloc_dm(ib_dev, uobj->context,
				  cmd.length, cmd.uaddr, &attrs->driver_udata);
	if (IS_ERR(dm)) {
		ret = PTR_ERR(dm);
		goto err_alloc;
	}

	dm->device  = ib_dev;
	dm->length  = cmd.length;
	dm->uobject = uobj;
	atomic_set(&dm->usecnt, 0);

	uobj->object = dm;

	memset(&resp, 0, sizeof(resp));
	resp.start_offset = dm->dev_addr & ~PAGE_MASK;
	resp.dm_handle = uobj->id;
	resp.response_length = sizeof(resp);

	ret = ib_copy_to_udata(&attrs->ucore, &resp, sizeof(resp));
	if (ret)
		goto err_copy;
	rdma_alloc_commit_uobject(uobj, attrs);
	return 0;
err_copy:
	ib_exp_free_dm(dm, attrs);

err_alloc:
	uobj_alloc_abort(uobj, attrs);

	return ret;
}

int ib_uverbs_exp_free_dm(struct uverbs_attr_bundle *attrs)
{
	struct ib_uverbs_exp_free_dm  cmd;
	int    ret;

	if (attrs->ucore.inlen < sizeof(cmd))
		return -EINVAL;

	ret = ib_copy_from_udata(&cmd, &attrs->ucore, sizeof(cmd));
	if (ret)
		return ret;

	return uobj_perform_destroy(UVERBS_OBJECT_DM, cmd.dm_handle, attrs);
}
