// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIBt
/* Copyright (c) 2020, Mellanox Technologies inc. All rights reserved. */

#include <linux/netdevice.h>
#include "en_accel/fs.h"
#include "ipsec_offload.h"
#include "ipsec_steering.h"
#include "fs_core.h"

static bool is_accel_type_supported(struct mlx5_core_dev *mdev,
				    enum accel_fs_type type)
{
	switch (type) {
	case ACCEL_FS_IPV4_TCP:
	case ACCEL_FS_IPV6_TCP:
		return true;
#ifdef CONFIG_MLX5_IPSEC
	case ACCEL_FS_IPV4_ESP:
	case ACCEL_FS_IPV6_ESP:
		return (mlx5e_ipsec_device_caps(mdev) &
			MLX5_ACCEL_IPSEC_CAP_DEVICE);
#endif
	default:
		return false;
	}
}

static void mlx5e_accel_set_ipv4_flow(struct mlx5_flow_spec *spec,
				      struct sock *sk)
{
	MLX5_SET(fte_match_param, spec->match_value,
		 outer_headers.ethertype,
		 ETH_P_IP);
	memcpy(MLX5_ADDR_OF(fte_match_param, spec->match_value,
			    outer_headers.src_ipv4_src_ipv6.ipv4_layout.ipv4),
	       &inet_sk(sk)->inet_daddr,
	       4);
	memcpy(MLX5_ADDR_OF(fte_match_param, spec->match_value,
			    outer_headers.dst_ipv4_dst_ipv6.ipv4_layout.ipv4),
	       &inet_sk(sk)->inet_rcv_saddr,
	       4);
	MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria,
			 outer_headers.src_ipv4_src_ipv6.ipv4_layout.ipv4);
	MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria,
			 outer_headers.dst_ipv4_dst_ipv6.ipv4_layout.ipv4);
}

static void mlx5e_accel_set_ipv6_flow(struct mlx5_flow_spec *spec,
				      struct sock *sk)
{
	MLX5_SET(fte_match_param, spec->match_value,
		 outer_headers.ethertype,
		 ETH_P_IPV6);
	memcpy(MLX5_ADDR_OF(fte_match_param, spec->match_value,
			    outer_headers.src_ipv4_src_ipv6.ipv6_layout.ipv6),
	       &sk->sk_v6_daddr,
	       16);
	memcpy(MLX5_ADDR_OF(fte_match_param, spec->match_value,
			    outer_headers.dst_ipv4_dst_ipv6.ipv6_layout.ipv6),
	       &inet6_sk(sk)->saddr,
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

struct mlx5_flow_handle *mlx5e_accel_fs_add_flow(struct mlx5e_priv *priv,
						 struct sock *sk, u32 tirn,
						 uint32_t flow_tag)
{
	struct mlx5_flow_spec *spec = kvzalloc(sizeof(*spec), GFP_KERNEL);
	struct mlx5_flow_destination dest = {};
	struct mlx5_flow_handle *flow;
	struct mlx5e_flow_table *ft;
	MLX5_DECLARE_FLOW_ACT(flow_act);

	/* match outer headers, don't know what it means */
	spec->match_criteria_enable = MLX5_MATCH_OUTER_HEADERS;

	/* IPv4/IPv6 packet */
	MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria,
			 outer_headers.ethertype);
	switch (sk->sk_family) {
	case AF_INET:
		mlx5e_accel_set_ipv4_flow(spec, sk);
		ft = &priv->fs.accel.accel_tables[ACCEL_FS_IPV4_TCP];
		netdev_dbg(priv->netdev, "%s flow is %pI4:%d -> %pI4:%d\n", __func__,
			   &inet_sk(sk)->inet_rcv_saddr,
			   inet_sk(sk)->inet_sport,
			   &inet_sk(sk)->inet_daddr,
			   inet_sk(sk)->inet_dport);
	break;
#if IS_ENABLED(CONFIG_IPV6)
	case AF_INET6:
		if (!sk->sk_ipv6only &&
		    ipv6_addr_type(&sk->sk_v6_daddr) == IPV6_ADDR_MAPPED) {
			mlx5e_accel_set_ipv4_flow(spec, sk);
			ft = &priv->fs.accel.accel_tables[ACCEL_FS_IPV4_TCP];
			break;
		}
		mlx5e_accel_set_ipv6_flow(spec, sk);
		ft = &priv->fs.accel.accel_tables[ACCEL_FS_IPV6_TCP];
	break;
#endif
	default:
		kfree(spec);
		return ERR_PTR(-EINVAL);
	}

	if (!ft) {
		kfree(spec);
		return ERR_PTR(-EINVAL);
	}

	MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria,
			 outer_headers.tcp_dport);
	MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria,
			 outer_headers.tcp_sport);
	MLX5_SET(fte_match_param, spec->match_value, outer_headers.tcp_dport,
		 htons(inet_sk(sk)->inet_sport));
	MLX5_SET(fte_match_param, spec->match_value, outer_headers.tcp_sport,
		 htons(inet_sk(sk)->inet_dport));

	dest.type = MLX5_FLOW_DESTINATION_TYPE_TIR;
	dest.tir_num = tirn;
	if (flow_tag != MLX5_FS_DEFAULT_FLOW_TAG) {
		spec->flow_context.flow_tag = flow_tag;
		spec->flow_context.flags = FLOW_CONTEXT_HAS_TAG;
	}

	flow = mlx5_add_flow_rules(ft->t,
				   spec,
				   &flow_act,
				   &dest,
				   1);

	if (IS_ERR_OR_NULL(flow)) {
		netdev_err(priv->netdev,
			      "mlx5_add_flow_rules() failed, flow is %ld\n",
			      PTR_ERR(flow));
		flow = NULL;
	}

	kvfree(spec);

	return flow;
}

