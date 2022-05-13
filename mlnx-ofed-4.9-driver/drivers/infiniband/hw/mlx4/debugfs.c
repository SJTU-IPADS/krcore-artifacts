/*
 * Copyright (c) 2012 Mellanox Technologies. All rights reserved.
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

#include "mlx4_ib.h"
#include <linux/debugfs.h>
#include <linux/export.h>
#include "linux/mlx4/device.h"
#include "ecn.h"

static struct dentry *mlx4_root;

void mlx4_ib_create_debug_files(struct mlx4_ib_dev *dev)
{
	int i;
	struct dentry *ecn;

	mlx4_ib_delete_debug_files(dev);
	if (!mlx4_root)
		return;
	INIT_LIST_HEAD(&dev->dbgfs_resources_list);
	dev->dev_root = debugfs_create_dir(dev->ib_dev.name, mlx4_root);
	if (!dev->dev_root)
		return;

	if (en_ecn) {
		ecn = debugfs_create_dir("ecn", dev->dev_root);
		if (!con_ctrl_dbgfs_init(ecn, dev)) {
			for (i = CTRL_ALGO_R_ROCE_ECN_1_REACTION_POINT;
			     i < CTRL_ALGO_SZ; i++) {
				void *algo_alloced =
					con_ctrl_dbgfs_add_algo(ecn, dev, i);
				if (algo_alloced) {
					INIT_LIST_HEAD((struct list_head *)
						 algo_alloced);
					list_add((struct list_head *)
						 algo_alloced,
						 &dev->dbgfs_resources_list);
				}
			}
		} else {
			pr_warn("ecn: failed to initialize qcn/ecn");
		}
	}
}

void mlx4_ib_delete_debug_files(struct mlx4_ib_dev *dev)
{
	if (dev->dev_root) {
		struct list_head *dev_res, *temp;

		debugfs_remove_recursive(dev->dev_root);
		list_for_each_safe(dev_res, temp, &dev->dbgfs_resources_list) {
			list_del(dev_res);
			kfree(dev_res);
		}
		con_ctrl_dbgfs_free(dev);
		dev->dev_root = NULL;
	}
}

int mlx4_ib_register_debugfs(void)
{
	mlx4_root = debugfs_create_dir("mlx4_ib", NULL);
	return mlx4_root ? 0 : -ENOMEM;
}

void mlx4_ib_unregister_debugfs(void)
{
	debugfs_remove_recursive(mlx4_root);
}
