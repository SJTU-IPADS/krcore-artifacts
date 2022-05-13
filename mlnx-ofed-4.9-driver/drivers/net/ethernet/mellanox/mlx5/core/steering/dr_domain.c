// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2019 Mellanox Technologies. */

#include <linux/mlx5/eswitch.h>
#include "dr_types.h"

static int dr_domain_init_cache(struct mlx5dr_domain *dmn)
{
	/* Per vport cached FW FT for checksum recalculation, this
	 * recalculation is needed due to a HW bug.
	 */
	dmn->cache.recalc_cs_ft = kcalloc(dmn->info.caps.num_nic_vports,
					  sizeof(dmn->cache.recalc_cs_ft[0]),
					  GFP_KERNEL);
	if (!dmn->cache.recalc_cs_ft)
		return -ENOMEM;

	return 0;
}

static void dr_domain_uninit_cache(struct mlx5dr_domain *dmn)
{
	int i;

	for (i = 0; i < dmn->info.caps.num_nic_vports; i++) {
		if (!dmn->cache.recalc_cs_ft[i])
			continue;

		mlx5dr_fw_destroy_recalc_cs_ft(dmn, dmn->cache.recalc_cs_ft[i]);
	}

	kfree(dmn->cache.recalc_cs_ft);
}

int mlx5dr_domain_cache_get_recalc_cs_ft_addr(struct mlx5dr_domain *dmn,
					      u32 vport_num,
					      u64 *rx_icm_addr)
{
	struct mlx5dr_fw_recalc_cs_ft *recalc_cs_ft;

	recalc_cs_ft = dmn->cache.recalc_cs_ft[vport_num];
	if (!recalc_cs_ft) {
		/* Table not in cache, need to allocate a new one */
		recalc_cs_ft = mlx5dr_fw_create_recalc_cs_ft(dmn, vport_num);
		if (!recalc_cs_ft)
			return -EINVAL;

		dmn->cache.recalc_cs_ft[vport_num] = recalc_cs_ft;
	}

	*rx_icm_addr = recalc_cs_ft->rx_icm_addr;

	return 0;
}

static int dr_domain_init_resources(struct mlx5dr_domain *dmn)
{
	int ret;

	ret = mlx5_core_alloc_pd(dmn->mdev, &dmn->pdn);
	if (ret) {
		mlx5dr_err(dmn, "Couldn't allocate PD, ret: %d", ret);
		return ret;
	}

	dmn->uar = mlx5_get_uars_page(dmn->mdev);
	if (!dmn->uar) {
		mlx5dr_err(dmn, "Couldn't allocate UAR\n");
		ret = -ENOMEM;
		goto clean_pd;
	}

	dmn->ste_icm_pool = mlx5dr_icm_pool_create(dmn, DR_ICM_TYPE_STE);
	if (!dmn->ste_icm_pool) {
		mlx5dr_err(dmn, "Couldn't get icm memory\n");
		ret = -ENOMEM;
		goto clean_uar;
	}

	dmn->action_icm_pool = mlx5dr_icm_pool_create(dmn, DR_ICM_TYPE_MODIFY_ACTION);
	if (!dmn->action_icm_pool) {
		mlx5dr_err(dmn, "Couldn't get action icm memory\n");
		ret = -ENOMEM;
		goto free_ste_icm_pool;
	}

	ret = mlx5dr_send_ring_alloc(dmn);
	if (ret) {
		mlx5dr_err(dmn, "Couldn't create send-ring\n");
		goto free_action_icm_pool;
	}

	return 0;

free_action_icm_pool:
	mlx5dr_icm_pool_destroy(dmn->action_icm_pool);
free_ste_icm_pool:
	mlx5dr_icm_pool_destroy(dmn->ste_icm_pool);
clean_uar:
	mlx5_put_uars_page(dmn->mdev, dmn->uar);
clean_pd:
	mlx5_core_dealloc_pd(dmn->mdev, dmn->pdn);

	return ret;
}

static void dr_domain_uninit_resources(struct mlx5dr_domain *dmn)
{
	mlx5dr_send_ring_free(dmn, dmn->send_ring);
	mlx5dr_icm_pool_destroy(dmn->action_icm_pool);
	mlx5dr_icm_pool_destroy(dmn->ste_icm_pool);
	mlx5_put_uars_page(dmn->mdev, dmn->uar);
	mlx5_core_dealloc_pd(dmn->mdev, dmn->pdn);
}

