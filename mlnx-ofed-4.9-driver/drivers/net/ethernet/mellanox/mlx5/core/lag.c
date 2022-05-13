/*
 * Copyright (c) 2016, Mellanox Technologies. All rights reserved.
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

#include <linux/netdevice.h>
#include <linux/mlx5/driver.h>
#include <linux/mlx5/vport.h>
#include <net/bonding.h>
#include "lib/devcom.h"
#include "mlx5_core.h"
#include "eswitch.h"
#include "lag.h"
#include "lag_mp.h"

/* General purpose, use for short periods of time.
 * Beware of lock dependencies (preferably, no locks should be acquired
 * under it).
 */
static DEFINE_MUTEX(lag_mutex);

static int mlx5_cmd_create_lag(struct mlx5_core_dev *dev, u8 remap_port1,
			       u8 remap_port2,
			       bool shared_fdb)
{
	u32   in[MLX5_ST_SZ_DW(create_lag_in)]   = {0};
	u32   out[MLX5_ST_SZ_DW(create_lag_out)] = {0};
	void *lag_ctx = MLX5_ADDR_OF(create_lag_in, in, ctx);

	MLX5_SET(create_lag_in, in, opcode, MLX5_CMD_OP_CREATE_LAG);

	MLX5_SET(lagc, lag_ctx, tx_remap_affinity_1, remap_port1);
	MLX5_SET(lagc, lag_ctx, tx_remap_affinity_2, remap_port2);
	MLX5_SET(lagc, lag_ctx, fdb_selection_mode, shared_fdb);

	return mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
}

static int mlx5_cmd_modify_lag(struct mlx5_core_dev *dev, u8 remap_port1,
			       u8 remap_port2)
{
	u32   in[MLX5_ST_SZ_DW(modify_lag_in)]   = {0};
	u32   out[MLX5_ST_SZ_DW(modify_lag_out)] = {0};
	void *lag_ctx = MLX5_ADDR_OF(modify_lag_in, in, ctx);

	MLX5_SET(modify_lag_in, in, opcode, MLX5_CMD_OP_MODIFY_LAG);
	MLX5_SET(modify_lag_in, in, field_select, 0x1);

	MLX5_SET(lagc, lag_ctx, tx_remap_affinity_1, remap_port1);
	MLX5_SET(lagc, lag_ctx, tx_remap_affinity_2, remap_port2);

	return mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
}

static int mlx5_cmd_destroy_lag(struct mlx5_core_dev *dev)
{
	u32  in[MLX5_ST_SZ_DW(destroy_lag_in)]  = {0};
	u32 out[MLX5_ST_SZ_DW(destroy_lag_out)] = {0};

	MLX5_SET(destroy_lag_in, in, opcode, MLX5_CMD_OP_DESTROY_LAG);

	return mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
}

int mlx5_cmd_create_vport_lag(struct mlx5_core_dev *dev)
{
	u32  in[MLX5_ST_SZ_DW(create_vport_lag_in)]  = {0};
	u32 out[MLX5_ST_SZ_DW(create_vport_lag_out)] = {0};

	MLX5_SET(create_vport_lag_in, in, opcode, MLX5_CMD_OP_CREATE_VPORT_LAG);

	return mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
}
EXPORT_SYMBOL(mlx5_cmd_create_vport_lag);

int mlx5_cmd_destroy_vport_lag(struct mlx5_core_dev *dev)
{
	u32  in[MLX5_ST_SZ_DW(destroy_vport_lag_in)]  = {0};
	u32 out[MLX5_ST_SZ_DW(destroy_vport_lag_out)] = {0};

	MLX5_SET(destroy_vport_lag_in, in, opcode, MLX5_CMD_OP_DESTROY_VPORT_LAG);

	return mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
}
EXPORT_SYMBOL(mlx5_cmd_destroy_vport_lag);

static int mlx5_cmd_query_cong_counter(struct mlx5_core_dev *dev,
				       bool reset, void *out, int out_size)
{
	u32 in[MLX5_ST_SZ_DW(query_cong_statistics_in)] = { };

	MLX5_SET(query_cong_statistics_in, in, opcode,
		 MLX5_CMD_OP_QUERY_CONG_STATISTICS);
	MLX5_SET(query_cong_statistics_in, in, clear, reset);
	return mlx5_cmd_exec(dev, in, sizeof(in), out, out_size);
}

int mlx5_lag_dev_get_netdev_idx(struct mlx5_lag *ldev,
				struct net_device *ndev)
{
	int i;

	for (i = 0; i < MLX5_MAX_PORTS; i++)
		if (ldev->pf[i].netdev == ndev)
			return i;

	return -1;
}

static bool __mlx5_lag_is_roce(struct mlx5_lag *ldev)
{
	return !!(ldev->flags & MLX5_LAG_FLAG_ROCE);
}

static bool __mlx5_lag_is_sriov(struct mlx5_lag *ldev)
{
	return !!(ldev->flags & MLX5_LAG_FLAG_SRIOV);
}

static void mlx5_infer_tx_affinity_mapping(struct lag_tracker *tracker,
					   u8 *port1, u8 *port2)
{
	*port1 = 1;
	*port2 = 2;
	if (!tracker->netdev_state[0].tx_enabled ||
	    !tracker->netdev_state[0].link_up) {
		*port1 = 2;
		return;
	}

	if (!tracker->netdev_state[1].tx_enabled ||
	    !tracker->netdev_state[1].link_up)
		*port2 = 1;
}