static int accel_fs_add_default_rule(struct mlx5e_priv *priv,
				     enum accel_fs_type type)
{
	struct mlx5e_flow_table *accel_fs_t =
		&priv->fs.accel.accel_tables[type];
	struct mlx5_flow_handle **default_rule =
		&priv->fs.accel.default_rules[type];
	struct mlx5e_tir *tir = priv->indir_tir;
	struct mlx5_flow_destination dest = {};
	MLX5_DECLARE_FLOW_ACT(flow_act);
	struct mlx5_flow_spec *spec;
	enum mlx5e_traffic_types tt = -EINVAL;
	int err = 0;

	spec = kvzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec) {
		err = -ENOMEM;
		goto out;
	}

	switch (type) {
	case ACCEL_FS_IPV4_TCP:
		tt = MLX5E_TT_IPV4_TCP;
		break;
	case ACCEL_FS_IPV6_TCP:
		tt = MLX5E_TT_IPV6_TCP;
		break;
#ifdef CONFIG_MLX5_IPSEC
	case ACCEL_FS_IPV4_ESP:
		tt = MLX5E_TT_IPV4_IPSEC_ESP;
		break;
	case ACCEL_FS_IPV6_ESP:
		tt = MLX5E_TT_IPV6_IPSEC_ESP;
		break;
#endif
	default:
		tt = -EINVAL;
	}
	if (tt == -EINVAL) {
		netdev_err(priv->netdev, "%s: bad accel_fs_type: %d\n",
			   __func__, type);
		err = -EINVAL;
		goto out;
	}

	dest.type = MLX5_FLOW_DESTINATION_TYPE_TIR;
	dest.tir_num = tir[tt].tirn;

	*default_rule = mlx5_add_flow_rules(accel_fs_t->t, spec,
					    &flow_act,
					    &dest, 1);
	if (IS_ERR(*default_rule)) {
		err = PTR_ERR(*default_rule);
		*default_rule = NULL;
		netdev_err(priv->netdev, "%s: add rule failed, accel_fs type=%d\n",
			   __func__, type);
	}
out:
	kvfree(spec);
	return err;
}

static int accel_fs_create_groups(struct mlx5e_flow_table *ft,
				  enum accel_fs_type type)
{
	int inlen = MLX5_ST_SZ_BYTES(create_flow_group_in);
	u8 match_criteria_enable;
	void *misc_parameters_c;
	void *outer_headers_c;
	int ix = 0;
	u32 *in;
	int err;
	u8 *mc;

	ft->g = kcalloc(MLX5E_ACCEL_FS_NUM_GROUPS,
			sizeof(*ft->g), GFP_KERNEL);
	in = kvzalloc(inlen, GFP_KERNEL);
	if  (!in || !ft->g) {
		kvfree(ft->g);
		kvfree(in);
		return -ENOMEM;
	}

	mc = MLX5_ADDR_OF(create_flow_group_in, in, match_criteria);
	outer_headers_c = MLX5_ADDR_OF(fte_match_param, mc,
				       outer_headers);
	misc_parameters_c = MLX5_ADDR_OF(fte_match_param, mc, misc_parameters);

