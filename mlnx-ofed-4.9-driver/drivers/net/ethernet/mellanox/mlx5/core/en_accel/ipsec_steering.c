// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2019, Mellanox Technologies inc. All rights reserved. */

#include "ipsec_steering.h"
#include "fs.h"
#include "fs_core.h"

static int mlx5e_add_ipsec_copy_action_rule(struct mlx5e_priv *priv,
					    enum accel_fs_type type)
{
	u8 action[MLX5_UN_SZ_BYTES(set_add_copy_action_in_auto)] = {};
	enum accel_fs_ipsec_default_type ipsec_default_type;
	struct mlx5_core_dev *mdev = priv->mdev;
	struct mlx5e_tir *tir = priv->indir_tir;
	struct mlx5_flow_destination dest = {};
	struct mlx5_flow_act flow_act = {};
	struct mlx5_modify_hdr *modify_hdr;
	struct mlx5e_flow_table *fs_t;
	struct mlx5_flow_handle *fte;
	struct mlx5_flow_spec *spec;
	enum mlx5e_traffic_types tt;
	int err = 0;

	if (type == ACCEL_FS_IPV4_ESP) {
		tt = MLX5E_TT_IPV4_IPSEC_ESP;
		ipsec_default_type = IPV4_ESP;
	} else if (type == ACCEL_FS_IPV6_ESP) {
		tt = MLX5E_TT_IPV6_IPSEC_ESP;
		ipsec_default_type = IPV6_ESP;
	} else {
		return -EINVAL;
	}

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;

	/* Action to copy 7 bit ipsec_syndrome */
	MLX5_SET(copy_action_in, action, action_type, MLX5_ACTION_TYPE_COPY);
	MLX5_SET(copy_action_in,
		 action,
		 src_field,
		 MLX5_ACTION_IN_FIELD_IPSEC_SYNDROME);
	MLX5_SET(copy_action_in, action, src_offset, 0);
	MLX5_SET(copy_action_in, action, length, 7);
	MLX5_SET(copy_action_in,
		 action,
		 dst_field,
		 MLX5_ACTION_IN_FIELD_METADATA_REG_B);
	MLX5_SET(copy_action_in, action, dst_offset, 0);

	modify_hdr = mlx5_modify_header_alloc(mdev,
					      MLX5_FLOW_NAMESPACE_KERNEL,
					      1,
					      action);

	if (IS_ERR(modify_hdr)) {
		mlx5_core_err(mdev, "fail to alloc ipsec copy modify_header_id\n");
		err = PTR_ERR(modify_hdr);
		goto out_spec;
	}
	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_MOD_HDR |
			  MLX5_FLOW_CONTEXT_ACTION_FWD_DEST;
	flow_act.modify_hdr = modify_hdr;
	dest.type = MLX5_FLOW_DESTINATION_TYPE_TIR;
	dest.tir_num = tir[tt].tirn;

	fs_t = &priv->fs.accel.ipsec_default[ipsec_default_type].ft_rx_err;
	fte = mlx5_add_flow_rules(fs_t->t, spec, &flow_act, &dest, 1);
	if (IS_ERR(fte)) {
		err = PTR_ERR(fte);
		mlx5_core_err(mdev,
			      "fail to add ipsec modify header rule err=%d\n",
			      err);
		fte = NULL;
		goto out;
	}

	priv->fs.accel.ipsec_default[ipsec_default_type].copy_fte = fte;
	priv->fs.accel.ipsec_default[ipsec_default_type].copy_modify_hdr = modify_hdr;

out:
	if (err)
		mlx5_modify_header_dealloc(mdev, modify_hdr);
out_spec:
	kfree(spec);
	return err;
}

static void mlx5e_del_ipsec_copy_action_rule(struct mlx5e_priv *priv)
{
	struct mlx5e_ipsec_default *ipsec_default;
	struct mlx5_core_dev *mdev;
	int i;

	mdev = priv->mdev;
	ipsec_default = priv->fs.accel.ipsec_default;

	for (i = 0; i < IPSEC_DEFAULT_TYPES; i++) {
		if (ipsec_default[i].copy_fte) {
			mlx5_del_flow_rules(ipsec_default[i].copy_fte);
			ipsec_default[i].copy_fte = NULL;
		}

		if (ipsec_default[i].copy_modify_hdr) {
			mlx5_modify_header_dealloc(mdev,
						   ipsec_default[i].copy_modify_hdr);
			ipsec_default[i].copy_modify_hdr = NULL;
		}
	}
}

