/*
 * Copyright (c) 2015 Mellanox Technologies Ltd.  All rights reserved.
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

#include <linux/errno.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/string.h>
#include <linux/slab.h>

#include <rdma/ib_verbs.h>
#include <rdma/ib_cache.h>
#include <rdma/ib_addr.h>

#include "core_priv.h"

struct ib_dct *ib_exp_create_dct(struct ib_pd *pd, struct ib_dct_init_attr *attr,
				 struct ib_udata *udata)
{
	struct ib_dct *dct;

	if (!pd->device->ops.exp_create_dct)
		return ERR_PTR(-ENOSYS);

	dct = pd->device->ops.exp_create_dct(pd, attr, udata);
	if (!IS_ERR(dct)) {
		dct->pd = pd;
		dct->srq = attr->srq;
		dct->cq = attr->cq;
		atomic_inc(&dct->srq->usecnt);
		atomic_inc(&dct->cq->usecnt);
		atomic_inc(&dct->pd->usecnt);
	}

	return dct;
}
EXPORT_SYMBOL(ib_exp_create_dct);

int ib_exp_destroy_dct(struct ib_dct *dct,  struct ib_udata *udata)
{
	struct ib_srq *srq;
	struct ib_cq *cq;
	struct ib_pd *pd;
	int err;

	if (!dct->device->ops.exp_destroy_dct)
		return -ENOSYS;

	srq = dct->srq;
	cq = dct->cq;
	pd = dct->pd;
	err = dct->device->ops.exp_destroy_dct(dct, udata);
	if (!err) {
		atomic_dec(&srq->usecnt);
		atomic_dec(&cq->usecnt);
		atomic_dec(&pd->usecnt);
	}

	return err;
}
EXPORT_SYMBOL(ib_exp_destroy_dct);

int ib_exp_query_dct(struct ib_dct *dct, struct ib_dct_attr *attr)
{
	if (!dct->device->ops.exp_query_dct)
		return -ENOSYS;

	return dct->device->ops.exp_query_dct(dct, attr);
}
EXPORT_SYMBOL(ib_exp_query_dct);

int ib_exp_arm_dct(struct ib_dct *dct)
{
	if (!dct->device->ops.exp_arm_dct)
		return -ENOSYS;

	return dct->device->ops.exp_arm_dct(dct, NULL);
}
EXPORT_SYMBOL(ib_exp_arm_dct);

int ib_exp_modify_cq(struct ib_cq *cq,
		 struct ib_cq_attr *cq_attr,
		 int cq_attr_mask)
{
	return cq->device->ops.exp_modify_cq ?
		cq->device->ops.exp_modify_cq(cq, cq_attr, cq_attr_mask) : -ENOSYS;
}
EXPORT_SYMBOL(ib_exp_modify_cq);

int ib_exp_query_device(struct ib_device *device,
			struct ib_exp_device_attr *device_attr,
			struct ib_udata *uhw)
{
	return device->ops.exp_query_device(device, device_attr, uhw);
}
EXPORT_SYMBOL(ib_exp_query_device);

int ib_exp_query_mkey(struct ib_mr *mr, u64 mkey_attr_mask,
		  struct ib_mkey_attr *mkey_attr)
{
	return mr->device->ops.exp_query_mkey ?
		mr->device->ops.exp_query_mkey(mr, mkey_attr_mask, mkey_attr) : -ENOSYS;
}
EXPORT_SYMBOL(ib_exp_query_mkey);

struct ib_mr *ib_get_dma_mr(struct ib_pd *pd, int mr_access_flags)
{
	struct ib_mr *mr;
	int err;

	err = ib_check_mr_access(mr_access_flags);
	if (err)
		return ERR_PTR(err);

	mr = pd->device->ops.get_dma_mr(pd, mr_access_flags);

	if (!IS_ERR(mr)) {
		mr->device  = pd->device;
		mr->pd      = pd;
		mr->uobject = NULL;
		atomic_inc(&pd->usecnt);
		mr->need_inval = false;
	}

	return mr;
}
EXPORT_SYMBOL(ib_get_dma_mr);

/* NVMEoF target offload */
int ib_query_nvmf_ns(struct ib_nvmf_ns *ns, struct ib_nvmf_ns_attr *ns_attr)
{
	return ns->ctrl->srq->device->ops.query_nvmf_ns ?
		ns->ctrl->srq->device->ops.query_nvmf_ns(ns, ns_attr) : -ENOSYS;
}
EXPORT_SYMBOL(ib_query_nvmf_ns);

struct ib_nvmf_ctrl *ib_create_nvmf_backend_ctrl(struct ib_srq *srq,
			struct ib_nvmf_backend_ctrl_init_attr *init_attr)
{
	struct ib_nvmf_ctrl *ctrl;

