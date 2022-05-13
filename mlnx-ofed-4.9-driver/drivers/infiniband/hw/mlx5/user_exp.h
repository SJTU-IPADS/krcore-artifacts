/*
 * Copyright (c) 2016, Mellanox Technologies. All rights reserved.
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

#ifndef MLX5_IB_USER_EXP_H
#define MLX5_IB_USER_EXP_H

#include <rdma/mlx5-abi.h>

enum mlx5_exp_ib_create_cq_mask {
	MLX5_EXP_CREATE_CQ_MASK_CQE_COMP_EN		= 1 << 0,
	MLX5_EXP_CREATE_CQ_MASK_CQE_COMP_RECV_TYPE      = 1 << 1,
	MLX5_EXP_CREATE_CQ_MASK_RESERVED		= 1 << 2,
};

enum mlx5_exp_cqe_comp_recv_type {
	MLX5_IB_CQE_FORMAT_HASH,
	MLX5_IB_CQE_FORMAT_CSUM,
};

struct mlx5_exp_ib_create_cq_data {
	__u32   comp_mask; /* use mlx5_exp_ib_creaet_cq_mask */
	__u8    cqe_comp_en;
	__u8    cqe_comp_recv_type; /* use mlx5_exp_cqe_comp_recv_type */
	__u8    as_notify_en;
	__u8	reserved;
};

struct mlx5_exp_ib_create_cq {
	__u64					buf_addr;
	__u64					db_addr;
	__u32					cqe_size;
	__u8					cqe_comp_en;
	__u8					cqe_comp_res_format;
	__u16					flags; /* Use mlx5_ib_create_cq_flags */

	/* Some more reserved fields for future growth of mlx5_ib_create_cq */
	__u64					prefix_reserved[8];

	/* sizeof prefix aligned with mlx5_ib_create_cq */
	__u64					size_of_prefix;
	struct mlx5_exp_ib_create_cq_data	exp_data;
};

enum mlx5_exp_ib_alloc_ucontext_data_resp_mask {
	MLX5_EXP_ALLOC_CTX_RESP_MASK_CQE_COMP_MAX_NUM		= 1 << 0,
	MLX5_EXP_ALLOC_CTX_RESP_MASK_CQE_VERSION		= 1 << 1,
	MLX5_EXP_ALLOC_CTX_RESP_MASK_RROCE_UDP_SPORT_MIN	= 1 << 2,
	MLX5_EXP_ALLOC_CTX_RESP_MASK_RROCE_UDP_SPORT_MAX	= 1 << 3,
	MLX5_EXP_ALLOC_CTX_RESP_MASK_HCA_CORE_CLOCK_OFFSET	= 1 << 4,
	MLX5_EXP_ALLOC_CTX_RESP_MASK_MAX_DESC_SZ_SQ_DC		= 1 << 5,
	MLX5_EXP_ALLOC_CTX_RESP_MASK_ATOMIC_ARG_SIZES_DC	= 1 << 6,
	MLX5_EXP_ALLOC_CTX_RESP_MASK_FLAGS			= 1 << 7,
	MLX5_EXP_ALLOC_CTX_RESP_MASK_CLOCK_INFO			= 1 << 8,
};

struct mlx5_exp_ib_alloc_ucontext_data_resp {
	__u32   comp_mask; /* use mlx5_ib_exp_alloc_ucontext_data_resp_mask */
	__u16	cqe_comp_max_num;
	__u8	cqe_version;
	__u8	clock_info_version_mask;
	__u16	rroce_udp_sport_min;
	__u16	rroce_udp_sport_max;
	__u32	hca_core_clock_offset;
	__u32	max_desc_sz_sq_dc;
	__u32	atomic_arg_sizes_dc;
	__u32	flags;
};