void mlx5_modify_lag(struct mlx5_lag *ldev,
		     struct lag_tracker *tracker)
{
	struct mlx5_core_dev *dev0 = ldev->pf[0].dev;
	u8 v2p_port1, v2p_port2;
	int err;

	mlx5_infer_tx_affinity_mapping(tracker, &v2p_port1,
				       &v2p_port2);

	if (v2p_port1 != ldev->v2p_map[0] ||
	    v2p_port2 != ldev->v2p_map[1]) {
		ldev->v2p_map[0] = v2p_port1;
		ldev->v2p_map[1] = v2p_port2;

		mlx5_core_info(dev0, "modify lag map port 1:%d port 2:%d",
			       ldev->v2p_map[0], ldev->v2p_map[1]);

		err = mlx5_cmd_modify_lag(dev0, v2p_port1, v2p_port2);
		if (err)
			mlx5_core_err(dev0,
				      "Failed to modify LAG (%d)\n",
				      err);
	}
}

static int mlx5_create_lag(struct mlx5_lag *ldev,
			   struct lag_tracker *tracker,
			   bool shared_fdb)
{
	struct mlx5_core_dev *dev0 = ldev->pf[0].dev;
	int err;

	mlx5_infer_tx_affinity_mapping(tracker, &ldev->v2p_map[0],
				       &ldev->v2p_map[1]);

	mlx5_core_info(dev0, "lag map port 1:%d port 2:%d shared_fdb(%d)",
		       ldev->v2p_map[0], ldev->v2p_map[1], shared_fdb);

	err = mlx5_cmd_create_lag(dev0, ldev->v2p_map[0], ldev->v2p_map[1],
				  shared_fdb);
	if (err) {
		mlx5_core_err(dev0,
			      "Failed to create LAG (%d)\n",
			      err);
		return err;
	}

	if (shared_fdb) {
		err = esw_offloads_config_single_fdb(ldev->pf[0].dev->priv.eswitch,
						     ldev->pf[1].dev->priv.eswitch);
		if (err)
			mlx5_core_err(dev0, "Can't enable single FDB mode\n");
		else
			mlx5_core_info(dev0, "Operation mode is single FDB\n");
	}

	if (err) {
		if (mlx5_cmd_destroy_lag(dev0))
			mlx5_core_err(dev0,
				      "Failed to deactivate RoCE LAG; driver restart required\n");
	}

	return err;
}

int mlx5_activate_lag(struct mlx5_lag *ldev,
		      struct lag_tracker *tracker,
		      u8 flags,
		      bool shared_fdb)
{
	bool roce_lag = !!(flags & MLX5_LAG_FLAG_ROCE);
	struct mlx5_core_dev *dev0 = ldev->pf[0].dev;
	int err;

	err = mlx5_create_lag(ldev, tracker, shared_fdb);
	if (err) {
		if (roce_lag) {
			mlx5_core_err(dev0,
				      "Failed to activate RoCE LAG\n");
		} else {
			mlx5_core_err(dev0,
				      "Failed to activate VF LAG\n"
				      "Make sure all VFs are unbound prior to VF LAG activation or deactivation\n");
		}
		return err;
	}

	ldev->flags |= flags;
	ldev->shared_fdb = shared_fdb;

	return 0;
}

static int mlx5_deactivate_lag(struct mlx5_lag *ldev)
{
	struct mlx5_core_dev *dev0 = ldev->pf[0].dev;
	bool roce_lag = __mlx5_lag_is_roce(ldev);
	int err;

	ldev->flags &= ~MLX5_LAG_MODE_FLAGS;
	if (ldev->shared_fdb) {
		esw_offloads_destroy_single_fdb(ldev->pf[0].dev->priv.eswitch,
						ldev->pf[1].dev->priv.eswitch);
		ldev->shared_fdb = false;
	}

	err = mlx5_cmd_destroy_lag(dev0);
	if (err) {
		if (roce_lag) {
			mlx5_core_err(dev0,
				      "Failed to deactivate RoCE LAG; driver restart required\n");
		} else {
			mlx5_core_err(dev0,
				      "Failed to deactivate VF LAG; driver restart required\n"
				      "Make sure all VFs are unbound prior to VF LAG activation or deactivation\n");
		}
	}

	return err;
}

static bool mlx5_lag_check_prereq(struct mlx5_lag *ldev)
{
	struct mlx5_core_dev *dev0 = ldev->pf[0].dev;
	struct mlx5_core_dev *dev1 = ldev->pf[1].dev;
	bool roce_lag_allowed;

	if (!dev0 || !dev1)
		return false;
       roce_lag_allowed = !mlx5_sriov_is_enabled(dev0) &&
			  !mlx5_sriov_is_enabled(dev1);

#ifdef CONFIG_MLX5_ESWITCH
	roce_lag_allowed &= dev0->priv.eswitch->mode == SRIOV_NONE &&
		dev1->priv.eswitch->mode == SRIOV_NONE;
#endif

	if (roce_lag_allowed)
		return !dev0->priv.lag_disabled && !dev1->priv.lag_disabled;

#ifdef CONFIG_MLX5_ESWITCH
	return mlx5_esw_lag_prereq(dev0, dev1);
#else
	return (!mlx5_sriov_is_enabled(dev0) &&
		!mlx5_sriov_is_enabled(dev1));
#endif
}