	if (!srq->device->ops.create_nvmf_backend_ctrl)
		return ERR_PTR(-ENOSYS);
	if (srq->srq_type != IB_EXP_SRQT_NVMF)
		return ERR_PTR(-EINVAL);

	ctrl = srq->device->ops.create_nvmf_backend_ctrl(srq, init_attr);
	if (!IS_ERR(ctrl)) {
		atomic_set(&ctrl->usecnt, 0);
		ctrl->srq = srq;
		ctrl->event_handler = init_attr->event_handler;
		ctrl->be_context = init_attr->be_context;
		atomic_inc(&srq->usecnt);
	}

	return ctrl;
}
EXPORT_SYMBOL_GPL(ib_create_nvmf_backend_ctrl);

int ib_destroy_nvmf_backend_ctrl(struct ib_nvmf_ctrl *ctrl)
{
	struct ib_srq *srq = ctrl->srq;
	int ret;

	if (atomic_read(&ctrl->usecnt))
		return -EBUSY;

	ret = srq->device->ops.destroy_nvmf_backend_ctrl(ctrl);
	if (!ret)
		atomic_dec(&srq->usecnt);

	return ret;
}
EXPORT_SYMBOL_GPL(ib_destroy_nvmf_backend_ctrl);

struct ib_nvmf_ns *ib_attach_nvmf_ns(struct ib_nvmf_ctrl *ctrl,
			struct ib_nvmf_ns_init_attr *init_attr)
{
	struct ib_srq *srq = ctrl->srq;
	struct ib_nvmf_ns *ns;

	if (!srq->device->ops.attach_nvmf_ns)
		return ERR_PTR(-ENOSYS);
	if (srq->srq_type != IB_EXP_SRQT_NVMF)
		return ERR_PTR(-EINVAL);

	ns = srq->device->ops.attach_nvmf_ns(ctrl, init_attr);
	if (!IS_ERR(ns)) {
		ns->ctrl   = ctrl;
		atomic_inc(&ctrl->usecnt);
	}

	return ns;
}
EXPORT_SYMBOL_GPL(ib_attach_nvmf_ns);

int ib_detach_nvmf_ns(struct ib_nvmf_ns *ns)
{
	struct ib_nvmf_ctrl *ctrl = ns->ctrl;
	struct ib_srq *srq = ctrl->srq;
	int ret;

	ret = srq->device->ops.detach_nvmf_ns(ns);
	if (!ret)
		atomic_dec(&ctrl->usecnt);

	return ret;
}
EXPORT_SYMBOL_GPL(ib_detach_nvmf_ns);

struct ib_dm *ib_exp_alloc_dm(struct ib_device *device, u64 length)
{
	struct ib_dm *dm;

	if (!device->ops.exp_alloc_dm)
		return ERR_PTR(-ENOSYS);

	dm = device->ops.exp_alloc_dm(device, NULL, length, 0, NULL);
	if (!IS_ERR(dm)) {
		dm->device = device;
		dm->length = length;
		dm->uobject = NULL;
	}

	return dm;
}
EXPORT_SYMBOL_GPL(ib_exp_alloc_dm);

int ib_exp_free_dm(struct ib_dm *dm, struct uverbs_attr_bundle *attrs)
{
	int err;

	if (!dm->device->ops.exp_free_dm)
		return -ENOSYS;

	WARN_ON(atomic_read(&dm->usecnt));

	err = dm->device->ops.exp_free_dm(dm, attrs);

	return err;
}
EXPORT_SYMBOL_GPL(ib_exp_free_dm);

struct ib_mr *ib_exp_alloc_mr(struct ib_pd *pd, struct ib_mr_init_attr *attr)
{
	struct ib_mr *mr;

	if (!pd->device->ops.exp_alloc_mr)
		return ERR_PTR(-ENOSYS);

	if ((attr->mr_type == IB_MR_TYPE_DM) && !attr->dm)
		return ERR_PTR(-EINVAL);

	mr = pd->device->ops.exp_alloc_mr(pd, attr);
	if (!IS_ERR(mr)) {
		mr->device  = pd->device;
		mr->pd      = pd;
		mr->uobject = NULL;
		atomic_inc(&pd->usecnt);
		mr->need_inval = false;
	}

	return mr;
}
EXPORT_SYMBOL_GPL(ib_exp_alloc_mr);

int ib_exp_invalidate_range(struct ib_device  *device, struct ib_mr *ibmr,
			    u64 start, u64 length, u32 flags)
{
	if (!device->ops.exp_invalidate_range)
		return -EOPNOTSUPP;

	return device->ops.exp_invalidate_range(device, NULL, start, length, flags);
}
EXPORT_SYMBOL(ib_exp_invalidate_range);
