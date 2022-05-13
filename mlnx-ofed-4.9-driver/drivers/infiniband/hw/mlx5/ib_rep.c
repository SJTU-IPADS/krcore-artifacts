// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2018 Mellanox Technologies. All rights reserved.
 */

#include <linux/mlx5/vport.h>
#include "ib_rep.h"
#include "srq.h"

static int
mlx5_ib_set_vport_rep(struct mlx5_core_dev *dev,
		      struct mlx5_eswitch_rep *rep,
		      int vport_index)
{
	struct mlx5_ib_dev *ibdev;

	ibdev = mlx5_ib_get_uplink_ibdev(dev->priv.eswitch);
	if (!ibdev)
		return -EINVAL;

	ibdev->port[vport_index].rep = rep;
	rep->rep_data[REP_IB].priv = ibdev;
	write_lock(&ibdev->port[vport_index].roce.netdev_lock);
	ibdev->port[vport_index].roce.netdev =
		mlx5_ib_get_rep_netdev(rep->esw, rep->vport);
	write_unlock(&ibdev->port[vport_index].roce.netdev_lock);

	return 0;
}

static void mlx5_ib_register_peer_vport_reps(struct mlx5_core_dev *mdev);

static int
mlx5_ib_vport_rep_load(struct mlx5_core_dev *dev, struct mlx5_eswitch_rep *rep)
{
	int num_ports = mlx5_eswitch_get_total_vports(dev);
	const struct mlx5_ib_profile *profile;
	struct mlx5_core_dev *peer_dev;
	struct mlx5_ib_dev *ibdev;
	int vport_index;

	vport_index = rep->vport_index;

	if (mlx5_lag_is_shared_fdb(dev)) {
		peer_dev = mlx5_lag_get_peer_mdev(dev);
		if (mlx5_lag_is_master(dev)) {
			num_ports += mlx5_eswitch_get_total_vports(peer_dev) - 1;
			if (num_ports > 255)
				return -EINVAL;
		} else {
			if (rep->vport == MLX5_VPORT_UPLINK)
				return 0;
			vport_index += mlx5_eswitch_get_total_vports(peer_dev);
			dev = peer_dev;
		}
	}
	if (rep->vport == MLX5_VPORT_UPLINK)
		profile = &ib_profile;
	else
		return mlx5_ib_set_vport_rep(dev, rep, vport_index);

	ibdev = ib_alloc_device(mlx5_ib_dev, ib_dev);
	if (!ibdev)
		return -ENOMEM;

	ibdev->port = kcalloc(num_ports, sizeof(*ibdev->port),
			      GFP_KERNEL);
	if (!ibdev->port) {
		ib_dealloc_device(&ibdev->ib_dev);
		return -ENOMEM;
	}

	ibdev->is_rep = true;
	ibdev->port[vport_index].rep = rep;
	ibdev->port[vport_index].roce.netdev =
		mlx5_ib_get_rep_netdev(dev->priv.eswitch, rep->vport);
	ibdev->mdev = dev;
	ibdev->num_ports = num_ports;

	if (!__mlx5_ib_add(ibdev, profile))
		return -EINVAL;

	rep->rep_data[REP_IB].priv = ibdev;
	/* Only master can reach this point */
	if (mlx5_lag_is_shared_fdb(dev))
		mlx5_ib_register_peer_vport_reps(dev);

	return 0;
}

static void
mlx5_ib_vport_rep_unload(struct mlx5_eswitch_rep *rep)
{
	struct mlx5_core_dev *mdev = mlx5_eswitch_get_core_dev(rep->esw);
	struct mlx5_ib_dev *dev = mlx5_ib_rep_to_dev(rep);
	struct mlx5_ib_port *port;
	int i;

	if (mlx5_lag_is_shared_fdb(mdev) &&
	    !mlx5_lag_is_master(mdev) &&
	    rep->vport == MLX5_VPORT_UPLINK)
		return;

	if (!dev)
		return;

	for (i = 0; i < dev->num_ports; i++) {
		port = &dev->port[i];
		if (port->rep == rep) {
			write_lock(&port->roce.netdev_lock);
			port->roce.netdev = NULL;
			write_unlock(&port->roce.netdev_lock);
			rep->rep_data[REP_IB].priv = NULL;
			port->rep = NULL;
			break;
		}
	}

	if (rep->vport == MLX5_VPORT_UPLINK) {
		struct mlx5_core_dev *peer_mdev;

		if (mlx5_lag_is_shared_fdb(mdev)) {
			peer_mdev = mlx5_lag_get_peer_mdev(mdev);
			mlx5_ib_unregister_vport_reps(peer_mdev);
		}
		__mlx5_ib_remove(dev, dev->profile, MLX5_IB_STAGE_MAX);
	}
}