static int dr_domain_query_vport(struct mlx5dr_domain *dmn,
				 u16 vport_number,
				 struct mlx5dr_cmd_vport_cap *vport_caps)
{
	bool other_vport;
	u16 cmd_vport;
	int ret;

	if (dmn->info.caps.is_ecpf) {
		other_vport = vport_number != ECPF_PORT;
		cmd_vport = vport_number == ECPF_PORT ? 0 : vport_number;
	} else {
		other_vport = !!vport_number;
		cmd_vport = vport_number;
	}

	ret = mlx5dr_cmd_query_esw_vport_context(dmn->mdev,
						 other_vport,
						 cmd_vport,
						 &vport_caps->icm_address_rx,
						 &vport_caps->icm_address_tx);
	if (ret)
		return ret;

	ret = mlx5dr_cmd_query_gvmi(dmn->mdev,
				    other_vport,
				    cmd_vport,
				    &vport_caps->vport_gvmi);
	if (ret)
		return ret;

	vport_caps->num = vport_number;
	vport_caps->vhca_gvmi = dmn->info.caps.gvmi;

	return 0;
}

int mlx5dr_domain_vport_enable(struct mlx5dr_domain *dmn, u32 vport)
{
	struct mlx5dr_cmd_vport_cap *vport_caps;
	int ret;

	vport_caps = mlx5dr_get_vport_cap(&dmn->info.caps, vport);
	if (!vport_caps)
		return -EINVAL;

	ret = dr_domain_query_vport(dmn, vport, vport_caps);
	if (ret)
		return ret;

	vport_caps->flags |= MLX5DR_CMD_VPORT_FLAG_ENABLED;
	return 0;
}

void mlx5dr_domain_vport_disable(struct mlx5dr_domain *dmn, u32 vport)
{
	struct mlx5dr_cmd_vport_cap *vport_caps;

	vport_caps = mlx5dr_get_vport_cap(&dmn->info.caps, vport);
	if (!vport_caps)
		return;

	vport_caps->flags &= ~MLX5DR_CMD_VPORT_FLAG_ENABLED;
}

static void dr_domain_query_uplink(struct mlx5dr_domain *dmn)
{
	struct mlx5dr_esw_caps *esw_caps = &dmn->info.caps.esw_caps;
	struct mlx5dr_cmd_vport_cap *wire_vport =
		&dmn->info.caps.uplink_vport_caps;

	wire_vport->num = WIRE_PORT;
	wire_vport->icm_address_rx = esw_caps->uplink_icm_address_rx;
	wire_vport->icm_address_tx = esw_caps->uplink_icm_address_tx;
	wire_vport->vport_gvmi = 0;
	wire_vport->vhca_gvmi = dmn->info.caps.gvmi;
	wire_vport->flags |= MLX5DR_CMD_VPORT_FLAG_ENABLED;
}

static int dr_domain_query_vports(struct mlx5dr_domain *dmn)
{
	struct mlx5dr_cmd_caps *caps = &dmn->info.caps;
	int ret;
	int vf;

	if (dmn->info.caps.is_ecpf) {
		ret = mlx5dr_domain_vport_enable(dmn, ECPF_PORT);
		if (ret)
			return ret;
	}

	ret = mlx5dr_domain_vport_enable(dmn, 0);
	if (ret)
		return ret;

	/* Query vf vports */
	for (vf = 0; vf < caps->num_vf_vports; vf++) {
		int vport = vf + 1;

		ret = mlx5dr_domain_vport_enable(dmn, vport);
		if (ret)
			return ret;
	}

	/* Sf vports cannot be queried before sf was enabled */

	dr_domain_query_uplink(dmn);

	return 0;
}

static int dr_domain_query_fdb_caps(struct mlx5_core_dev *mdev,
				    struct mlx5dr_domain *dmn)
{
	int ret;

	if (!dmn->info.caps.eswitch_manager)
		return -EOPNOTSUPP;

	ret = mlx5dr_cmd_query_esw_caps(mdev, &dmn->info.caps.esw_caps);
	if (ret)
		return ret;