struct mlx5_exp_ib_alloc_ucontext_resp {
	__u32						qp_tab_size;
	__u32						bf_reg_size;
	__u32						tot_bfregs;
	__u32						cache_line_size;
	__u16						max_sq_desc_sz;
	__u16						max_rq_desc_sz;
	__u32						max_send_wqebb;
	__u32						max_recv_wr;
	__u32						max_srq_recv_wr;
	__u16						num_ports;
	__u16						flow_action_flags;
	__u32						comp_mask;
	__u32						response_length;
	__u8						cqe_version;
	__u8						cmds_supp_uhw;
	__u8						eth_min_inline;
	__u8						clock_info_versions;
	__u64						hca_core_clock_offset;
	__u32						log_uar_size;
	__u32						num_uars_per_page;
	__u32						num_dyn_bfregs;
	__u32                                           dump_fill_mkey;
	/* Some more reserved fields for future growth of mlx5_ib_alloc_ucontext_resp */
	__u64						prefix_reserved[8];
	struct mlx5_exp_ib_alloc_ucontext_data_resp	exp_data;
};

enum mlx5_exp_ib_create_qp_mask {
	MLX5_EXP_CREATE_QP_MASK_UIDX		= 1 << 0,
	MLX5_EXP_CREATE_QP_MASK_SQ_BUFF_ADD	= 1 << 1,
	MLX5_EXP_CREATE_QP_MASK_WC_UAR_IDX	= 1 << 2,
	MLX5_EXP_CREATE_QP_MASK_FLAGS_IDX	= 1 << 3,
	MLX5_EXP_CREATE_QP_MASK_ASSOC_QPN	= 1 << 4,
	MLX5_EXP_CREATE_QP_MASK_RESERVED	= 1 << 5,
};

enum mlx5_exp_create_qp_flags {
	MLX5_EXP_CREATE_QP_MULTI_PACKET_WQE_REQ_FLAG = 1 << 0,
};

struct mlx5_exp_ib_create_qp_data {
	__u32   comp_mask; /* use mlx5_exp_ib_create_qp_mask */
	__u32   uidx;
	__u64	sq_buf_addr;
	__u32   wc_uar_index;
	__u32   flags; /* use mlx5_exp_create_qp_flags */
	__u32  associated_qpn;
	__u32  reserved;
};

enum mlx5_exp_ib_create_qp_resp_mask {
	MLX5_EXP_CREATE_QP_RESP_MASK_FLAGS_IDX	= 1 << 0,
	MLX5_EXP_CREATE_QP_RESP_MASK_RESERVED	= 1 << 1,
};

enum mlx5_exp_create_qp_resp_flags {
	MLX5_EXP_CREATE_QP_RESP_MULTI_PACKET_WQE_FLAG = 1 << 0,
};

struct mlx5_exp_ib_create_qp {
	/* To allow casting to mlx5_ib_create_qp the prefix is the same as
	 * struct mlx5_ib_create_qp prefix
	 */
	__aligned_u64 buf_addr;
	__aligned_u64 db_addr;
	__u32	sq_wqe_count;
	__u32	rq_wqe_count;
	__u32	rq_wqe_shift;
	__u32	flags;
	__u32	uidx;
	__u32	bfreg_index;
	__u64	sq_buf_addr;

	/* Some more reserved fields for future growth of mlx5_ib_create_qp */
	__u64   prefix_reserved[6];

	/* sizeof prefix aligned with mlx5_ib_create_qp */
	__u64   size_of_prefix;

	/* Experimental data
	 * Add new experimental data only inside the exp struct
	 */
	struct mlx5_exp_ib_create_qp_data exp;
};

enum mlx5_exp_drv_create_qp_uar_idx {
	MLX5_EXP_CREATE_QP_DB_ONLY_BFREG = -1
};


struct mlx5_exp_ib_create_qp_resp_data {
	__u32   comp_mask; /* use mlx5_exp_ib_create_qp_resp_mask */
	__u32   flags; /* use mlx5_exp_create_qp_resp_flags */
};

struct mlx5_exp_ib_create_qp_resp {
	__u32   bfreg_index;
	__u32   rsvd;

	/* Some more reserved fields for future growth of
	 * mlx5_ib_create_qp_resp
	 */
	__u64   prefix_reserved[8];

	/* sizeof prefix aligned with mlx5_ib_create_qp_resp */
	__u64   size_of_prefix;