static void mlx5_lag_add_ib_devices(struct mlx5_lag *ldev)
{
	int i;

	for (i = 0; i < MLX5_MAX_PORTS; i++)
		if (ldev->pf[i].dev)
			mlx5_add_dev_by_protocol(ldev->pf[i].dev,
						 MLX5_INTERFACE_PROTOCOL_IB);
}

static void mlx5_lag_reload_ib_reps_not_device(struct mlx5_lag *ldev,
					       struct mlx5_core_dev *dev)
{
	int i;

	for (i = 0; i < MLX5_MAX_PORTS; i++)
		if (ldev->pf[i].dev &&
		    ldev->pf[i].dev != dev)
			esw_offloads_reload_reps(ldev->pf[i].dev->priv.eswitch);
}

static void mlx5_lag_remove_ib_devices(struct mlx5_lag *ldev)
{
	int i;

	for (i = 0; i < MLX5_MAX_PORTS; i++)
		if (ldev->pf[i].dev)
			mlx5_remove_dev_by_protocol(ldev->pf[i].dev,
						    MLX5_INTERFACE_PROTOCOL_IB);
}

static bool mlx5_shared_fdb_supported(struct mlx5_lag *ldev)
{
	struct mlx5_core_dev *dev0 = ldev->pf[0].dev;
	struct mlx5_core_dev *dev1 = ldev->pf[1].dev;

	if (mlx5_sriov_is_enabled(dev0) &&
	    mlx5_sriov_is_enabled(dev1) &&
	    mlx5_eswitch_vport_match_metadata_enabled(dev0->priv.eswitch) &&
	    mlx5_eswitch_vport_match_metadata_enabled(dev1->priv.eswitch) &&
	    mlx5_devcom_is_paired(dev0->priv.devcom,
				  MLX5_DEVCOM_ESW_OFFLOADS) &&
	    MLX5_CAP_GEN(dev1, lag_native_fdb_selection) &&
	    MLX5_CAP_ESW(dev1, root_ft_on_other_esw) &&
	    MLX5_CAP_ESW(dev0, esw_shared_ingress_acl) &&
	    (dev0->priv.steering->mode == MLX5_FLOW_STEERING_MODE_DMFS) &&
	    (dev1->priv.steering->mode == MLX5_FLOW_STEERING_MODE_DMFS))
		return true;

	return false;
}

static void mlx5_do_bond(struct mlx5_lag *ldev)
{
	struct mlx5_core_dev *dev0 = ldev->pf[0].dev;
	struct mlx5_core_dev *dev1 = ldev->pf[1].dev;
	struct lag_tracker tracker;
	bool do_bond, roce_lag;
	int err;

	if (!dev0 || !dev1)
		return;

	mutex_lock(&lag_mutex);
	tracker = ldev->tracker;
	mutex_unlock(&lag_mutex);

	do_bond = tracker.is_bonded &&
		ldev->pf[0].netdev == tracker.ndev[0] &&
		ldev->pf[1].netdev == tracker.ndev[1] &&
		mlx5_lag_check_prereq(ldev);

	if (do_bond && !__mlx5_lag_is_active(ldev)) {
		bool shared_fdb = mlx5_shared_fdb_supported(ldev);

		roce_lag = !mlx5_sriov_is_enabled(dev0);
#ifdef CONFIG_MLX5_ESWITCH
		roce_lag &= dev0->priv.eswitch->mode == MLX5_ESWITCH_NONE &&
			    dev1->priv.eswitch->mode == MLX5_ESWITCH_NONE;
#endif
		if (shared_fdb || roce_lag)
			mlx5_lag_remove_ib_devices(ldev);

		err = mlx5_activate_lag(ldev, &tracker,
					roce_lag ?
					MLX5_LAG_FLAG_ROCE :
					MLX5_LAG_FLAG_SRIOV,
					shared_fdb);
		if (err) {
			if (shared_fdb || roce_lag)
				mlx5_lag_add_ib_devices(ldev);
		} else if (shared_fdb) {
			mlx5_add_dev_by_protocol(ldev->pf[0].dev,
						 MLX5_INTERFACE_PROTOCOL_IB);

			err = esw_offloads_load_all_reps(ldev->pf[0].dev->priv.eswitch);
			if (!err)
				err = esw_offloads_load_all_reps(ldev->pf[1].dev->priv.eswitch);

			if (err) {
				mlx5_remove_dev_by_protocol(ldev->pf[0].dev,
							    MLX5_INTERFACE_PROTOCOL_IB);
				mlx5_deactivate_lag(ldev);
				mlx5_lag_add_ib_devices(ldev);
				esw_offloads_reload_reps(ldev->pf[0].dev->priv.eswitch);
				esw_offloads_reload_reps(ldev->pf[1].dev->priv.eswitch);
				mlx5_core_err(dev0, "Failed to enable lag\n");
				return;
			}
		} else if (roce_lag) {
			mlx5_add_dev_by_protocol(dev0, MLX5_INTERFACE_PROTOCOL_IB);
			mlx5_nic_vport_enable_roce(dev1);
		}
	} else if (do_bond && __mlx5_lag_is_active(ldev)) {
		mlx5_modify_lag(ldev, &tracker);
	} else if (!do_bond && __mlx5_lag_is_active(ldev)) {
		bool shared_fdb = ldev->shared_fdb;

		roce_lag = __mlx5_lag_is_roce(ldev);

		mlx5_lag_remove_ib_devices(ldev);
		if (!shared_fdb && roce_lag)
			mlx5_nic_vport_disable_roce(dev1);

		err = mlx5_deactivate_lag(ldev);
		if (err)
			return;

		mlx5_lag_add_ib_devices(ldev);
		if (shared_fdb) {
			esw_offloads_reload_reps(dev0->priv.eswitch);
			esw_offloads_reload_reps(dev1->priv.eswitch);
		}
	}
}

