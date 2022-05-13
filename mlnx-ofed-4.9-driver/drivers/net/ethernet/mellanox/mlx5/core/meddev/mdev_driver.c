// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2018-19 Mellanox Technologies

#include <linux/module.h>
#include <net/devlink.h>
#include <linux/mdev.h>

#include "mlx5_core.h"
#include "meddev/sf.h"

static const struct devlink_ops sf_devlink_ops = {};

static int mlx5_meddev_probe(struct device *dev)
{
	struct mdev_device *meddev = mdev_from_dev(dev);
	struct mlx5_sf *sf = mdev_get_drvdata(meddev);
	struct mlx5_core_dev *coredev;
	struct devlink *devlink;
	int ret;

	devlink = devlink_alloc(&sf_devlink_ops, sizeof(*coredev));
	if (!devlink)
		return -ENOMEM;

	coredev = devlink_priv(devlink);
	coredev->device = dev;
	coredev->pdev = sf->parent_dev->pdev;
	coredev->bar_addr = sf->bar_base_addr;
	coredev->iseg_base = sf->bar_base_addr;
	coredev->coredev_type = MLX5_COREDEV_SF;

	sf->dev = coredev;
	ret = mlx5_sf_load(sf);
	if (ret)
		goto load_err;

	return 0;

load_err:
	devlink_free(devlink);
	return ret;
}

static void mlx5_meddev_remove(struct device *dev)
{
	struct mdev_device *meddev = mdev_from_dev(dev);
	struct mlx5_sf *sf = mdev_get_drvdata(meddev);
	struct devlink *devlink;

	devlink = priv_to_devlink(sf->dev);
	mlx5_sf_unload(sf);
	devlink_free(devlink);
}

static struct mdev_driver mlx5_meddev_driver = {
	.name	= KBUILD_MODNAME,
	.probe	= mlx5_meddev_probe,
	.remove	= mlx5_meddev_remove,
};

int mlx5_meddev_register_driver(void)
{
	return mdev_register_driver(&mlx5_meddev_driver, THIS_MODULE);
}

void mlx5_meddev_unregister_driver(void)
{
	mdev_unregister_driver(&mlx5_meddev_driver);
}