	/* Experimental data
	 * Add new experimental data only inside the exp struct
	 */
	struct mlx5_exp_ib_create_qp_resp_data exp;
};

struct mlx5_ib_create_dct {
	__u32   uidx;
	__u32   reserved;
};

struct mlx5_ib_arm_dct {
	__u64	reserved0;
	__u64	reserved1;
};

struct mlx5_ib_arm_dct_resp {
	__u64	reserved0;
	__u64	reserved1;
};


enum  mlx5_exp_wq_init_attr_mask {
	MLX5_EXP_MODIFY_WQ_VLAN_OFFLOADS = (1 << 0),
};

struct mlx5_ib_exp_modify_wq {
	__u32	comp_mask;
	__u32	attr_mask;
	__u16	vlan_offloads;
	__u8	reserved[6];
};

enum mlx5_ib_exp_create_wq_comp_mask {
	MLX5_EXP_CREATE_WQ_MP_RQ		= 1 << 0,
	MLX5_EXP_CREATE_WQ_VLAN_OFFLOADS	= 1 << 1,
	MLX5_EXP_CREATE_WQ_RESERVED		= 1 << 2,
};

struct mlx5_ib_create_wq_data_mp_rq {
	__u32	use_shift;
	__u8    single_wqe_log_num_of_strides;
	__u8    single_stride_log_num_of_bytes;
};

struct mlx5_ib_create_wq_data {
	__u64   buf_addr;
	__u64   db_addr;
	__u32   rq_wqe_count;
	__u32   rq_wqe_shift;
	__u32   user_index;
	__u32   flags;
	__u32   comp_mask;
	struct  mlx5_ib_create_wq_data_mp_rq mp_rq;
	__u16	vlan_offloads;
};

struct mlx5_ib_create_wq_mp_rq {
	__u32	use_shift;
	__u8    single_wqe_log_num_of_strides;
	__u8    single_stride_log_num_of_bytes;
	__u16   reserved2;
};

struct mlx5_ib_exp_create_wq {
	__u64   buf_addr;
	__u64   db_addr;
	__u32   rq_wqe_count;
	__u32   rq_wqe_shift;
	__u32   user_index;
	__u32   flags;
	__u32   comp_mask;
	__u16	vlan_offloads;
	__u16   reserved;
	struct mlx5_ib_create_wq_mp_rq mp_rq;
};

enum mlx5_exp_create_srq_mask {
	MLX5_EXP_CREATE_SRQ_MASK_DC_OP	  = 1 << 0,
	MLX5_EXP_CREATE_SRQ_MASK_MP_WR	  = 1 << 1,
	MLX5_EXP_CREATE_SRQ_MASK_RESERVED = 1 << 2,
};

struct mlx5_ib_set_xrq_dc_offload_params {
	__u16	pkey_index;
	__u8	path_mtu;
	__u8	sl;
	__u8	max_rd_atomic;
	__u8	min_rnr_timer;
	__u8	timeout;
	__u8	retry_cnt;
	__u8	rnr_retry;
	__u8	reserved2[3];
	__u32	ooo_caps;
	__u64	dct_key;
};

struct mlx5_ib_exp_create_srq {
	__u64	buf_addr;
	__u64	db_addr;
	__u32	flags;
	__u32	reserved0;
	__u32	uidx;
	__u8	mp_wr_log_num_of_strides;
	__u8	mp_wr_log_stride_size;
	__u16	reserved1;
	__u32	max_num_tags;
	__u32	comp_mask;
	struct mlx5_ib_set_xrq_dc_offload_params dc_op;
};

static inline int get_qp_exp_user_index(struct mlx5_ib_ucontext *ucontext,
					struct mlx5_exp_ib_create_qp *ucmd,
					int inlen,
					u32 *user_index)
{
	if (ucmd->exp.comp_mask & MLX5_EXP_CREATE_QP_MASK_UIDX)
		*user_index = ucmd->exp.uidx;
	else
		*user_index = MLX5_IB_DEFAULT_UIDX;

	return 0;
}

#endif /* MLX5_IB_USER_EXP_H */