static void mlx5_queue_bond_work(struct mlx5_lag *ldev, unsigned long delay)
{
	queue_delayed_work(ldev->wq, &ldev->bond_work, delay);
}

static void mlx5_do_bond_work(struct work_struct *work)
{
	struct delayed_work *delayed_work = to_delayed_work(work);
	struct mlx5_lag *ldev = container_of(delayed_work, struct mlx5_lag,
					     bond_work);
	int status;

	status = mlx5_dev_list_trylock();
	if (!status) {
queue:
		/* 1 sec delay. */
		mlx5_queue_bond_work(ldev, HZ/2);
		return;
	}
	if (ldev->esw_updating) {
		mlx5_dev_list_unlock();
		goto queue;
	}

	mlx5_do_bond(ldev);
	mlx5_dev_list_unlock();
}

static bool mlx5_lag_eval_bonding_conds(struct mlx5_lag *ldev,
					struct lag_tracker *tracker,
					struct net_device *upper,
					enum netdev_lag_tx_type tx_type)
{
	int bond_status = 0, num_slaves = 0, idx;
	struct net_device *ndev_tmp;
	bool is_bonded;

	rcu_read_lock();
	for_each_netdev_in_bond_rcu(upper, ndev_tmp) {
		idx = mlx5_lag_dev_get_netdev_idx(ldev, ndev_tmp);
		if (idx > -1)
			bond_status |= (1 << idx);

		num_slaves++;
	}
	rcu_read_unlock();

	/* None of this lagdev's netdevs are slaves of this master. */
	if (!(bond_status & 0x3))
		return false;

	tracker->tx_type = tx_type;

	/* Determine bonding status:
	 * A device is considered bonded if both its physical ports are slaves
	 * of the same lag master, and only them.
	 * Lag mode must be activebackup or hash.
	 */
	is_bonded = (num_slaves == MLX5_MAX_PORTS) &&
		    (bond_status == 0x3) &&
		    ((tracker->tx_type == NETDEV_LAG_TX_TYPE_ACTIVEBACKUP) ||
		     (tracker->tx_type == NETDEV_LAG_TX_TYPE_HASH));

	if (tracker->is_bonded != is_bonded) {
		tracker->is_bonded = is_bonded;
		return true;
	}

	return false;
}

static bool mlx5_handle_changeupper_event(struct mlx5_lag *ldev,
					  struct lag_tracker *tracker,
					  struct net_device *ndev,
					  struct netdev_notifier_changeupper_info *info)
{
	enum netdev_lag_tx_type tx_type = NETDEV_LAG_TX_TYPE_UNKNOWN;
	struct netdev_lag_upper_info *lag_upper_info;
	struct net_device *upper = info->upper_dev;

	if (!netif_is_lag_master(upper))
		return false;

	if (info->linking) {
		lag_upper_info = info->upper_info;

		if (lag_upper_info)
			tx_type = lag_upper_info->tx_type;
	}

	return mlx5_lag_eval_bonding_conds(ldev, tracker, upper, tx_type);
}

static bool mlx5_handle_changelowerstate_event(struct mlx5_lag *ldev,
					       struct lag_tracker *tracker,
					       struct net_device *ndev,
					       struct netdev_notifier_changelowerstate_info *info)
{
	struct netdev_lag_lower_state_info *lag_lower_info;
	int idx;

	if (!netif_is_lag_port(ndev))
		return 0;

	idx = mlx5_lag_dev_get_netdev_idx(ldev, ndev);
	if (idx == -1)
		return 0;

	/* This information is used to determine virtual to physical
	 * port mapping.
	 */
	lag_lower_info = info->lower_state_info;
	if (!lag_lower_info)
		return 0;

	tracker->netdev_state[idx] = *lag_lower_info;

	return 1;
}

static int mlx5_lag_netdev_event(struct notifier_block *this,
				 unsigned long event, void *ptr)
{
	struct net_device *ndev = netdev_notifier_info_to_dev(ptr);
	struct lag_tracker tracker;
	struct mlx5_lag *ldev;
	bool changed = 0;

	if (!net_eq(dev_net(ndev), &init_net))
		return NOTIFY_DONE;

	if ((event != NETDEV_CHANGEUPPER) && (event != NETDEV_CHANGELOWERSTATE))
		return NOTIFY_DONE;

	ldev    = container_of(this, struct mlx5_lag, nb);
	tracker = ldev->tracker;

	switch (event) {
	case NETDEV_CHANGEUPPER:
		changed = mlx5_handle_changeupper_event(ldev, &tracker, ndev,
							ptr);
		break;
	case NETDEV_CHANGELOWERSTATE:
		changed = mlx5_handle_changelowerstate_event(ldev, &tracker,
							     ndev, ptr);
		break;
	}

	mutex_lock(&lag_mutex);
	ldev->tracker = tracker;
	ldev->tracker.ndev[0] = ldev->pf[0].netdev;
	ldev->tracker.ndev[1] = ldev->pf[1].netdev;
	mutex_unlock(&lag_mutex);

	if (changed)
		mlx5_queue_bond_work(ldev, 0);

	return NOTIFY_DONE;
}

