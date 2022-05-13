/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2019, Mellanox Technologies inc. All rights reserved. */

#ifndef __MLX5_IPSEC_STEERING_H__
#define __MLX5_IPSEC_STEERING_H__

#include "en.h"
#include "ipsec.h"
#include "ipsec_offload.h"

int mlx5e_ipsec_create_rx_err_ft(struct mlx5e_priv *priv);
void mlx5e_ipsec_destroy_rx_err_ft(struct mlx5e_priv *priv);
int mlx5e_xfrm_add_rule(struct mlx5_core_dev *mdev,
			struct mlx5_accel_esp_xfrm_attrs *attrs,
			struct mlx5e_ipsec_sa_ctx *sa_ctx);

void mlx5e_xfrm_del_rule(struct mlx5e_ipsec_sa_ctx *sa_ctx);
int mlx5e_ipsec_create_tx_ft(struct mlx5e_priv *priv);
void mlx5e_ipsec_destroy_tx_ft(struct mlx5e_priv *priv);
#endif /* __MLX5_IPSEC_STEERING_H__ */
