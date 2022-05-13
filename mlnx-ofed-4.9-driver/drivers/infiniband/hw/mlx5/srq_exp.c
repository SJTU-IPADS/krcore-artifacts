/*
 * Copyright (c) 2017 Mellanox Technologies. All rights reserved.
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

#include <linux/mlx5/qp.h>
#include <linux/mlx5/qp_exp.h>
#include <rdma/ib_verbs_exp.h>

#include "mlx5_ib.h"
#include "user_exp.h"
#include "srq_exp.h"

int mlx5_ib_exp_create_srq_user(struct mlx5_ib_dev *dev,
				struct mlx5_srq_attr *in,
				struct ib_udata *udata,
				struct mlx5_ib_create_srq *ucmd)
{
	struct mlx5_ib_exp_create_srq ucmd_exp = {};
	size_t ucmdlen;

	ucmdlen = min(udata->inlen, sizeof(ucmd_exp));
	if (ib_copy_from_udata(&ucmd_exp, udata, ucmdlen)) {
		mlx5_ib_dbg(dev, "failed copy udata\n");
		return -EFAULT;
	}

	if (ucmd_exp.reserved0 || ucmd_exp.reserved1 ||
	    ucmd_exp.comp_mask >= MLX5_EXP_CREATE_SRQ_MASK_RESERVED)
		return -EINVAL;

	if (ucmd_exp.comp_mask & MLX5_EXP_CREATE_SRQ_MASK_MP_WR) {
		if (!MLX5_CAP_GEN(dev->mdev, ib_striding_wq)) {
			mlx5_ib_dbg(dev, "MP WR isn't supported\n");
			return -EOPNOTSUPP;
		}

		if (in->type != IB_EXP_SRQT_TAG_MATCHING) {
			mlx5_ib_dbg(dev, "MP WR is supported over TM SRQ type only\n");
			return -EOPNOTSUPP;
		}

		if ((ucmd_exp.mp_wr_log_num_of_strides <
		     MLX5_EXT_MIN_SINGLE_WQE_LOG_NUM_STRIDES) ||
		    ((ucmd_exp.mp_wr_log_num_of_strides <
		      MLX5_MIN_SINGLE_WQE_LOG_NUM_STRIDES) &&
		     !MLX5_CAP_GEN(dev->mdev, ext_stride_num_range))) {
			mlx5_ib_dbg(dev, "Requested MP WR log2 number of strides %u is lower than supported\n",
				    ucmd_exp.mp_wr_log_num_of_strides);
			return -EINVAL;
		}

		if (ucmd_exp.mp_wr_log_num_of_strides >
		    MLX5_MAX_SINGLE_WQE_LOG_NUM_STRIDES) {
			mlx5_ib_dbg(dev, "Requested MP WR log2 number of strides %u is higher than supported\n",
				    ucmd_exp.mp_wr_log_num_of_strides);
			return -EINVAL;
		}

		if ((ucmd_exp.mp_wr_log_stride_size >
		     MLX5_MAX_SINGLE_STRIDE_LOG_NUM_BYTES) ||
		    (ucmd_exp.mp_wr_log_stride_size <
		     MLX5_MIN_SINGLE_STRIDE_LOG_NUM_BYTES)) {
			mlx5_ib_dbg(dev, "Requested MP WR log2 stride size of %u is unsupported\n",
				    ucmd_exp.mp_wr_log_stride_size);
			return -EINVAL;
		}

		in->flags |= MLX5_SRQ_FLAG_STRIDING_RECV_WQ;
		in->striding_recv_wq.log_wqe_num_of_strides =
			ucmd_exp.mp_wr_log_num_of_strides;
		in->striding_recv_wq.log_wqe_stride_size =
			ucmd_exp.mp_wr_log_stride_size;
	}

	if (in->type == IB_EXP_SRQT_TAG_MATCHING) {
		if (!ucmd_exp.max_num_tags)
			return -EINVAL;
		in->tm_log_list_size = ilog2(ucmd_exp.max_num_tags) + 1;
		if (in->tm_log_list_size >
		    MLX5_CAP_GEN(dev->mdev, log_tag_matching_list_sz)) {
			mlx5_ib_dbg(dev, "TM SRQ max_num_tags exceeding limit\n");
			return -EINVAL;
		}
		in->flags |= MLX5_SRQ_FLAG_RNDV;

		if (ucmd_exp.comp_mask & MLX5_EXP_CREATE_SRQ_MASK_DC_OP) {
			in->dc_op.pkey_index = ucmd_exp.dc_op.pkey_index;
			in->dc_op.path_mtu = ucmd_exp.dc_op.path_mtu;
			in->dc_op.sl = ucmd_exp.dc_op.sl;
			in->dc_op.max_rd_atomic = ucmd_exp.dc_op.max_rd_atomic;
			in->dc_op.min_rnr_timer = ucmd_exp.dc_op.min_rnr_timer;
			in->dc_op.timeout = ucmd_exp.dc_op.timeout;
			in->dc_op.retry_cnt = ucmd_exp.dc_op.retry_cnt;
			in->dc_op.rnr_retry = ucmd_exp.dc_op.rnr_retry;
			in->dc_op.dct_key = ucmd_exp.dc_op.dct_key;
			in->dc_op.ooo_caps = ucmd_exp.dc_op.ooo_caps;
			in->flags |= MLX5_SRQ_FLAG_SET_DC_OP;
		}
	}

	ucmdlen = offsetof(typeof(*ucmd), reserved1) + sizeof(ucmd->reserved1);
	ucmdlen = min(udata->inlen, ucmdlen);
	memcpy(ucmd, &ucmd_exp, ucmdlen);

	return 0;
}

int get_nvmf_pas_size(struct mlx5_nvmf_attr *nvmf)
{
	return nvmf->staging_buffer_number_of_pages * sizeof(u64);
}

void set_nvmf_srq_pas(struct mlx5_nvmf_attr *nvmf, __be64 *pas)
{
	int i;

	for (i = 0; i < nvmf->staging_buffer_number_of_pages; i++)
		pas[i] = cpu_to_be64(nvmf->staging_buffer_pas[i]);
}

void set_nvmf_xrq_context(struct mlx5_nvmf_attr *nvmf, void *xrqc)
{
	u16 nvme_queue_size;

        /*
         * According to the PRM, nvme_queue_size is a 16 bit field and
         * setting it to 0 means setting size to 2^16 (The maximum queue size
         * possible for an NVMe device).
         */
	if (nvmf->nvme_queue_size < 0x10000)
		nvme_queue_size = nvmf->nvme_queue_size;
	else
		nvme_queue_size = 0;


	MLX5_SET(xrqc, xrqc,
		 nvme_offload_context.nvmf_offload_type,
		 nvmf->type);
	MLX5_SET(xrqc, xrqc,
		 nvme_offload_context.log_max_namespace,
		 nvmf->log_max_namespace);
	MLX5_SET(xrqc, xrqc,
		 nvme_offload_context.ioccsz,
		 nvmf->ioccsz);
	MLX5_SET(xrqc, xrqc,
		 nvme_offload_context.icdoff,
		 nvmf->icdoff);
	MLX5_SET(xrqc, xrqc,
		 nvme_offload_context.log_max_io_size,
		 nvmf->log_max_io_size);
	MLX5_SET(xrqc, xrqc,
		 nvme_offload_context.nvme_memory_log_page_size,
		 nvmf->nvme_memory_log_page_size);
	MLX5_SET(xrqc, xrqc,
		 nvme_offload_context.staging_buffer_log_page_size,
		 nvmf->staging_buffer_log_page_size);
	MLX5_SET(xrqc, xrqc,
		 nvme_offload_context.staging_buffer_number_of_pages,
		 nvmf->staging_buffer_number_of_pages);
	MLX5_SET(xrqc, xrqc,
		 nvme_offload_context.staging_buffer_page_offset,
		 nvmf->staging_buffer_page_offset);
	MLX5_SET(xrqc, xrqc,
		 nvme_offload_context.nvme_queue_size,
		 nvme_queue_size);
}