	MLX5_SET_TO_ONES(fte_match_set_lyr_2_4, outer_headers_c, ethertype);
	switch (type) {
	case ACCEL_FS_IPV4_TCP:
	case ACCEL_FS_IPV6_TCP:
		MLX5_SET_TO_ONES(fte_match_set_lyr_2_4, outer_headers_c, tcp_dport);
		MLX5_SET_TO_ONES(fte_match_set_lyr_2_4, outer_headers_c, tcp_sport);
		match_criteria_enable = MLX5_MATCH_OUTER_HEADERS;
		break;
#ifdef CONFIG_MLX5_IPSEC
	case ACCEL_FS_IPV4_ESP:
	case ACCEL_FS_IPV6_ESP:
		MLX5_SET_TO_ONES(fte_match_set_lyr_2_4, outer_headers_c, ip_protocol);
		MLX5_SET_TO_ONES(fte_match_set_lyr_2_4, outer_headers_c, frag);
		MLX5_SET_TO_ONES(fte_match_set_misc, misc_parameters_c,
				 outer_esp_spi);
		match_criteria_enable = MLX5_MATCH_OUTER_HEADERS | MLX5_MATCH_MISC_PARAMETERS;
		break;
#endif
	default:
		err = -EINVAL;
		goto out;
	}

	switch (type) {
	case ACCEL_FS_IPV4_TCP:
#ifdef CONFIG_MLX5_IPSEC
	case ACCEL_FS_IPV4_ESP:
#endif
		MLX5_SET_TO_ONES(fte_match_set_lyr_2_4, outer_headers_c,
				 src_ipv4_src_ipv6.ipv4_layout.ipv4);
		MLX5_SET_TO_ONES(fte_match_set_lyr_2_4, outer_headers_c,
				 dst_ipv4_dst_ipv6.ipv4_layout.ipv4);
		break;
	case ACCEL_FS_IPV6_TCP:
#ifdef CONFIG_MLX5_IPSEC
	case ACCEL_FS_IPV6_ESP:
#endif
		memset(MLX5_ADDR_OF(fte_match_set_lyr_2_4, outer_headers_c,
				    src_ipv4_src_ipv6.ipv6_layout.ipv6),
		       0xff, 16);
		memset(MLX5_ADDR_OF(fte_match_set_lyr_2_4, outer_headers_c,
				    dst_ipv4_dst_ipv6.ipv6_layout.ipv6),
		       0xff, 16);
		break;
	default:
		err = -EINVAL;
		goto out;
	}

	MLX5_SET_CFG(in, match_criteria_enable, match_criteria_enable);
	MLX5_SET_CFG(in, start_flow_index, ix);
	ix += MLX5E_ACCEL_FS_GROUP1_SIZE;
	MLX5_SET_CFG(in, end_flow_index, ix - 1);
	ft->g[ft->num_groups] = mlx5_create_flow_group(ft->t, in);
	if (IS_ERR(ft->g[ft->num_groups]))
		goto err;
	ft->num_groups++;

	/* Default Flow Group */
	memset(in, 0, inlen);
	MLX5_SET_CFG(in, start_flow_index, ix);
	ix += MLX5E_ACCEL_FS_GROUP2_SIZE;
	MLX5_SET_CFG(in, end_flow_index, ix - 1);
	ft->g[ft->num_groups] = mlx5_create_flow_group(ft->t, in);
	if (IS_ERR(ft->g[ft->num_groups]))
		goto err;
	ft->num_groups++;

	kvfree(in);
	return 0;

err:
	err = PTR_ERR(ft->g[ft->num_groups]);
	ft->g[ft->num_groups] = NULL;
out:
	kvfree(in);

	return err;
}

static int accel_fs_create_table(struct mlx5e_priv *priv,
				 enum accel_fs_type type)
{
	struct mlx5e_flow_table *ft = &priv->fs.accel.accel_tables[type];
	struct mlx5_flow_table_attr ft_attr = {};
	int err;

	ft->num_groups = 0;

	ft_attr.max_fte = MLX5E_ACCEL_FS_TABLE_SIZE;
	ft_attr.level = MLX5E_ACCEL_FS_FT_LEVEL;
	ft_attr.prio = MLX5E_NIC_PRIO;

