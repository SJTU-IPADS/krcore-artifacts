// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2019 Mellanox Technologies

#include <linux/netdevice.h>
#include <linux/kref.h>
#include <linux/list.h>

#include "eswitch.h"
#include "en_rep.h"

struct mlx5e_rep_bond_slave_entry {
   struct list_head list;
   struct net_device *netdev;
};

struct mlx5e_rep_bond_metadata {
	struct list_head list; /* link to global list of rep_bond_metadata */
	struct mlx5_eswitch *esw;
	struct net_device *lag_dev;
	u32 metadata_reg_c_0;

	struct list_head slaves_list; /* slaves list */
	int slaves;
};

static struct mlx5e_rep_bond_metadata *
mlx5e_lookup_master_rep_bond_metadata(struct mlx5_eswitch *esw,
				      const struct net_device *lag_dev)
{
	struct mlx5e_rep_bond_metadata *cur, *found = NULL;

	list_for_each_entry(cur, &esw->offloads.rep_bond_metadata_list, list) {
		if (cur->lag_dev == lag_dev) {
			found = cur;
			break;
		}
	}

	return found;
}

static struct mlx5e_rep_bond_slave_entry *
mlx5e_lookup_rep_bond_slave_entry(const struct mlx5e_rep_bond_metadata *mdata,
				  const struct net_device *netdev)
{
	struct mlx5e_rep_bond_slave_entry *cur, *found = NULL;

	list_for_each_entry(cur, &mdata->slaves_list, list) {
		if (cur->netdev == netdev) {
			found = cur;
			break;
		}
	}

	return found;
}

static void mlx5e_rep_bond_metadata_release(struct mlx5e_rep_bond_metadata *mdata)
{
	list_del(&mdata->list);
	esw_free_unique_match_id(GEN_MATCH_ID(mdata->metadata_reg_c_0));
	WARN_ON(!list_empty(&mdata->slaves_list));
	kfree(mdata);
}

/* This must be called under rntl_lock */
int mlx5e_enslave_rep(struct mlx5_eswitch *esw, struct net_device *netdev,
		      struct net_device *lag_dev)
{
	struct mlx5e_rep_bond_slave_entry *s_entry;
	struct mlx5e_rep_bond_metadata *mdata;
	struct mlx5e_rep_priv *rpriv;
	struct mlx5e_priv *priv;
	u16 match_id;

	ASSERT_RTNL();

	mdata = mlx5e_lookup_master_rep_bond_metadata(esw, lag_dev);
	if (!mdata) {
		mdata = kzalloc(sizeof(*mdata), GFP_KERNEL);
		if (!mdata)
			return -ENOMEM;

		mdata->lag_dev = lag_dev;
		mdata->esw = esw;
		INIT_LIST_HEAD(&mdata->slaves_list);
		match_id = esw_get_unique_match_id();
		if (match_id < 0) {
			kfree(mdata);
			return -ENOSPC;
		}
		mdata->metadata_reg_c_0 = GEN_METADATA(match_id);
		list_add(&mdata->list, &esw->offloads.rep_bond_metadata_list);
		esw_debug(esw->dev,
			  "added rep_bond_metadata for lag_dev(%s) metadata(0x%x)\n",
			  lag_dev->name, mdata->metadata_reg_c_0);
	}

	s_entry = kzalloc(sizeof(*s_entry), GFP_KERNEL);
	if (!s_entry) {
		if (!mdata->slaves)
			mlx5e_rep_bond_metadata_release(mdata);
		return -ENOMEM;
	}

	s_entry->netdev = netdev;
	priv = netdev_priv(netdev);
	rpriv = priv->ppriv;
	mdata->slaves++;
	list_add_tail(&s_entry->list, &mdata->slaves_list);
	esw_replace_ingress_match_metadata(esw, rpriv->rep->vport,
					   mdata->metadata_reg_c_0);
	mlx5e_replace_rep_vport_rx_rule_metadata(netdev);
	esw_debug(esw->dev,
		  "Slave rep(%s) vport(%d) of lag_dev(%s) metadata(0x%x)\n",
		  s_entry->netdev->name, rpriv->rep->vport,
		  lag_dev->name, mdata->metadata_reg_c_0);

	return 0;
}

/* This must be called under rtnl_lock */
void mlx5e_unslave_rep(struct mlx5_eswitch *esw, const struct net_device *netdev,
		       const struct net_device *lag_dev)
{
	struct mlx5e_rep_bond_slave_entry *s_entry;
	struct mlx5e_rep_bond_metadata *mdata;
	struct mlx5e_rep_priv *rpriv;
	struct mlx5e_priv *priv;

	ASSERT_RTNL();

	mdata = mlx5e_lookup_master_rep_bond_metadata(esw, lag_dev);
	if (!mdata)
		return;

	s_entry = mlx5e_lookup_rep_bond_slave_entry(mdata, netdev);
	if (!s_entry)
		return;

	priv = netdev_priv(netdev);
	rpriv = priv->ppriv;
	/* Reset this slave rep metadata and ingress */
	esw_replace_ingress_match_metadata(esw, rpriv->rep->vport, 0);
	mlx5e_replace_rep_vport_rx_rule_metadata(netdev);
	list_del(&s_entry->list);

	esw_debug(esw->dev,
		  "Unslave rep(%s) vport(%d) of lag_dev(%s) metadata(0x%x)\n",
		  s_entry->netdev->name, rpriv->rep->vport,
		  lag_dev->name, mdata->metadata_reg_c_0);

	if (--mdata->slaves == 0)
		mlx5e_rep_bond_metadata_release(mdata);
	kfree(s_entry);
}