static struct mlx5_lag *mlx5_lag_dev_alloc(void)
{
	struct mlx5_lag *ldev;

	ldev = kzalloc(sizeof(*ldev), GFP_KERNEL);
	if (!ldev)
		return NULL;

	ldev->wq = create_singlethread_workqueue("mlx5_lag");
	if (!ldev->wq) {
		kfree(ldev);
		return NULL;
	}

	kref_init(&ldev->ref);
	INIT_DELAYED_WORK(&ldev->bond_work, mlx5_do_bond_work);

	return ldev;
}

static void mlx5_lag_dev_free(struct kref *ref)
{
	struct mlx5_lag *ldev = container_of(ref, struct mlx5_lag, ref);

	if (ldev->nb.notifier_call)
		unregister_netdevice_notifier(&ldev->nb);
	mlx5_lag_mp_cleanup(ldev);
	cancel_delayed_work_sync(&ldev->bond_work);
	destroy_workqueue(ldev->wq);
	kfree(ldev);
}

static void mlx5_lag_dev_put(struct mlx5_lag *ldev)
{
	kref_put(&ldev->ref, mlx5_lag_dev_free);
}

static void mlx5_ldev_get(struct mlx5_lag *ldev)
{
	kref_get(&ldev->ref);
}

static void mlx5_lag_dev_add_mdev(struct mlx5_lag *ldev,
				  struct mlx5_core_dev *dev)
{
	unsigned int fn = PCI_FUNC(dev->pdev->devfn);

	if (fn >= MLX5_MAX_PORTS)
		return;

	mutex_lock(&lag_mutex);
	ldev->pf[fn].dev = dev;
	dev->priv.lag = ldev;
	mutex_unlock(&lag_mutex);
}

static void mlx5_lag_dev_add_pf(struct mlx5_lag *ldev,
				struct mlx5_core_dev *dev,
				struct net_device *netdev)
{
	unsigned int fn = PCI_FUNC(dev->pdev->devfn);

	if (fn >= MLX5_MAX_PORTS)
		return;

	mutex_lock(&lag_mutex);
	ldev->pf[fn].netdev = netdev;
	ldev->tracker.netdev_state[fn].link_up = 0;
	ldev->tracker.netdev_state[fn].tx_enabled = 0;
	mutex_unlock(&lag_mutex);
}

static void mlx5_lag_dev_remove_mdev(struct mlx5_lag *ldev,
				     struct mlx5_core_dev *dev)
{
	int i;

	for (i = 0; i < MLX5_MAX_PORTS; i++)
		if (ldev->pf[i].dev == dev)
			break;

	if (i == MLX5_MAX_PORTS)
		return;

	mutex_lock(&lag_mutex);
	ldev->pf[i].dev = NULL;
	dev->priv.lag = NULL;
	mutex_unlock(&lag_mutex);
}

static void mlx5_lag_dev_remove_pf(struct mlx5_lag *ldev,
				   struct mlx5_core_dev *dev)
{
	int i;

	for (i = 0; i < MLX5_MAX_PORTS; i++)
		if (ldev->pf[i].dev == dev)
			break;

	if (i == MLX5_MAX_PORTS)
		return;

	mutex_lock(&lag_mutex);
	ldev->pf[i].netdev = NULL;
	mutex_unlock(&lag_mutex);
}

static ssize_t mlx5_lag_show_enabled(struct device *device,
				     struct device_attribute *attr,
				     char *buf)
{
	struct pci_dev *pdev = container_of(device, struct pci_dev, dev);
	struct mlx5_core_dev *dev  = pci_get_drvdata(pdev);

	return sprintf(buf, "%d\n", !dev->priv.lag_disabled);
}

static ssize_t mlx5_lag_set_enabled(struct device *device,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct pci_dev *pdev = container_of(device, struct pci_dev, dev);
	struct mlx5_core_dev *dev  = pci_get_drvdata(pdev);
	int ret = -EINVAL;
	u32 val;

	ret = kstrtou32(buf, 0, &val);
	if (ret)
		return ret;

	if (val == 1)
		dev->priv.lag_disabled = 0;
	else if (val == 0)
		dev->priv.lag_disabled = 1;
	else
		return -EINVAL;

	mlx5_lag_update(dev);

	return ret ? ret : count;
}

static DEVICE_ATTR(roce_lag_enable, 0644, mlx5_lag_show_enabled, mlx5_lag_set_enabled);
static struct device_attribute *mlx5_lag_dev_attrs = &dev_attr_roce_lag_enable;

