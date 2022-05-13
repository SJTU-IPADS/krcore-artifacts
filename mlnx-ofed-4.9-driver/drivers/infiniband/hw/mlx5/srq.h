/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/*
 * Copyright (c) 2013-2018, Mellanox Technologies. All rights reserved.
 */

#ifndef MLX5_IB_SRQ_H
#define MLX5_IB_SRQ_H

enum {
	MLX5_SRQ_FLAG_ERR    = (1 << 0),
	MLX5_SRQ_FLAG_WQ_SIG = (1 << 1),
	MLX5_SRQ_FLAG_RNDV   = (1 << 2),
	MLX5_SRQ_FLAG_SET_DC_OP = (1 << 3),
	MLX5_SRQ_FLAG_STRIDING_RECV_WQ = (1 << 4),
};

enum mlx5_nvmf_offload_type {
	MLX5_NVMF_WRITE_OFFLOAD			= 1,
	MLX5_NVMF_READ_OFFLOAD			= 2,
	MLX5_NVMF_READ_WRITE_OFFLOAD		= 3,
	MLX5_NVMF_READ_WRITE_FLUSH_OFFLOAD	= 4,
};

struct mlx5_nvmf_attr {
	enum mlx5_nvmf_offload_type	type;
	u8				log_max_namespace;
	u32				cmd_unknown_namespace_cnt;
	u32				ioccsz;
	u8				icdoff;
	u8				log_max_io_size;
	u8				nvme_memory_log_page_size;
	u8				staging_buffer_log_page_size;
	u16				staging_buffer_number_of_pages;
	u8				staging_buffer_page_offset;
	u32				nvme_queue_size;
	u64				*staging_buffer_pas;
};

struct mlx5_dc_offload_params {
	u16                             pkey_index;
	enum ib_mtu                     path_mtu;
	u8                              sl;
	u8                              max_rd_atomic;
	u8                              min_rnr_timer;
	u8                              timeout;
	u8                              retry_cnt;
	u8                              rnr_retry;
	u64                             dct_key;
	u32                             ooo_caps;
};

struct mlx5_striding_recv_wq {
	u8 log_wqe_num_of_strides;
	u8 log_wqe_stride_size;
};

struct mlx5_srq_attr {
	u32 type;
	u32 flags;
	u32 log_size;
	u32 wqe_shift;
	u32 log_page_size;
	u32 wqe_cnt;
	u32 srqn;
	u32 xrcd;
	u32 page_offset;
	u32 cqn;
	u32 pd;
	u32 lwm;
	u32 user_index;
	u64 db_record;
	__be64 *pas;
	u32 tm_log_list_size;
	u32 tm_next_tag;
	u32 tm_hw_phase_cnt;
	u32 tm_sw_phase_cnt;
	u16 uid;
	struct mlx5_nvmf_attr nvmf;
	struct mlx5_dc_offload_params dc_op;
	struct mlx5_striding_recv_wq	striding_recv_wq;
};

struct mlx5_ib_dev;

struct mlx5_core_srq {
	struct mlx5_core_rsc_common common; /* must be first */
	u32 srqn;
	int max;
	size_t max_gs;
	size_t max_avail_gather;
	int wqe_shift;
	void (*event)(struct mlx5_core_srq *srq, enum mlx5_event e);

	/* protect ctrl list */
	spinlock_t		lock;
	struct list_head	ctrl_list;
	u16 uid;
};

struct mlx5_srq_table {
	struct notifier_block nb;
	struct xarray array;
};

int mlx5_cmd_create_srq(struct mlx5_ib_dev *dev, struct mlx5_core_srq *srq,
			struct mlx5_srq_attr *in);
void mlx5_cmd_destroy_srq(struct mlx5_ib_dev *dev, struct mlx5_core_srq *srq);
int mlx5_cmd_query_srq(struct mlx5_ib_dev *dev, struct mlx5_core_srq *srq,
		       struct mlx5_srq_attr *out);
int mlx5_cmd_arm_srq(struct mlx5_ib_dev *dev, struct mlx5_core_srq *srq,
		     u16 lwm, int is_srq);
struct mlx5_core_srq *mlx5_cmd_get_srq(struct mlx5_ib_dev *dev, u32 srqn);

int mlx5_init_srq_table(struct mlx5_ib_dev *dev);
void mlx5_cleanup_srq_table(struct mlx5_ib_dev *dev);
#endif /* MLX5_IB_SRQ_H */
