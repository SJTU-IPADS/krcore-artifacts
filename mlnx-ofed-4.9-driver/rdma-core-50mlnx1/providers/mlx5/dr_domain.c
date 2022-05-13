/*
 * Copyright (c) 2019, Mellanox Technologies. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *	Redistribution and use in source and binary forms, with or
 *	without modification, are permitted provided that the following
 *	conditions are met:
 *
 *	- Redistributions of source code must retain the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer.
 *
 *	- Redistributions in binary form must reproduce the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer in the documentation and/or other materials
 *	  provided with the distribution.
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

#include <unistd.h>
#include <stdlib.h>
#include "mlx5dv_dr.h"

enum {
	MLX5DV_DR_DOMAIN_SYNC_SUP_FLAGS =
		(MLX5DV_DR_DOMAIN_SYNC_FLAGS_SW |
		 MLX5DV_DR_DOMAIN_SYNC_FLAGS_HW),
};

static int dr_domain_init_resources(struct mlx5dv_dr_domain *dmn)
{
	int ret = -1;

	dmn->ste_ctx = dr_ste_init_ctx(dmn->info.caps.sw_format_ver);
	if (!dmn->ste_ctx) {
		dr_dbg(dmn, "Couldn't select ste context\n");
		return errno;
	}

	dmn->pd = ibv_alloc_pd(dmn->ctx);
	if (!dmn->pd) {
		dr_dbg(dmn, "Couldn't allocate PD\n");
		return ret;
	}

	dmn->uar = mlx5dv_devx_alloc_uar(dmn->ctx, 0);
	if (!dmn->uar) {
		dr_dbg(dmn, "Can't allocate UAR\n");
		goto clean_pd;
	}

	dmn->ste_icm_pool = dr_icm_pool_create(dmn, DR_ICM_TYPE_STE);
	if (!dmn->ste_icm_pool) {
		dr_dbg(dmn, "Couldn't get icm memory for %s\n",
		       ibv_get_device_name(dmn->ctx->device));
		goto clean_uar;
	}

	dmn->action_icm_pool = dr_icm_pool_create(dmn, DR_ICM_TYPE_MODIFY_ACTION);
	if (!dmn->action_icm_pool) {
		dr_dbg(dmn, "Couldn't get action icm memory for %s\n",
		       ibv_get_device_name(dmn->ctx->device));
		goto free_ste_icm_pool;
	}

	ret = dr_send_ring_alloc(dmn);
	if (ret) {
		dr_dbg(dmn, "Couldn't create send-ring for %s\n",
		       ibv_get_device_name(dmn->ctx->device));
		goto free_action_icm_pool;
	}

	return 0;

free_action_icm_pool:
	dr_icm_pool_destroy(dmn->action_icm_pool);
free_ste_icm_pool:
	dr_icm_pool_destroy(dmn->ste_icm_pool);
clean_uar:
	mlx5dv_devx_free_uar(dmn->uar);
clean_pd:
	ibv_dealloc_pd(dmn->pd);

	return ret;
}

static void dr_free_resources(struct mlx5dv_dr_domain *dmn)
{
	dr_send_ring_free(dmn->send_ring);
	dr_icm_pool_destroy(dmn->action_icm_pool);
	dr_icm_pool_destroy(dmn->ste_icm_pool);
	mlx5dv_devx_free_uar(dmn->uar);
	ibv_dealloc_pd(dmn->pd);
}

static int dr_domain_query_vport(struct mlx5dv_dr_domain *dmn,
				 bool other_vport,
				 uint16_t vport_number,
				 struct dr_devx_vport_cap *vport_caps)
{
	int ret;

	ret = dr_devx_query_esw_vport_context(dmn->ctx,
					      other_vport,
					      vport_number,
					      &vport_caps->icm_address_rx,
					      &vport_caps->icm_address_tx);
	if (ret)
		return ret;

	ret = dr_devx_query_gvmi(dmn->ctx,
				 other_vport,
				 vport_number,
				 &vport_caps->vport_gvmi);
	if (ret)
		return ret;

	vport_caps->num = vport_number;
	vport_caps->vhca_gvmi = dmn->info.caps.gvmi;
	vport_caps->valid = true;

	return 0;
}

static int dr_domain_query_esw_mngr(struct mlx5dv_dr_domain *dmn)
{
	return dr_domain_query_vport(dmn, false, 0,
				     &dmn->info.caps.esw_manager_vport_caps);
}

static int dr_domain_query_vports(struct mlx5dv_dr_domain *dmn)
{
	struct dr_esw_caps *esw_caps = &dmn->info.caps.esw_caps;
	struct dr_devx_vport_cap *wire_vport;
	struct ibv_port_attr port_attr = {};
	int vport;
	int ret;

	ret = dr_domain_query_esw_mngr(dmn);
	if (ret)
		return ret;

	/* Query vports (except wire vport) */
	for (vport = 0; vport < dmn->info.attr.phys_port_cnt - 1; vport++) {
		/* Validate the port before issuing command to operate on it */
		ret = ibv_query_port(dmn->ctx, vport, &port_attr);
		if (ret || port_attr.state == IBV_PORT_NOP)
			continue;

		ret = dr_domain_query_vport(dmn, !!vport || dmn->info.caps.is_ecpf,
					    vport, &dmn->info.caps.vports_caps[vport]);
		if (ret)
			return ret;
	}

	/* Last vport is the wire port */
	wire_vport = &dmn->info.caps.vports_caps[vport];
	wire_vport->num = WIRE_PORT;
	wire_vport->icm_address_rx = esw_caps->uplink_icm_address_rx;
	wire_vport->icm_address_tx = esw_caps->uplink_icm_address_tx;
	wire_vport->vport_gvmi = 0;
	wire_vport->vhca_gvmi = dmn->info.caps.gvmi;
	wire_vport->valid = true;

	return 0;
}

