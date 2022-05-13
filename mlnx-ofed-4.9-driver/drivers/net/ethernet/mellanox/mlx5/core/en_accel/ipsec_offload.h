/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2020, Mellanox Technologies inc. All rights reserved. */

#ifndef __MLX5_IPSEC_H__
#define __MLX5_IPSEC_H__

#include <linux/mlx5/driver.h>
#include "accel/ipsec.h"

struct mlx5e_ipsec_sa_ctx {
	struct rhash_head       hash;
	u32                     enc_key_id;
	u32                     ipsec_obj_id;
	struct mlx5_flow_handle *ipsec_rule;
	struct mlx5_modify_hdr  *set_modify_hdr;
	/* hw ctx */
	struct mlx5_core_dev    *dev;
	struct mlx5e_ipsec_esp_xfrm     *mxfrm;
};

#ifdef CONFIG_MLX5_IPSEC

struct mlx5_accel_esp_xfrm *
mlx5e_ipsec_esp_create_xfrm(struct mlx5_core_dev *mdev,
			    const struct mlx5_accel_esp_xfrm_attrs *attrs,
			    u32 flags);

void mlx5e_ipsec_esp_destroy_xfrm(struct mlx5_accel_esp_xfrm *xfrm);

void *mlx5e_ipsec_create_sa_ctx(struct mlx5_core_dev *mdev,
				struct mlx5_accel_esp_xfrm *accel_xfrm,
				u32 *hw_handle);

void mlx5e_ipsec_delete_sa_ctx(void *context);

u32 mlx5e_ipsec_device_caps(struct mlx5_core_dev *mdev);

static inline bool mlx5e_is_ipsec_device(struct mlx5_core_dev *mdev)
{
	if (!MLX5_CAP_GEN(mdev, ipsec_offload))
		return false;

	if (!MLX5_CAP_GEN(mdev, log_max_dek))
		return false;

	return MLX5_CAP_IPSEC(mdev, ipsec_crypto_offload) &&
		MLX5_CAP_ETH(mdev, insert_trailer);
}

int mlx5e_ipsec_esp_modify_xfrm(struct mlx5_accel_esp_xfrm *xfrm,
				const struct mlx5_accel_esp_xfrm_attrs *attrs);
#else
static inline u32 mlx5e_ipsec_device_caps(struct mlx5_core_dev *mdev)
{
	return 0;
}

static inline struct mlx5_accel_esp_xfrm *
mlx5e_ipsec_esp_create_xfrm(struct mlx5_core_dev *mdev,
			    const struct mlx5_accel_esp_xfrm_attrs *attrs,
			    u32 flags)
{
	return NULL;
}

static inline void mlx5e_ipsec_esp_destroy_xfrm(struct mlx5_accel_esp_xfrm *xfrm) {}

static inline void *mlx5e_ipsec_create_sa_ctx(struct mlx5_core_dev *mdev,
					      struct mlx5_accel_esp_xfrm *accel_xfrm,
					      u32 *hw_handle)
{
	return NULL;
}

static inline void mlx5e_ipsec_delete_sa_ctx(void *context) {}

static inline bool mlx5e_is_ipsec_device(struct mlx5_core_dev *mdev)
{
	return false;
}

static inline int mlx5e_ipsec_esp_modify_xfrm(struct mlx5_accel_esp_xfrm *xfrm,
					      const struct mlx5_accel_esp_xfrm_attrs *attrs)
{
	return 0;
}

#endif /* CONFIG_MLX5_IPSEC */
#endif /* __MLX5_IPSEC_H__ */