static int mlx5_ib_check_nvmf_srq_attrs(struct ib_srq_init_attr *init_attr)
{
	switch (init_attr->ext.nvmf.type) {
	case IB_NVMF_WRITE_OFFLOAD:
	case IB_NVMF_READ_OFFLOAD:
	case IB_NVMF_READ_WRITE_OFFLOAD:
	case IB_NVMF_READ_WRITE_FLUSH_OFFLOAD:
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

/* Must be called after checking that offload type values are valid */
static enum mlx5_nvmf_offload_type to_mlx5_nvmf_offload_type(enum ib_nvmf_offload_type type)
{
	switch (type) {
	case IB_NVMF_WRITE_OFFLOAD:
		return MLX5_NVMF_WRITE_OFFLOAD;
	case IB_NVMF_READ_OFFLOAD:
		return MLX5_NVMF_READ_OFFLOAD;
	case IB_NVMF_READ_WRITE_OFFLOAD:
		return MLX5_NVMF_READ_WRITE_OFFLOAD;
	case IB_NVMF_READ_WRITE_FLUSH_OFFLOAD:
		return MLX5_NVMF_READ_WRITE_FLUSH_OFFLOAD;
	default:
		return -EINVAL;
	}
}

int mlx5_ib_exp_set_nvmf_srq_attrs(struct mlx5_nvmf_attr *nvmf,
				   struct ib_srq_init_attr *init_attr)
{
	int err;

	err = mlx5_ib_check_nvmf_srq_attrs(init_attr);
	if (err)
		return -EINVAL;

	nvmf->type = to_mlx5_nvmf_offload_type(init_attr->ext.nvmf.type);
	nvmf->log_max_namespace = init_attr->ext.nvmf.log_max_namespace;
	nvmf->ioccsz = init_attr->ext.nvmf.cmd_size;
	nvmf->icdoff = init_attr->ext.nvmf.data_offset;
	nvmf->log_max_io_size = init_attr->ext.nvmf.log_max_io_size;
	nvmf->nvme_memory_log_page_size = init_attr->ext.nvmf.nvme_memory_log_page_size;
	nvmf->staging_buffer_log_page_size = init_attr->ext.nvmf.staging_buffer_log_page_size;
	nvmf->staging_buffer_number_of_pages = init_attr->ext.nvmf.staging_buffer_number_of_pages;
	nvmf->staging_buffer_page_offset = init_attr->ext.nvmf.staging_buffer_page_offset;
	nvmf->nvme_queue_size = init_attr->ext.nvmf.nvme_queue_size;
	nvmf->staging_buffer_pas = init_attr->ext.nvmf.staging_buffer_pas;

	return err;
}

int set_xrq_dc_params_entry(struct mlx5_ib_dev *dev,
			    struct mlx5_core_srq *srq,
			    struct mlx5_dc_offload_params *dc_op)
{
	u32 in[MLX5_ST_SZ_DW(set_xrq_dc_params_entry_in)] = {0};
	u32 out[MLX5_ST_SZ_DW(set_xrq_dc_params_entry_out)] = {0};

	MLX5_SET(set_xrq_dc_params_entry_in, in, pkey_table_index,
		 dc_op->pkey_index);
	MLX5_SET(set_xrq_dc_params_entry_in, in, mtu, dc_op->path_mtu);
	MLX5_SET(set_xrq_dc_params_entry_in, in, sl, dc_op->sl);
	MLX5_SET(set_xrq_dc_params_entry_in, in, reverse_sl, dc_op->sl);
	MLX5_SET(set_xrq_dc_params_entry_in, in, cnak_reverse_sl, dc_op->sl);
	MLX5_SET(set_xrq_dc_params_entry_in, in, ack_timeout, dc_op->timeout);
	MLX5_SET64(set_xrq_dc_params_entry_in, in, dc_access_key,
		   dc_op->dct_key);

	MLX5_SET(set_xrq_dc_params_entry_in, in, xrqn, srq->srqn);
	MLX5_SET(set_xrq_dc_params_entry_in, in, opcode,
		 MLX5_CMD_OP_SET_XRQ_DC_PARAMS_ENTRY);

	return mlx5_cmd_exec(dev->mdev, in, sizeof(in), out, sizeof(out));
}