static void mlx5_lag_update_trackers(struct mlx5_lag *ldev)
{
	enum netdev_lag_tx_type tx_type = NETDEV_LAG_TX_TYPE_UNKNOWN;
	struct net_device *upper = NULL, *ndev;
	struct lag_tracker *tracker;
	struct bonding *bond;
	struct slave *slave;
	int i;

	rtnl_lock();
	tracker = &ldev->tracker;

	for (i = 0; i < MLX5_MAX_PORTS; i++) {
		ndev = ldev->pf[i].netdev;
		if (!ndev)
			continue;

		if (ndev->reg_state != NETREG_REGISTERED)
			continue;

		if (!netif_is_bond_slave(ndev))
			continue;

		rcu_read_lock();
		slave = bond_slave_get_rcu(ndev);
		rcu_read_unlock();
		bond = bond_get_bond_by_slave(slave);

		tracker->netdev_state[i].link_up = bond_slave_is_up(slave);
		tracker->netdev_state[i].tx_enabled = bond_slave_can_tx(slave);

		if (bond_mode_uses_xmit_hash(bond))
			tx_type = NETDEV_LAG_TX_TYPE_HASH;
		else if (BOND_MODE(bond) == BOND_MODE_ACTIVEBACKUP)
			tx_type = NETDEV_LAG_TX_TYPE_ACTIVEBACKUP;

		upper = bond->dev;
	}

	if (!upper)
		goto out;

	if (mlx5_lag_eval_bonding_conds(ldev, tracker, upper, tx_type))
		mlx5_queue_bond_work(ldev, 0);

out:
	rtnl_unlock();
}

/* Must be called with intf_mutex held */
static void __mlx5_lag_add_mdev(struct mlx5_core_dev *dev)
{
	struct mlx5_lag *ldev = NULL;
	struct mlx5_core_dev *tmp_dev;
	int err;

	if (!MLX5_CAP_GEN(dev, vport_group_manager) ||
	    !MLX5_CAP_GEN(dev, lag_master) ||
	    (MLX5_CAP_GEN(dev, num_lag_ports) != MLX5_MAX_PORTS))
		return;

	if (device_create_file(&dev->pdev->dev, mlx5_lag_dev_attrs)) {
		mlx5_core_err(dev, "Failed to create RoCE LAG sysfs\n");
		return;
	}

	tmp_dev = mlx5_get_next_phys_dev(dev);
	if (tmp_dev)
		ldev = tmp_dev->priv.lag;

	if (!ldev) {
		ldev = mlx5_lag_dev_alloc();
		if (!ldev) {
			mlx5_core_err(dev, "Failed to alloc lag dev\n");
			goto remove_file;
		}
	} else {
		mlx5_ldev_get(ldev);
	}

	mlx5_lag_dev_add_mdev(ldev, dev);

	if (!ldev->nb.notifier_call) {
		ldev->nb.notifier_call = mlx5_lag_netdev_event;
		if (register_netdevice_notifier(&ldev->nb)) {
			ldev->nb.notifier_call = NULL;
			mlx5_core_err(dev, "Failed to register LAG netdev notifier\n");
		}
	}

	err = mlx5_lag_mp_init(ldev);
	if (err)
		mlx5_core_err(dev, "Failed to init multipath lag err=%d\n",
			      err);
	return;

remove_file:
	device_remove_file(&dev->pdev->dev, mlx5_lag_dev_attrs);
}

/* Must be called with intf_mutex held */
static void __mlx5_lag_add(struct mlx5_core_dev *dev, struct net_device *netdev)
{
	struct mlx5_lag *ldev = dev->priv.lag;

	if (!MLX5_CAP_GEN(dev, vport_group_manager) ||
	    !MLX5_CAP_GEN(dev, lag_master) ||
	    (MLX5_CAP_GEN(dev, num_lag_ports) != MLX5_MAX_PORTS) ||
	    !ldev)
		return;

	mlx5_ldev_get(ldev);
	mlx5_lag_dev_add_pf(ldev, dev, netdev);

	mlx5_lag_update_trackers(ldev);
}

static void mlx5_lag_disable_lag(struct mlx5_lag *ldev,
				 struct mlx5_core_dev *dev)
{
	bool shared_fdb = ldev->shared_fdb;
	struct mlx5_core_dev *dev0, *dev1;
	bool roce_lag;

	mlx5_lag_remove_ib_devices(ldev);

	dev0 = ldev->pf[0].dev;
	dev1 = ldev->pf[1].dev;

	roce_lag = !mlx5_sriov_is_enabled(dev0);
#ifdef CONFIG_MLX5_ESWITCH
	roce_lag &= dev0->priv.eswitch->mode == MLX5_ESWITCH_NONE &&
		dev1->priv.eswitch->mode == MLX5_ESWITCH_NONE;
#endif
	if (!shared_fdb && roce_lag)
		mlx5_nic_vport_disable_roce(dev1);
	mlx5_deactivate_lag(ldev);
	mlx5_lag_add_ib_devices(ldev);
	if (shared_fdb)
		mlx5_lag_reload_ib_reps_not_device(ldev, dev);
}

/* Must be called with intf_mutex held */
static void __mlx5_lag_remove_mdev(struct mlx5_core_dev *dev)
{
	struct mlx5_lag *ldev;

	ldev = mlx5_lag_dev_get(dev);
	if (!ldev)
		return;

	device_remove_file(&dev->pdev->dev, mlx5_lag_dev_attrs);
	mlx5_lag_dev_remove_mdev(ldev, dev);
	mlx5_lag_dev_put(ldev);
}