	dmn->info.caps.fdb_sw_owner = dmn->info.caps.esw_caps.sw_owner;
	dmn->info.caps.esw_rx_drop_address = dmn->info.caps.esw_caps.drop_icm_address_rx;
	dmn->info.caps.esw_tx_drop_address = dmn->info.caps.esw_caps.drop_icm_address_tx;

	dmn->info.caps.pf_vf_vports_caps =
		kcalloc(dmn->info.caps.num_pf_vf_vports,
			sizeof(dmn->info.caps.pf_vf_vports_caps[0]),
			GFP_KERNEL);

	if (!dmn->info.caps.pf_vf_vports_caps)
		return -ENOMEM;

	ret = dr_domain_query_vports(dmn);
	if (ret) {
		mlx5dr_err(dmn, "Failed to query vports caps (err: %d)", ret);
		goto free_vports_caps;
	}

	if (dmn->info.caps.num_sf_vports > 0) {
		dmn->info.caps.sf_vports_caps =
			kcalloc(dmn->info.caps.num_sf_vports,
				sizeof(dmn->info.caps.sf_vports_caps[0]),
				GFP_KERNEL);

		if (!dmn->info.caps.sf_vports_caps) {
			ret = -ENOMEM;
			goto free_vports_caps;
		}
	}

	return 0;

free_vports_caps:
	kfree(dmn->info.caps.pf_vf_vports_caps);
	dmn->info.caps.pf_vf_vports_caps = NULL;
	return ret;
}

static int dr_domain_caps_init(struct mlx5_core_dev *mdev,
			       struct mlx5dr_domain *dmn)
{
	struct mlx5dr_cmd_vport_cap *vport_cap;
	int ret;

	if (MLX5_CAP_GEN(mdev, port_type) != MLX5_CAP_PORT_TYPE_ETH) {
		mlx5dr_err(dmn, "Failed to allocate domain, bad link type\n");
		return -EOPNOTSUPP;
	}

	ret = mlx5dr_cmd_query_device(mdev, &dmn->info.caps);
	if (ret)
		return ret;

	ret = dr_domain_query_fdb_caps(mdev, dmn);
	if (ret)
		return ret;

	switch (dmn->type) {
	case MLX5DR_DOMAIN_TYPE_NIC_RX:
		if (!dmn->info.caps.rx_sw_owner)
			return -ENOTSUPP;

		dmn->info.supp_sw_steering = true;
		dmn->info.rx.ste_type = MLX5DR_STE_TYPE_RX;
		dmn->info.rx.default_icm_addr = dmn->info.caps.nic_rx_drop_address;
		dmn->info.rx.drop_icm_addr = dmn->info.caps.nic_rx_drop_address;
		break;
	case MLX5DR_DOMAIN_TYPE_NIC_TX:
		if (!dmn->info.caps.tx_sw_owner)
			return -ENOTSUPP;

		dmn->info.supp_sw_steering = true;
		dmn->info.tx.ste_type = MLX5DR_STE_TYPE_TX;
		dmn->info.tx.default_icm_addr = dmn->info.caps.nic_tx_allow_address;
		dmn->info.tx.drop_icm_addr = dmn->info.caps.nic_tx_drop_address;
		break;
	case MLX5DR_DOMAIN_TYPE_FDB:
		if (!dmn->info.caps.eswitch_manager)
			return -ENOTSUPP;

		if (!dmn->info.caps.fdb_sw_owner)
			return -ENOTSUPP;

		dmn->info.rx.ste_type = MLX5DR_STE_TYPE_RX;
		dmn->info.tx.ste_type = MLX5DR_STE_TYPE_TX;
		vport_cap = &dmn->info.caps.esw_manager_vport_caps;

		dmn->info.supp_sw_steering = true;
		dmn->info.tx.default_icm_addr = vport_cap->icm_address_tx;
		dmn->info.rx.default_icm_addr = vport_cap->icm_address_rx;
		dmn->info.rx.drop_icm_addr = dmn->info.caps.esw_rx_drop_address;
		dmn->info.tx.drop_icm_addr = dmn->info.caps.esw_tx_drop_address;
		break;
	default:
		mlx5dr_err(dmn, "Invalid domain\n");
		ret = -EINVAL;
		break;
	}

	return ret;
}

static void dr_domain_caps_uninit(struct mlx5dr_domain *dmn)
{
	kfree(dmn->info.caps.pf_vf_vports_caps);
	if (dmn->info.caps.num_sf_vports > 0)
		kfree(dmn->info.caps.sf_vports_caps);
}

