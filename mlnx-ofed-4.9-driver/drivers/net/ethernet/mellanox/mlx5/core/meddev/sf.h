/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2019 Mellanox Technologies */

#ifndef __MLX5_SF_H__
#define __MLX5_SF_H__

#include <linux/mlx5/driver.h>
#include <linux/mlx5/eswitch.h>
#include <linux/mdev.h>

struct mlx5_sf {
	struct mlx5_core_dev *dev;
	struct mlx5_core_dev *parent_dev;
	phys_addr_t bar_base_addr;
	u16 idx;	/* Index allocated by the SF table bitmap */
};

struct mlx5_sf_table {
	phys_addr_t base_address;
	/* Protects sfs life cycle and sf enable/disable flows */
	struct mutex lock;
	unsigned long *sf_id_bitmap;
	u16 max_sfs;
	u16 log_sf_bar_size;
};

static inline bool mlx5_core_is_sf_supported(const struct mlx5_core_dev *dev)
{
	return MLX5_ESWITCH_MANAGER(dev) &&
	       MLX5_CAP_GEN(dev, max_num_sf_partitions) &&
	       MLX5_CAP_GEN(dev, sf);
}

static inline u16 mlx5_sf_base_id(const struct mlx5_core_dev *dev)
{
	return MLX5_CAP_GEN(dev, sf_base_id);
}

#ifdef CONFIG_MLX5_MDEV
int mlx5_sf_table_init(struct mlx5_core_dev *dev,
		       struct mlx5_sf_table *sf_table);
void mlx5_sf_table_cleanup(struct mlx5_core_dev *dev,
			   struct mlx5_sf_table *sf_table);

struct mlx5_sf *
mlx5_sf_alloc(struct mlx5_core_dev *coredev, struct mlx5_sf_table *sf_table,
	      struct device *dev);
void mlx5_sf_free(struct mlx5_core_dev *coredev, struct mlx5_sf_table *sf_table,
		  struct mlx5_sf *sf);
u16 mlx5_core_max_sfs(const struct mlx5_core_dev *dev,
		      struct mlx5_sf_table *sf_table);
u16 mlx5_get_free_sfs(struct mlx5_core_dev *dev,
		      struct mlx5_sf_table *sf_table);
int mlx5_sf_set_max_sfs(struct mlx5_core_dev *dev,
			struct mlx5_sf_table *sf_table, u16 new_max_sfs);

int mlx5_sf_load(struct mlx5_sf *sf);
void mlx5_sf_unload(struct mlx5_sf *sf);

static inline struct mlx5_core_dev *
mlx5_sf_get_parent_dev(struct mlx5_core_dev *dev)
{
	struct mdev_device *meddev = mdev_from_dev(dev->device);
	struct mlx5_sf *sf = mdev_get_drvdata(meddev);

	return sf->parent_dev;
}

#else
static inline u16 mlx5_core_max_sfs(const struct mlx5_core_dev *dev,
				    struct mlx5_sf_table *sf_table)
{
	return 0;
}

static inline u16 mlx5_get_free_sfs(struct mlx5_core_dev *dev,
				    struct mlx5_sf_table *sf_table)
{
	return 0;
}

static inline struct mlx5_core_dev *
mlx5_sf_get_parent_dev(struct mlx5_core_dev *dev)
{
	return NULL;
}

#endif

#endif