/* Must be called with intf_mutex held */
static void __mlx5_lag_remove(struct mlx5_core_dev *dev)
{
	struct mlx5_lag *ldev;

	ldev = mlx5_lag_dev_get(dev);
	if (!ldev)
		return;

	if (__mlx5_lag_is_active(ldev))
		mlx5_lag_disable_lag(ldev, dev);

	mlx5_lag_dev_remove_pf(ldev, dev);
	mlx5_lag_dev_put(ldev);
}

void mlx5_lag_add_mdev(struct mlx5_core_dev *dev)
{
	mlx5_dev_list_lock();
	__mlx5_lag_add_mdev(dev);
	mlx5_dev_list_unlock();
}

void mlx5_lag_remove_mdev(struct mlx5_core_dev *dev)
{
	mlx5_dev_list_lock();
	__mlx5_lag_remove_mdev(dev);
	mlx5_dev_list_unlock();
}

void mlx5_lag_remove(struct mlx5_core_dev *dev, bool intf_mutex_held)
{
	if (!intf_mutex_held)
		mlx5_dev_list_lock();
	__mlx5_lag_remove(dev);
	if (!intf_mutex_held)
		mlx5_dev_list_unlock();
}

void mlx5_lag_add(struct mlx5_core_dev *dev,
		  struct net_device *netdev,
		  bool intf_mutex_held)
{
	if (!intf_mutex_held)
		mlx5_dev_list_lock();
	__mlx5_lag_add(dev, netdev);
	if (!intf_mutex_held)
		mlx5_dev_list_unlock();
}

bool mlx5_lag_is_roce(struct mlx5_core_dev *dev)
{
	struct mlx5_lag *ldev;
	bool res;

	mutex_lock(&lag_mutex);
	ldev = mlx5_lag_dev_get(dev);
	res  = ldev && __mlx5_lag_is_roce(ldev);
	mutex_unlock(&lag_mutex);

	return res;
}
EXPORT_SYMBOL(mlx5_lag_is_roce);

bool mlx5_lag_is_active(struct mlx5_core_dev *dev)
{
	struct mlx5_lag *ldev;
	bool res;

	mutex_lock(&lag_mutex);
	ldev = mlx5_lag_dev_get(dev);
	res  = ldev && __mlx5_lag_is_active(ldev);
	mutex_unlock(&lag_mutex);

	return res;
}
EXPORT_SYMBOL(mlx5_lag_is_active);

bool mlx5_lag_is_master(struct mlx5_core_dev *dev)
{
#ifndef MLX_LAG_SUPPORTED
	return false;
#else
	struct mlx5_lag *ldev;
	bool res;

	mutex_lock(&lag_mutex);
	ldev = mlx5_lag_dev_get(dev);
	res  = ldev && __mlx5_lag_is_active(ldev) &&
		dev == ldev->pf[0].dev;
	mutex_unlock(&lag_mutex);

	return res;
#endif
}
EXPORT_SYMBOL(mlx5_lag_is_master);

bool mlx5_lag_is_sriov(struct mlx5_core_dev *dev)
{
	struct mlx5_lag *ldev;
	bool res;

	mutex_lock(&lag_mutex);
	ldev = mlx5_lag_dev_get(dev);
	res  = ldev && __mlx5_lag_is_sriov(ldev);
	mutex_unlock(&lag_mutex);

	return res;
}
EXPORT_SYMBOL(mlx5_lag_is_sriov);

bool mlx5_lag_is_shared_fdb(struct mlx5_core_dev *dev)
{
	struct mlx5_lag *ldev;
	bool res;

	mutex_lock(&lag_mutex);
	ldev = mlx5_lag_dev_get(dev);
	res  = ldev && __mlx5_lag_is_sriov(ldev) && ldev->shared_fdb;
	mutex_unlock(&lag_mutex);

	return res;
}
EXPORT_SYMBOL(mlx5_lag_is_shared_fdb);

void mlx5_lag_update(struct mlx5_core_dev *dev)
{
	struct mlx5_lag *ldev;

	mlx5_dev_list_lock();
	ldev = mlx5_lag_dev_get(dev);
	if (!ldev)
		goto unlock;

	mlx5_do_bond(ldev);

unlock:
	mlx5_dev_list_unlock();
}

struct mlx5_lag *mlx5_lag_disable(struct mlx5_core_dev *dev)
{
	struct mlx5_lag *ldev;

loop:
	mlx5_dev_list_lock();
	ldev = mlx5_lag_dev_get(dev);
	if (!ldev)
		goto unlock;

	mlx5_ldev_get(ldev);
	if (ldev->esw_updating) {
		mlx5_lag_dev_put(ldev);
		mlx5_dev_list_unlock();
		msleep(500);
		goto loop;
	}
	ldev->esw_updating++;

	if (!__mlx5_lag_is_active(ldev))
		goto unlock;

	mlx5_do_bond(ldev);

unlock:
	mlx5_dev_list_unlock();
	return ldev;
}

void mlx5_lag_enable(struct mlx5_core_dev *dev,
		     struct mlx5_lag *ldev)
{
	mlx5_dev_list_lock();
	if (!ldev)
		goto unlock;