	ft->t = mlx5_create_flow_table(priv->fs.ns, &ft_attr);
	if (IS_ERR(ft->t)) {
		err = PTR_ERR(ft->t);
		ft->t = NULL;
		return err;
	}
	netdev_dbg(priv->netdev, "Created fs accel table id %u level %u\n", ft->t->id, ft->t->level);

	err = accel_fs_create_groups(ft, type);
	if (err)
		goto err;

	err = accel_fs_add_default_rule(priv, type);
	if (err)
		goto err;

	return 0;
err:
	mlx5e_destroy_flow_table(ft);
	return err;
}

static int fs_accel2tt(enum accel_fs_type i)
{
	switch (i) {
	case ACCEL_FS_IPV4_TCP:
		return MLX5E_TT_IPV4_TCP;
	case ACCEL_FS_IPV6_TCP:
		return MLX5E_TT_IPV6_TCP;
#ifdef CONFIG_MLX5_IPSEC
	case ACCEL_FS_IPV4_ESP:
		return MLX5E_TT_IPV4_IPSEC_ESP;
	case ACCEL_FS_IPV6_ESP:
		return MLX5E_TT_IPV6_IPSEC_ESP;
#endif
	default:
		return -EINVAL;
	}
}

static int mlx5e_accel_fs_disable(struct mlx5e_priv *priv)
{
	struct mlx5_flow_destination dest = {};
	int err = 0;
	int i;

	dest.type = MLX5_FLOW_DESTINATION_TYPE_TIR;

	for (i = 0; i < ACCEL_FS_NUM_TYPES; i++) {
		struct mlx5_flow_handle *handle = NULL;

		if (!is_accel_type_supported(priv->mdev, i))
			continue;

		int tt = fs_accel2tt(i);

		if (tt < 0)
			return -EINVAL;

		handle = priv->fs.ttc.rules[tt];
		dest.tir_num = priv->indir_tir[tt].tirn;

		/* Modify ttc rules destination to point back to the indir TIRs */
		err = mlx5_modify_rule_destination(handle, &dest, NULL);
		if (err) {
			netdev_err(priv->netdev,
				   "%s: modify ttc destination failed err=%d\n",
				   __func__, err);
			return err;
		}
	}

	return 0;
}

void mlx5e_accel_fs_destroy_tables(struct mlx5e_priv *priv)
{
	int i;

	mlx5e_accel_fs_disable(priv);

#ifdef CONFIG_MLX5_IPSEC
	mlx5e_ipsec_destroy_rx_err_ft(priv);
#endif

	for (i = 0; i < ACCEL_FS_NUM_TYPES; i++) {
		if (!IS_ERR_OR_NULL(priv->fs.accel.accel_tables[i].t)) {
			mlx5_del_flow_rules(priv->fs.accel.default_rules[i]);
			mlx5e_destroy_flow_table(&priv->fs.accel.accel_tables[i]);
			priv->fs.accel.accel_tables[i].t = NULL;
		}
	}
}

static int mlx5e_accel_fs_enable(struct mlx5e_priv *priv)
{
	struct mlx5_flow_destination dest = {};
	int err = 0;
	int i;

	dest.type = MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE;
	for (i = 0; i < ACCEL_FS_NUM_TYPES; i++) {
		struct mlx5_flow_handle *handle = NULL;

		if (!is_accel_type_supported(priv->mdev, i))
			continue;

		int tt = fs_accel2tt(i);

		if (tt < 0)
			return -EINVAL;

		handle = priv->fs.ttc.rules[tt];
		dest.ft = priv->fs.accel.accel_tables[i].t;

		/* Modify ttc rules destination to point on the accel_fs FTs */
		err = mlx5_modify_rule_destination(handle, &dest, NULL);
		if (err) {
			netdev_err(priv->netdev,
				   "%s: modify ttc destination failed err=%d\n",
				   __func__, err);
			return err;
		}
	}
	return 0;
}

int mlx5e_accel_fs_create_tables(struct mlx5e_priv *priv)
{
	int i, err;

	for (i = 0; i < ACCEL_FS_NUM_TYPES; i++) {
		if (!is_accel_type_supported(priv->mdev, i))
			continue;

		err = accel_fs_create_table(priv, i);
		if (err)
			goto err;
	}

#ifdef CONFIG_MLX5_IPSEC
	err = mlx5e_ipsec_create_rx_err_ft(priv);
	if (err)
		goto err;
#endif

	err = mlx5e_accel_fs_enable(priv);
	if (err)
		goto err;

	return 0;

err:
	mlx5e_accel_fs_destroy_tables(priv);
	return err;
}
