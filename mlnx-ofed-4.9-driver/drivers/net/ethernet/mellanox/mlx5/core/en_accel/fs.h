/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2020, Mellanox Technologies inc. All rights reserved. */

#ifndef __MLX5E_ACCEL_FS_H__
#define __MLX5E_ACCEL_FS_H__

#ifdef CONFIG_MLX5_EN_ACCEL_FS

#include "en.h"

#define MLX5E_ACCEL_FS_NUM_GROUPS	(2)
#define MLX5E_ACCEL_FS_GROUP1_SIZE	(BIT(16) - 1)
#define MLX5E_ACCEL_FS_GROUP2_SIZE	(BIT(0))
#define MLX5E_ACCEL_FS_TABLE_SIZE	(MLX5E_ACCEL_FS_GROUP1_SIZE +\
					 MLX5E_ACCEL_FS_GROUP2_SIZE)

int mlx5e_accel_fs_create_tables(struct mlx5e_priv *priv);
void mlx5e_accel_fs_destroy_tables(struct mlx5e_priv *priv);

struct mlx5_flow_handle *mlx5e_accel_fs_add_flow(struct mlx5e_priv *priv,
						 struct sock *sk, u32 tirn,
						 uint32_t flow_tag);

#else

int mlx5e_accel_fs_create_tables(struct mlx5e_priv *priv) {return 0; }
void mlx5e_accel_fs_destroy_tables(struct mlx5e_priv *priv) {}
struct mlx5_flow_handle *mlx5e_accel_fs_add_flow(struct mlx5e_priv *priv,
						 struct sock *sk, u32 tirn,
						 uint32_t flow_tag)
{ return NULL; }

#endif

#endif /* __MLX5E_ACCEL_FS_H__ */