	ldev->esw_updating--;

	if (__mlx5_lag_is_active(ldev))
		goto ldev_put;

	mlx5_do_bond(ldev);

ldev_put:
	mlx5_lag_dev_put(ldev);
unlock:
	mlx5_dev_list_unlock();
}

struct net_device *mlx5_lag_get_roce_netdev(struct mlx5_core_dev *dev)
{
	struct net_device *ndev = NULL;
	struct mlx5_lag *ldev;

	mutex_lock(&lag_mutex);
	ldev = mlx5_lag_dev_get(dev);

	if (!(ldev && __mlx5_lag_is_roce(ldev)))
		goto unlock;

	if (ldev->tracker.tx_type == NETDEV_LAG_TX_TYPE_ACTIVEBACKUP) {
		ndev = ldev->tracker.netdev_state[0].tx_enabled ?
		       ldev->pf[0].netdev : ldev->pf[1].netdev;
	} else {
		ndev = ldev->pf[0].netdev;
	}
	if (ndev)
		dev_hold(ndev);

unlock:
	mutex_unlock(&lag_mutex);

	return ndev;
}
EXPORT_SYMBOL(mlx5_lag_get_roce_netdev);

bool mlx5_lag_intf_add(struct mlx5_interface *intf, struct mlx5_priv *priv)
{
	struct mlx5_core_dev *dev = container_of(priv, struct mlx5_core_dev,
						 priv);
	struct mlx5_lag *ldev;

	if (intf->protocol != MLX5_INTERFACE_PROTOCOL_IB)
		return true;

	ldev = mlx5_lag_dev_get(dev);
	if (!ldev || !__mlx5_lag_is_roce(ldev) || ldev->pf[0].dev == dev)
		return true;

	/* If bonded, we do not add an IB device for PF1. */
	return false;
}

int mlx5_lag_query_cong_counters(struct mlx5_core_dev *dev,
				 u64 *values,
				 int num_counters,
				 size_t *offsets)
{
	int outlen = MLX5_ST_SZ_BYTES(query_cong_statistics_out);
	struct mlx5_core_dev *mdev[MLX5_MAX_PORTS];
	struct mlx5_lag *ldev;
	int num_ports;
	int ret, i, j;
	void *out;

	out = kvzalloc(outlen, GFP_KERNEL);
	if (!out)
		return -ENOMEM;

	memset(values, 0, sizeof(*values) * num_counters);

	mutex_lock(&lag_mutex);
	ldev = mlx5_lag_dev_get(dev);
	if (ldev && __mlx5_lag_is_active(ldev)) {
		num_ports = MLX5_MAX_PORTS;
		mdev[0] = ldev->pf[0].dev;
		mdev[1] = ldev->pf[1].dev;
	} else {
		num_ports = 1;
		mdev[0] = dev;
	}

	for (i = 0; i < num_ports; ++i) {
		ret = mlx5_cmd_query_cong_counter(mdev[i], false, out, outlen);
		if (ret)
			goto unlock;

		for (j = 0; j < num_counters; ++j)
			values[j] += be64_to_cpup((__be64 *)(out + offsets[j]));
	}

unlock:
	mutex_unlock(&lag_mutex);
	kvfree(out);
	return ret;
}
EXPORT_SYMBOL(mlx5_lag_query_cong_counters);

static int mlx5_cmd_modify_cong_params(struct mlx5_core_dev *dev,
				       void *in, int in_size)
{
	u32 out[MLX5_ST_SZ_DW(modify_cong_params_out)] = { };

	return mlx5_cmd_exec(dev, in, in_size, out, sizeof(out));
}

struct mlx5_core_dev *mlx5_lag_get_peer_mdev(struct mlx5_core_dev *dev)
{
	struct mlx5_core_dev *peer_dev = NULL;
	struct mlx5_lag *ldev;

	mutex_lock(&lag_mutex);
	ldev = mlx5_lag_dev_get(dev);
	if (!ldev)
		goto unlock;

	peer_dev = ldev->pf[0].dev == dev ? ldev->pf[1].dev : ldev->pf[0].dev;

unlock:
	mutex_unlock(&lag_mutex);
	return peer_dev;
}

EXPORT_SYMBOL(mlx5_lag_get_peer_mdev);

int mlx5_lag_modify_cong_params(struct mlx5_core_dev *dev,
				void *in, int in_size)
{
	struct mlx5_core_dev *mdev[MLX5_MAX_PORTS];
	struct mlx5_lag *ldev;
	int num_ports;
	int ret;
	int i;

	mutex_lock(&lag_mutex);
	ldev = mlx5_lag_dev_get(dev);
	if (ldev && __mlx5_lag_is_active(ldev)) {
		num_ports = MLX5_MAX_PORTS;
		mdev[0] = ldev->pf[0].dev;
		mdev[1] = ldev->pf[1].dev;
	} else {
		num_ports = 1;
		mdev[0] = dev;
	}

	for (i = 0; i < num_ports; i++) {
		ret = mlx5_cmd_modify_cong_params(mdev[i], in, in_size);
		if (ret)
			goto unlock;
	}

unlock:
	mutex_unlock(&lag_mutex);
	return ret;
}
EXPORT_SYMBOL(mlx5_lag_modify_cong_params);