void mlx5e_ipsec_destroy_rx_err_ft(struct mlx5e_priv *priv)
{
	int i;

	if (!(mlx5e_ipsec_device_caps(priv->mdev) & MLX5_ACCEL_IPSEC_CAP_DEVICE))
		return;

	mlx5e_del_ipsec_copy_action_rule(priv);

	for (i = 0; i < IPSEC_DEFAULT_TYPES; i++) {
		if (priv->fs.accel.ipsec_default[i].ft_rx_err.t) {
			mlx5e_destroy_flow_table(&priv->fs.accel.ipsec_default[i].ft_rx_err);
			priv->fs.accel.ipsec_default[i].ft_rx_err.t = NULL;
		}
	}
}

static int create_empty_groups(struct mlx5e_flow_table *ft)
{
	int inlen = MLX5_ST_SZ_BYTES(create_flow_group_in);
	u32 *in;
	int err;

	ft->g = kcalloc(1, sizeof(*ft->g), GFP_KERNEL);
	in = kvzalloc(inlen, GFP_KERNEL);
	if  (!in || !ft->g) {
		kvfree(ft->g);
		kvfree(in);
		return -ENOMEM;
	}

	MLX5_SET_CFG(in, start_flow_index, 0);
	MLX5_SET_CFG(in, end_flow_index, 0);
	ft->g[ft->num_groups] = mlx5_create_flow_group(ft->t, in);

	kvfree(in);

	if (IS_ERR(ft->g[ft->num_groups]))
		goto err;
	ft->num_groups++;

	return 0;

err:
	err = PTR_ERR(ft->g[ft->num_groups]);
	ft->g[ft->num_groups] = NULL;
	return err;
}

static int create_default_ft(struct mlx5e_priv *priv,
			     struct mlx5e_flow_table *ft,
			     enum accel_fs_type type)
{
	struct mlx5_flow_table_attr ft_attr = {};
	int err;

	ft->num_groups = 0;
	ft_attr.max_fte = 1;
	ft_attr.level = MLX5E_ACCEL_FS_ERR_FT_LEVEL;
	ft_attr.prio = MLX5E_NIC_PRIO;

	ft->t = mlx5_create_flow_table(priv->fs.ns, &ft_attr);
	if (IS_ERR(ft->t)) {
		err = PTR_ERR(ft->t);
		ft->t = NULL;
		return err;
	}

	err = create_empty_groups(ft);
	if (err)
		return err;

	err = mlx5e_add_ipsec_copy_action_rule(priv, type);

	mlx5_core_dbg(priv->mdev, "created ipsec err table id %u level %u\n",
		      ft->t->id, ft->t->level);

	return err;
}

int mlx5e_ipsec_create_rx_err_ft(struct mlx5e_priv *priv)
{
	struct mlx5e_flow_table *ft;
	int err = 0;
	int i;

	if (!(mlx5e_ipsec_device_caps(priv->mdev) & MLX5_ACCEL_IPSEC_CAP_DEVICE))
		return 0;

	for (i = 0; i < IPSEC_DEFAULT_TYPES; i++) {
		ft = &priv->fs.accel.ipsec_default[i].ft_rx_err;
		err = create_default_ft(priv, ft, ACCEL_FS_IPV4_ESP + i);
		if (err)
			goto out_err;
	}

	return err;

out_err:
	mlx5e_ipsec_destroy_rx_err_ft(priv);

	return err;
}

int mlx5e_xfrm_add_rule(struct mlx5_core_dev *mdev,
			struct mlx5_accel_esp_xfrm_attrs *attrs,
			struct mlx5e_ipsec_sa_ctx *sa_ctx)
{
	u8 action[MLX5_UN_SZ_BYTES(set_add_copy_action_in_auto)] = {};
	struct mlx5_modify_hdr *modify_hdr = NULL;
	struct mlx5_flow_handle *rule_tmp = NULL;
	struct mlx5_flow_destination dest = {};
	struct mlx5e_priv *priv = attrs->priv;
	struct mlx5_flow_act flow_act = {0};
	struct mlx5_flow_spec *spec = NULL;
	struct mlx5e_flow_table *fs_t;
	struct mlx5e_ipsec *ipsec;
	u16 ethertype;
	int err = 0;