static int dr_domain_query_ib_ports(struct mlx5dv_dr_domain *dmn)
{
	struct dr_devx_vport_cap **ib_ports_caps;
	struct mlx5dv_devx_port devx_port;
	struct dr_devx_vport_cap *vport;
	uint64_t wire_comp_mask;
	uint64_t comp_mask;
	int vport_idx;
	int ret;
	int i;

	ret = dr_domain_query_esw_mngr(dmn);
	if (ret)
		return ret;

	/* Query vport 0 */
	ret = dr_domain_query_vport(dmn, dmn->info.caps.is_ecpf, 0,
				    &dmn->info.caps.vports_caps[0]);
	if (ret)
		return ret;

	ib_ports_caps = calloc(dmn->info.attr.phys_port_cnt,
			       sizeof(struct dr_devx_vport_cap *));
	if (!ib_ports_caps) {
		errno = ENOMEM;
		return errno;
	}

	wire_comp_mask = MLX5DV_DEVX_PORT_VPORT |
			 MLX5DV_DEVX_PORT_ESW_OWNER_VHCA_ID |
			 MLX5DV_DEVX_PORT_MATCH_REG_C_0 |
			 MLX5DV_DEVX_PORT_VPORT_ICM_TX;

	comp_mask = wire_comp_mask |
		    MLX5DV_DEVX_PORT_VPORT_VHCA_ID |
		    MLX5DV_DEVX_PORT_VPORT_ICM_RX;

	for (i = 1; i <= dmn->info.attr.phys_port_cnt; i++) {
		memset(&devx_port, 0, sizeof(devx_port));
		devx_port.comp_mask = comp_mask;

		ret = mlx5dv_query_devx_port(dmn->ctx, i, &devx_port);
		if (ret)
			goto free_ib_ports;

		/* Skip if ib_port not mapped to a vport */
		if (!(devx_port.comp_mask & MLX5DV_DEVX_PORT_VPORT))
			continue;

		/* Check if required fields were supplied */
		if (devx_port.vport_num == WIRE_PORT) {
			if ((devx_port.comp_mask & wire_comp_mask) != wire_comp_mask) {
				errno = EINVAL;
				ret = errno;
				goto free_ib_ports;
			}

			/* Wire vport is stored in the the last vport */
			vport_idx = dmn->info.attr.phys_port_cnt - 1;
		} else {
			if ((devx_port.comp_mask & comp_mask) != comp_mask)
				continue;

			vport_idx = devx_port.vport_num;
		}

		if (vport_idx >= dmn->info.attr.phys_port_cnt) {
			dr_dbg(dmn, "Invalid vport_num 0x%x\n", vport_idx);
			continue;
		}

		if (devx_port.esw_owner_vhca_id == dmn->info.caps.gvmi)
			vport = &dmn->info.caps.vports_caps[vport_idx];
		else
			vport = &dmn->info.caps.other_vports_caps[vport_idx];

		vport->num = devx_port.vport_num;
		vport->icm_address_rx = devx_port.icm_addr_rx;
		vport->icm_address_tx = devx_port.icm_addr_tx;
		vport->vport_gvmi = devx_port.vport_vhca_id;
		vport->vhca_gvmi = devx_port.esw_owner_vhca_id;
		vport->metadata_c = devx_port.reg_c_0.value;
		vport->metadata_c_mask = devx_port.reg_c_0.mask;
		vport->valid = true;

		/* Point from ib port to vport */
		ib_ports_caps[i - 1] = vport;
	}

	dmn->info.caps.ib_ports_caps = ib_ports_caps;

	return 0;

free_ib_ports:
	memset(dmn->info.caps.vports_caps, 0,
	       dmn->info.attr.phys_port_cnt *
	       sizeof(struct dr_devx_vport_cap));
	memset(dmn->info.caps.other_vports_caps, 0,
	       dmn->info.attr.phys_port_cnt *
	       sizeof(struct dr_devx_vport_cap));
	free(ib_ports_caps);
	return ret;
}