struct mlx5dr_domain *
mlx5dr_domain_create(struct mlx5_core_dev *mdev, enum mlx5dr_domain_type type)
{
	struct mlx5dr_domain *dmn;
	int ret;

	if (type > MLX5DR_DOMAIN_TYPE_FDB)
		return NULL;

	dmn = kzalloc(sizeof(*dmn), GFP_KERNEL);
	if (!dmn)
		return NULL;

	dmn->mdev = mdev;
	dmn->type = type;
	refcount_set(&dmn->refcount, 1);
	mutex_init(&dmn->info.rx.mutex);
	mutex_init(&dmn->info.tx.mutex);
	mutex_init(&dmn->dbg_mutex);

	if (dr_domain_caps_init(mdev, dmn)) {
		mlx5dr_err(dmn, "Failed init domain, no caps\n");
		goto free_domain;
	}

	dmn->info.max_log_action_icm_sz = DR_CHUNK_SIZE_4K;
	dmn->info.max_log_sw_icm_sz = min_t(u32, DR_CHUNK_SIZE_1024K,
					    dmn->info.caps.log_icm_size);

	if (!dmn->info.supp_sw_steering) {
		mlx5dr_err(dmn, "SW steering is not supported\n");
		goto uninit_caps;
	}

	/* Allocate resources */
	ret = dr_domain_init_resources(dmn);
	if (ret) {
		mlx5dr_err(dmn, "Failed init domain resources\n");
		goto uninit_caps;
	}

	ret = dr_domain_init_cache(dmn);
	if (ret) {
		mlx5dr_err(dmn, "Failed initialize domain cache\n");
		goto uninit_resourses;
	}

	ret = mlx5dr_dbg_init_dump(dmn);
	if (ret) {
		mlx5dr_err(dmn, "Failed initialize domain dump tool\n");
		goto uninit_cache;
	}

	INIT_LIST_HEAD(&dmn->tbl_list);

	return dmn;

uninit_cache:
	dr_domain_uninit_cache(dmn);
uninit_resourses:
	dr_domain_uninit_resources(dmn);
uninit_caps:
	dr_domain_caps_uninit(dmn);
free_domain:
	kfree(dmn);
	return NULL;
}

/* Assure synchronization of the device steering tables with updates made by SW
 * insertion.
 */
int mlx5dr_domain_sync(struct mlx5dr_domain *dmn, u32 flags)
{
	int ret = 0;

	if (flags & MLX5DR_DOMAIN_SYNC_FLAGS_SW) {
		mlx5dr_domain_lock(dmn);
		ret = mlx5dr_send_ring_force_drain(dmn);
		mlx5dr_domain_unlock(dmn);
		if (ret) {
			mlx5dr_err(dmn, "Force drain failed flags: %d, ret: %d\n",
				   flags, ret);
			return ret;
		}
	}

	if (flags & MLX5DR_DOMAIN_SYNC_FLAGS_HW)
		ret = mlx5dr_cmd_sync_steering(dmn->mdev);

	return ret;
}

int mlx5dr_domain_destroy(struct mlx5dr_domain *dmn)
{
	if (refcount_read(&dmn->refcount) > 1)
		return -EBUSY;

	/* make sure resources are not used by the hardware */
	mlx5dr_cmd_sync_steering(dmn->mdev);
	mlx5dr_dbg_cleanup_dump(dmn);
	dr_domain_uninit_cache(dmn);
	dr_domain_uninit_resources(dmn);
	dr_domain_caps_uninit(dmn);
	mutex_destroy(&dmn->info.tx.mutex);
	mutex_destroy(&dmn->info.rx.mutex);
	mutex_destroy(&dmn->dbg_mutex);
	kfree(dmn);
	return 0;
}

void mlx5dr_domain_set_peer(struct mlx5dr_domain *dmn,
			    struct mlx5dr_domain *peer_dmn)
{
	mlx5dr_domain_lock(dmn);

	if (dmn->peer_dmn)
		refcount_dec(&dmn->peer_dmn->refcount);

	dmn->peer_dmn = peer_dmn;

	if (dmn->peer_dmn)
		refcount_inc(&dmn->peer_dmn->refcount);

	mlx5dr_domain_unlock(dmn);
}