	ethertype = (attrs->is_ipv6) ? ETH_P_IPV6 : ETH_P_IP;

	spec = kvzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec) {
		err = -ENOMEM;
		goto out;
	}

	ipsec = priv->ipsec;

	/* Set 1  bit ipsec marker */
	/* Set 24 bit ipsec_obj_id */
	sa_ctx->set_modify_hdr = NULL;
	if (attrs->action == MLX5_ACCEL_ESP_ACTION_DECRYPT) {
		MLX5_SET(set_action_in,
			 action,
			 action_type,
			 MLX5_ACTION_TYPE_SET);
		MLX5_SET(set_action_in, action,
			 field,
			 MLX5_ACTION_IN_FIELD_METADATA_REG_B);
		MLX5_SET(set_action_in, action, data, (sa_ctx->ipsec_obj_id << 1) | 0x1);
		MLX5_SET(set_action_in, action, offset, 7);
		MLX5_SET(set_action_in, action, length, 25);

		modify_hdr = mlx5_modify_header_alloc(mdev,
						      MLX5_FLOW_NAMESPACE_KERNEL,
						      1,
						      action);

		if (IS_ERR(modify_hdr)) {
			mlx5_core_err(mdev, "fail to alloc ipsec set modify_header_id\n");
			err = PTR_ERR(modify_hdr);
			goto out;
		}
	}

	spec->match_criteria_enable = MLX5_MATCH_OUTER_HEADERS | MLX5_MATCH_MISC_PARAMETERS;

	/* Ethertype */
	MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria,
			 outer_headers.ethertype);
	MLX5_SET(fte_match_param, spec->match_value, outer_headers.ethertype,
		 ethertype);

	/* Non fragmented */
	MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria,
			 outer_headers.frag);
	MLX5_SET(fte_match_param, spec->match_value, outer_headers.frag, 0);

	/* ESP header */
	MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria,
			 outer_headers.ip_protocol);
	MLX5_SET(fte_match_param, spec->match_value, outer_headers.ip_protocol,
		 IPPROTO_ESP);

	/* SPI number */
	MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria,
			 misc_parameters.outer_esp_spi);
	MLX5_SET(fte_match_param, spec->match_value, misc_parameters.outer_esp_spi,
		 cpu_to_be32(attrs->spi));

	if (ethertype == ETH_P_IP) {
		memcpy(MLX5_ADDR_OF(fte_match_param, spec->match_value,
				    outer_headers.src_ipv4_src_ipv6.ipv4_layout.ipv4),
		       &attrs->saddr.a4,
		       4);
		memcpy(MLX5_ADDR_OF(fte_match_param, spec->match_value,
				    outer_headers.dst_ipv4_dst_ipv6.ipv4_layout.ipv4),
		       &attrs->daddr.a4,
		       4);
		MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria,
				 outer_headers.src_ipv4_src_ipv6.ipv4_layout.ipv4);
		MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria,
				 outer_headers.dst_ipv4_dst_ipv6.ipv4_layout.ipv4);
	} else {
		memcpy(MLX5_ADDR_OF(fte_match_param, spec->match_value,
				    outer_headers.src_ipv4_src_ipv6.ipv6_layout.ipv6),
		       &attrs->saddr.a6,
		       16);
		memcpy(MLX5_ADDR_OF(fte_match_param, spec->match_value,
				    outer_headers.dst_ipv4_dst_ipv6.ipv6_layout.ipv6),
		       &attrs->daddr.a6,
		       16);
		memset(MLX5_ADDR_OF(fte_match_param, spec->match_criteria,
				    outer_headers.src_ipv4_src_ipv6.ipv6_layout.ipv6),
		       0xff,
		       16);
		memset(MLX5_ADDR_OF(fte_match_param, spec->match_criteria,
				    outer_headers.dst_ipv4_dst_ipv6.ipv6_layout.ipv6),
		       0xff,
		       16);
	}

	/* XFRM_OFFLOAD_INBOUND destination is error FT.
	 * Outbound action is ALLOW.
	 */
	flow_act.ipsec_obj_id = sa_ctx->ipsec_obj_id;

	if (attrs->action == MLX5_ACCEL_ESP_ACTION_DECRYPT) {
		flow_act.action = MLX5_FLOW_CONTEXT_ACTION_FWD_DEST |
				  MLX5_FLOW_CONTEXT_ACTION_IPSEC_DECRYPT |
				  MLX5_FLOW_CONTEXT_ACTION_MOD_HDR;
		dest.type = MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE;
		flow_act.modify_hdr = modify_hdr;
		if (ethertype == ETH_P_IP) {
			fs_t = &priv->fs.accel.accel_tables[ACCEL_FS_IPV4_ESP];
			dest.ft = priv->fs.accel.ipsec_default[IPV4_ESP].ft_rx_err.t;
		} else {
			fs_t = &priv->fs.accel.accel_tables[ACCEL_FS_IPV6_ESP];
			dest.ft = priv->fs.accel.ipsec_default[IPV6_ESP].ft_rx_err.t;
		}

		rule_tmp = mlx5_add_flow_rules(fs_t->t, spec,
					       &flow_act, &dest, 1);
	} else {
		/* Add IPsec indicator in metdata_reg_a */
		spec->match_criteria_enable |= MLX5_MATCH_MISC_PARAMETERS_2;
		MLX5_SET(fte_match_param, spec->match_criteria, misc_parameters_2.metadata_reg_a,
			 MLX5_ETH_WQE_FT_META_IPSEC);
		MLX5_SET(fte_match_param, spec->match_value, misc_parameters_2.metadata_reg_a,
			 MLX5_ETH_WQE_FT_META_IPSEC);

		flow_act.action = MLX5_FLOW_CONTEXT_ACTION_ALLOW |
				  MLX5_FLOW_CONTEXT_ACTION_IPSEC_ENCRYPT;
		rule_tmp = mlx5_add_flow_rules(ipsec->ft_tx, spec,
					       &flow_act, NULL, 0);
	}

	if (IS_ERR(rule_tmp)) {
		if (attrs->action == MLX5_ACCEL_ESP_ACTION_DECRYPT)
			mlx5_modify_header_dealloc(mdev, modify_hdr);
		err = PTR_ERR(rule_tmp);
		pr_err("Fail to add ipsec rule\n");
	} else {
		sa_ctx->ipsec_rule = rule_tmp;
		sa_ctx->set_modify_hdr = modify_hdr;
	}