static int dr_domain_query_fdb_caps(struct ibv_context *ctx,
				    struct mlx5dv_dr_domain *dmn)
{
	int ret;

	if (!dmn->info.caps.eswitch_manager)
		return 0;

	ret = dr_devx_query_esw_caps(ctx, &dmn->info.caps.esw_caps);
	if (ret)
		return ret;

	dmn->info.caps.fdb_sw_owner = dmn->info.caps.esw_caps.sw_owner;
	dmn->info.caps.fdb_sw_owner_v2 = dmn->info.caps.esw_caps.sw_owner_v2;
	dmn->info.caps.esw_rx_drop_address = dmn->info.caps.esw_caps.drop_icm_address_rx;
	dmn->info.caps.esw_tx_drop_address = dmn->info.caps.esw_caps.drop_icm_address_tx;

	dmn->info.caps.vports_caps = calloc(dmn->info.attr.phys_port_cnt,
					    sizeof(struct dr_devx_vport_cap));
	if (!dmn->info.caps.vports_caps) {
		errno = ENOMEM;
		return errno;
	}

	dmn->info.caps.other_vports_caps = calloc(dmn->info.attr.phys_port_cnt,
						  sizeof(struct dr_devx_vport_cap));
	if (!dmn->info.caps.other_vports_caps) {
		errno = ENOMEM;
		ret = errno;
		goto free_vports_caps;
	}

	ret = dr_domain_query_ib_ports(dmn);
	if (ret) {
		ret = dr_domain_query_vports(dmn);
		if (ret) {
			dr_dbg(dmn, "Failed to query vports caps using devx and dv\n");
			goto free_other_vport_caps;
		}
	}

	dmn->info.caps.num_vports = dmn->info.attr.phys_port_cnt - 1;

	return 0;

free_other_vport_caps:
	free(dmn->info.caps.other_vports_caps);
	dmn->info.caps.other_vports_caps = NULL;
free_vports_caps:
	free(dmn->info.caps.vports_caps);
	dmn->info.caps.vports_caps = NULL;
	return ret;
}

static int dr_domain_caps_init(struct ibv_context *ctx,
			       struct mlx5dv_dr_domain *dmn)
{
	struct dr_devx_vport_cap *vport_cap;
	struct ibv_port_attr port_attr = {};
	int ret;

	ret = ibv_query_port(ctx, 1, &port_attr);
	if (ret) {
		dr_dbg(dmn, "Failed to query port\n");
		return ret;
	}

	if (port_attr.link_layer != IBV_LINK_LAYER_ETHERNET) {
		dr_dbg(dmn, "Failed to allocate domain, bad link type\n");
		errno = EOPNOTSUPP;
		return errno;
	}

	ret = ibv_query_device(ctx, &dmn->info.attr);
	if (ret)
		return ret;

	ret = dr_devx_query_device(ctx, &dmn->info.caps);
	if (ret)
		/* Ignore devx query failure to allow steering on root level
		 * tables in case devx is not supported over mlx5dv_dr API
		 */
		return 0;

	ret = dr_domain_query_fdb_caps(ctx, dmn);
	if (ret)
		return ret;

	switch (dmn->type) {
	case MLX5DV_DR_DOMAIN_TYPE_NIC_RX:
		if (!dmn->info.caps.rx_sw_owner && !dmn->info.caps.rx_sw_owner_v2)
			return 0;

		dmn->info.supp_sw_steering = true;
		dmn->info.rx.ste_type = DR_STE_TYPE_RX;
		dmn->info.rx.default_icm_addr = dmn->info.caps.nic_rx_drop_address;
		dmn->info.rx.drop_icm_addr = dmn->info.caps.nic_rx_drop_address;
		break;
	case MLX5DV_DR_DOMAIN_TYPE_NIC_TX:
		if (!dmn->info.caps.tx_sw_owner && !dmn->info.caps.tx_sw_owner_v2)
			return 0;

		dmn->info.supp_sw_steering = true;
		dmn->info.tx.ste_type = DR_STE_TYPE_TX;
		dmn->info.tx.default_icm_addr = dmn->info.caps.nic_tx_allow_address;
		dmn->info.tx.drop_icm_addr = dmn->info.caps.nic_tx_drop_address;
		break;
	case MLX5DV_DR_DOMAIN_TYPE_FDB:
		if (!dmn->info.caps.eswitch_manager)
			return 0;

		if (!dmn->info.caps.fdb_sw_owner && !dmn->info.caps.fdb_sw_owner_v2)
			return 0;

		dmn->info.rx.ste_type = DR_STE_TYPE_RX;
		dmn->info.tx.ste_type = DR_STE_TYPE_TX;
		vport_cap = &dmn->info.caps.esw_manager_vport_caps;
		if (!vport_cap->valid) {
			dr_dbg(dmn, "Esw manager vport is invalid\n");
			return errno;
		}

		dmn->info.supp_sw_steering = true;
		dmn->info.tx.default_icm_addr = vport_cap->icm_address_tx;
		dmn->info.rx.default_icm_addr = vport_cap->icm_address_rx;
		dmn->info.rx.drop_icm_addr = dmn->info.caps.esw_rx_drop_address;
		dmn->info.tx.drop_icm_addr = dmn->info.caps.esw_tx_drop_address;
		break;
	default:
		dr_dbg(dmn, "Invalid domain\n");
		ret = EINVAL;
		break;
	}