static void *mlx5_ib_vport_get_proto_dev(struct mlx5_eswitch_rep *rep)
{
	return mlx5_ib_rep_to_dev(rep);
}

static const struct mlx5_eswitch_rep_ops rep_ops = {
	.load = mlx5_ib_vport_rep_load,
	.unload = mlx5_ib_vport_rep_unload,
	.get_proto_dev = mlx5_ib_vport_get_proto_dev,
};

static void mlx5_ib_register_peer_vport_reps(struct mlx5_core_dev *mdev)
{
	struct mlx5_core_dev *peer_mdev = mlx5_lag_get_peer_mdev(mdev);
	struct mlx5_eswitch *esw;

	if (!peer_mdev)
		return;

	esw = peer_mdev->priv.eswitch;
	mlx5_eswitch_register_vport_reps(esw, &rep_ops, REP_IB);
}

void mlx5_ib_register_vport_reps(struct mlx5_core_dev *mdev)
{
	struct mlx5_eswitch *esw = mdev->priv.eswitch;

	mlx5_eswitch_register_vport_reps(esw, &rep_ops, REP_IB);
}

void mlx5_ib_unregister_vport_reps(struct mlx5_core_dev *mdev)
{
	struct mlx5_eswitch *esw = mdev->priv.eswitch;

	if (mlx5_lag_is_shared_fdb(mdev)) {
		struct mlx5_core_dev *peer_dev;

		peer_dev = mlx5_lag_get_peer_mdev(mdev);
		mlx5_eswitch_unregister_vport_reps(peer_dev->priv.eswitch,
						   REP_IB);
	}

	mlx5_eswitch_unregister_vport_reps(esw, REP_IB);
}

u8 mlx5_ib_eswitch_mode(struct mlx5_eswitch *esw)
{
	return mlx5_eswitch_mode(esw);
}

struct mlx5_ib_dev *mlx5_ib_get_rep_ibdev(struct mlx5_eswitch *esw,
					  u16 vport_num)
{
	return mlx5_eswitch_get_proto_dev(esw, vport_num, REP_IB);
}

struct net_device *mlx5_ib_get_rep_netdev(struct mlx5_eswitch *esw,
					  u16 vport_num)
{
	return mlx5_eswitch_get_proto_dev(esw, vport_num, REP_ETH);
}

struct mlx5_ib_dev *mlx5_ib_get_uplink_ibdev(struct mlx5_eswitch *esw)
{
	return mlx5_eswitch_uplink_get_proto_dev(esw, REP_IB);
}

struct mlx5_eswitch_rep *mlx5_ib_vport_rep(struct mlx5_eswitch *esw,
					   u16 vport_num)
{
	return mlx5_eswitch_vport_rep(esw, vport_num);
}

u32 mlx5_ib_eswitch_vport_match_metadata_enabled(struct mlx5_eswitch *esw)
{
	return mlx5_eswitch_vport_match_metadata_enabled(esw);
}

u32 mlx5_ib_eswitch_get_vport_metadata_for_match(struct mlx5_eswitch *esw,
						 u16 vport)
{
	return mlx5_eswitch_get_vport_metadata_for_match(esw, vport);
}

u32 mlx5_ib_eswitch_get_vport_metadata_mask(void)
{
	return mlx5_eswitch_get_vport_metadata_mask();
}

struct mlx5_flow_handle *create_flow_rule_vport_sq(struct mlx5_ib_dev *dev,
						   struct mlx5_ib_sq *sq,
						   u16 port)
{
	struct mlx5_eswitch *esw = dev->mdev->priv.eswitch;
	struct mlx5_eswitch_rep *rep;

	if (!dev->is_rep || !port)
		return NULL;

	if (!dev->port[port - 1].rep)
		return ERR_PTR(-EINVAL);

	rep = dev->port[port - 1].rep;

	return mlx5_eswitch_add_send_to_vport_rule(esw, esw, rep,
						   sq->base.mqp.qpn);
}