out:
	kvfree(spec);
	return err;
}

void mlx5e_xfrm_del_rule(struct mlx5e_ipsec_sa_ctx *sa_ctx)
{
	if (sa_ctx->ipsec_rule) {
		mlx5_del_flow_rules(sa_ctx->ipsec_rule);
		sa_ctx->ipsec_rule = NULL;
	}

	if (sa_ctx->set_modify_hdr) {
		mlx5_modify_header_dealloc(sa_ctx->dev, sa_ctx->set_modify_hdr);
		sa_ctx->set_modify_hdr = NULL;
	}
}

#define NUM_IPSEC_FTE BIT(15)
#define NUM_IPSEC_FG 1

int mlx5e_ipsec_create_tx_ft(struct mlx5e_priv *priv)
{
	struct mlx5e_ipsec *ipsec = priv->ipsec;
	struct mlx5_core_dev *mdev = priv->mdev;
	struct mlx5_flow_table *ft;

	if (!ipsec)
		return 0;

	ft = mlx5_create_auto_grouped_flow_table(priv->fs.egress_ns,
						 0,
						 NUM_IPSEC_FTE,
						 NUM_IPSEC_FG,
						 0, 0);
	if (IS_ERR(ft)) {
		mlx5_core_err(mdev, "fail to create ipsec tx ft\n");
		goto error;
	}
	ipsec->ft_tx = ft;

	return 0;

error:
	mlx5e_ipsec_destroy_tx_ft(priv);
	return PTR_ERR(ft);
}

void mlx5e_ipsec_destroy_tx_ft(struct mlx5e_priv *priv)
{
	struct mlx5e_ipsec *ipsec = priv->ipsec;

	if (!ipsec)
		return;

	if (!IS_ERR_OR_NULL(ipsec->ft_tx)) {
		mlx5_destroy_flow_table(ipsec->ft_tx);
		ipsec->ft_tx = NULL;
	}
}