	return ret;
}

static void dr_domain_caps_uninit(struct mlx5dv_dr_domain *dmn)
{
	if (dmn->info.caps.vports_caps)
		free(dmn->info.caps.vports_caps);

	if (dmn->info.caps.other_vports_caps)
		free(dmn->info.caps.other_vports_caps);

	if (dmn->info.caps.ib_ports_caps)
		free(dmn->info.caps.ib_ports_caps);
}

struct mlx5dv_dr_domain *
mlx5dv_dr_domain_create(struct ibv_context *ctx,
			enum mlx5dv_dr_domain_type type)
{
	struct mlx5dv_dr_domain *dmn;
	int ret;

	if (type > MLX5DV_DR_DOMAIN_TYPE_FDB) {
		errno = EINVAL;
		return NULL;
	}

	dmn = calloc(1, sizeof(*dmn));
	if (!dmn) {
		errno = ENOMEM;
		return NULL;
	}

	dmn->ctx = ctx;
	dmn->type = type;
	atomic_init(&dmn->refcount, 1);
	list_head_init(&dmn->tbl_list);

	if (dr_domain_caps_init(ctx, dmn)) {
		dr_dbg(dmn, "Failed init domain, no caps\n");
		goto free_domain;
	}

	dmn->info.max_log_action_icm_sz = DR_CHUNK_SIZE_4K;
	dmn->info.max_log_sw_icm_sz = min_t(uint32_t, DR_CHUNK_SIZE_1024K,
					    dmn->info.caps.log_icm_size);

	/* Allocate resources */
	if (dmn->info.supp_sw_steering) {
		ret = dr_domain_init_resources(dmn);
		if (ret) {
			dr_dbg(dmn, "Failed init domain resources for %s\n",
			       ibv_get_device_name(ctx->device));
			goto uninit_caps;
		}

		/* Init CRC table for htbl CRC calculation */
		dr_crc32_init_table();
	}
	return dmn;

uninit_caps:
	dr_domain_caps_uninit(dmn);
free_domain:
	free(dmn);
	return NULL;
}

/*
 * Assure synchronization of the device steering tables with updates made by SW
 * insertion.
 */
int mlx5dv_dr_domain_sync(struct mlx5dv_dr_domain *dmn, uint32_t flags)
{
	int ret = 0;

	if (!dmn->info.supp_sw_steering ||
	    !check_comp_mask(flags, MLX5DV_DR_DOMAIN_SYNC_SUP_FLAGS)) {
		errno = EOPNOTSUPP;
		return errno;
	}

	if (flags & MLX5DV_DR_DOMAIN_SYNC_FLAGS_SW) {
		pthread_mutex_lock(&dmn->mutex);
		ret = dr_send_ring_force_drain(dmn);
		if (ret)
			goto out_unlock;

		pthread_mutex_unlock(&dmn->mutex);
	}

	if (flags & MLX5DV_DR_DOMAIN_SYNC_FLAGS_HW)
		ret = dr_devx_sync_steering(dmn->ctx);

	return ret;

out_unlock:
	pthread_mutex_unlock(&dmn->mutex);
	return ret;
}

int mlx5dv_dr_domain_destroy(struct mlx5dv_dr_domain *dmn)
{
	if (atomic_load(&dmn->refcount) > 1)
		return EBUSY;

	if (dmn->info.supp_sw_steering) {
		/* make sure resources are not used by the hardware */
		dr_devx_sync_steering(dmn->ctx);
		dr_free_resources(dmn);
	}

	dr_domain_caps_uninit(dmn);

	free(dmn);
	return 0;
}
