/*
 * Copyright (c) 2015, Mellanox Technologies inc.  All rights reserved.
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

#include <linux/module.h>
#include <linux/debugfs.h>
#include "en.h"

/* For non-default namespaces, add suffix in format "@<pci_id>" */
/* PCI id format: "%04x:%02x:%02x.%d" pci_domain bus_num pci_slot pci_func */
#define PCI_ID_LEN 16
#define MLX5_MAX_DEBUGFS_ROOT_NAME_LEN (IFNAMSIZ + 1 + PCI_ID_LEN)
#define MLX5_MAX_DEBUGFS_NAME_LEN 16

static void mlx5e_create_channel_debugfs(struct mlx5e_priv *priv,
					 int channel_num)
{
	int i;
	char name[MLX5_MAX_DEBUGFS_NAME_LEN];
	struct dentry *channel_root;
	struct mlx5e_channel *channel;

	snprintf(name, MLX5_MAX_DEBUGFS_NAME_LEN, "channel-%d", channel_num);
	channel_root = debugfs_create_dir(name, priv->dfs_root);
	if (!channel_root) {
		netdev_err(priv->netdev,
			   "Failed to create channel debugfs for %s\n",
			   priv->netdev->name);
		return;
	}
	priv->channels.c[channel_num]->dfs_root = channel_root;
	channel = priv->channels.c[channel_num];

	for (i = 0; i < priv->channels.params.num_tc; i++) {
		snprintf(name, MLX5_MAX_DEBUGFS_NAME_LEN, "sqn-%d", i);
		debugfs_create_u32(name, S_IRUSR, channel_root,
				   &channel->sq[i].sqn);

		snprintf(name, MLX5_MAX_DEBUGFS_NAME_LEN, "sq-cqn-%d", i);
		debugfs_create_u32(name, S_IRUSR, channel_root,
				   &channel->sq[i].cq.mcq.cqn);
	}

	debugfs_create_u32("rqn", S_IRUSR, channel_root,
			   &channel->rq.rqn);

	debugfs_create_u32("rq-cqn", S_IRUSR, channel_root,
			   &channel->rq.cq.mcq.cqn);
}

void mlx5e_create_debugfs(struct mlx5e_priv *priv)
{
	int i;
	char ns_root_name[MLX5_MAX_DEBUGFS_ROOT_NAME_LEN];
	char name[MLX5_MAX_DEBUGFS_NAME_LEN];
	char *root_name;

	struct net_device *dev = priv->netdev;
	struct net *net = dev_net(dev);

	if (net_eq(net, &init_net)) {
		root_name = dev->name;
	} else {
		snprintf(ns_root_name, MLX5_MAX_DEBUGFS_ROOT_NAME_LEN,
			 "%s@%s", dev->name, dev_name(priv->mdev->device));
		root_name = ns_root_name;
	}

	priv->dfs_root = debugfs_create_dir(root_name, NULL);
	if (!priv->dfs_root) {
		netdev_err(priv->netdev, "Failed to init debugfs files for %s\n",
			   root_name);
		return;
	}

	debugfs_create_u8("num_tc", S_IRUSR, priv->dfs_root,
			  &priv->channels.params.num_tc);

	for (i = 0; i < mlx5e_get_num_lag_ports(priv->mdev); i++) {
		int tc;

		for (tc = 0; tc < priv->channels.params.num_tc; tc++) {
			snprintf(name, MLX5_MAX_DEBUGFS_NAME_LEN, "tisn-%d_%d", i, tc);
			debugfs_create_u32(name, S_IRUSR, priv->dfs_root,
					   &priv->tisn[i][tc]);
		}
	}

	for (i = 0; i < MLX5E_NUM_INDIR_TIRS; i++) {
		snprintf(name, MLX5_MAX_DEBUGFS_NAME_LEN, "indir-tirn-%d", i);
		debugfs_create_u32(name, S_IRUSR, priv->dfs_root,
				   &priv->indir_tir[i].tirn);
	}

	for (i = 0; i < MLX5E_MAX_NUM_CHANNELS; i++) {
		snprintf(name, MLX5_MAX_DEBUGFS_NAME_LEN, "dir-tirn-%d", i);
		debugfs_create_u32(name, S_IRUSR, priv->dfs_root,
				   &priv->direct_tir[i].tirn);
	}

	for (i = 0; i < priv->channels.num; i++)
		mlx5e_create_channel_debugfs(priv, i);
}

void mlx5e_destroy_debugfs(struct mlx5e_priv *priv)
{
	debugfs_remove_recursive(priv->dfs_root);
	priv->dfs_root = NULL;
}
