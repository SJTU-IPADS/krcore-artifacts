/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2019 Mellanox Technologies. */

#ifndef __MLX5_MINIFLOW_H__
#define __MLX5_MINIFLOW_H__

#include <net/netfilter/nf_conntrack.h>
#include "fs_core.h"
#include "en_tc.h"

#ifdef HAVE_MINIFLOW

#define MFC_INFOMASK	7UL
#define MFC_PTRMASK  	~(MFC_INFOMASK)
#define MFC_CT_FLOW     BIT(0)

#define MINIFLOW_MAX_CT_TUPLES 6

struct mlx5e_ct_tuple {
	struct net *net;
	struct nf_conntrack_tuple tuple;
	struct nf_conntrack_zone zone;
	unsigned long nat;
	__be32 ipv4;
	__be16 port;
	__u8 proto;

	struct mlx5e_tc_flow *flow;
};

struct mlx5e_miniflow_node {
	struct list_head node;
	struct mlx5e_miniflow *miniflow;
};

struct mlx5e_miniflow {
	struct rhash_head node;
	struct work_struct work;
	struct mlx5e_priv *priv;
	struct mlx5e_tc_flow *flow;
	struct rhashtable *mf_ht;

	struct nf_conntrack_tuple tuple;

	u64 version;
	int nr_flows;
	struct {
		unsigned long        cookies[MINIFLOW_MAX_FLOWS];
		struct mlx5e_tc_flow *flows[MINIFLOW_MAX_FLOWS];
	} path;

	int nr_ct_tuples;
	unsigned long cleanup;
	struct mlx5e_ct_tuple ct_tuples[MINIFLOW_MAX_CT_TUPLES];

	struct mlx5e_miniflow_node mnodes[MINIFLOW_MAX_FLOWS];
};

u64 miniflow_version_inc(void);

void mlx5e_del_miniflow_list(struct mlx5e_tc_flow *flow);

int miniflow_cache_init(struct mlx5e_priv *priv);
void miniflow_cache_destroy(struct mlx5e_priv *priv);
int miniflow_configure_ct(struct mlx5e_priv *priv,
			  struct tc_ct_offload *cto);
int miniflow_configure(struct mlx5e_priv *priv,
		       struct tc_miniflow_offload *mf);

int mlx5_ct_flow_offload_table_init(void);
void mlx5_ct_flow_offload_table_destroy(void);

int mlx5_ct_flow_offload_add(const struct net *net,
			     const struct nf_conntrack_zone *zone,
			     const struct nf_conntrack_tuple *tuple,
			     struct mlx5e_tc_flow *tc_flow);

int mlx5_ct_flow_offload_remove(const struct net *net,
				const struct nf_conntrack_zone *zone,
				const struct nf_conntrack_tuple *tuple);

int mlx5_ct_flow_offloaded_count(void);

int ct_flow_offload_add(void *arg, struct list_head *head);
void ct_flow_offload_get_stats(struct list_head *head, u64 *lastuse);
int ct_flow_offload_destroy(struct list_head *head);

#else /* HAVE_MINIFLOW */

static inline void mlx5e_del_miniflow_list(struct mlx5e_tc_flow *flow) {}
static inline int miniflow_cache_init(struct mlx5e_priv *priv) { return 0; }
static inline void miniflow_cache_destroy(struct mlx5e_priv *priv) {}

#endif /* HAVE_MINIFLOW */
#endif /* __MLX5_MINIFLOW_H__ */
