/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2019 Mellanox Technologies. */

#ifndef __MLX5_EN_PARAMS_H__
#define __MLX5_EN_PARAMS_H__

#include "en.h"

u32 mlx5e_rx_get_linear_frag_sz(struct mlx5e_params *params);
u8 mlx5e_mpwqe_log_pkts_per_wqe(struct mlx5e_params *params);
bool mlx5e_rx_is_linear_skb(struct mlx5e_params *params);
bool mlx5e_rx_mpwqe_is_linear_skb(struct mlx5_core_dev *mdev,
				  struct mlx5e_params *params);
u8 mlx5e_mpwqe_get_log_rq_size(struct mlx5e_params *params);
u8 mlx5e_mpwqe_get_log_stride_size(struct mlx5_core_dev *mdev,
				   struct mlx5e_params *params);
u8 mlx5e_mpwqe_get_log_num_strides(struct mlx5_core_dev *mdev,
				   struct mlx5e_params *params);
u16 mlx5e_get_rq_headroom(struct mlx5_core_dev *mdev,
			  struct mlx5e_params *params);

#endif /* __MLX5_EN_PARAMS_H__ */
