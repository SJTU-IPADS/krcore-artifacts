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

#ifndef __MLX5_EN_TC_H__
#define __MLX5_EN_TC_H__

#include <net/pkt_cls.h>
#include <net/ip_tunnels.h>
#include <net/vxlan.h>
#include "eswitch.h"

#define MLX5E_TC_FLOW_ID_MASK 0x0000ffff

#ifdef CONFIG_MLX5_ESWITCH

enum {
	MLX5E_TC_FLAG_INGRESS_BIT,
	MLX5E_TC_FLAG_EGRESS_BIT,
	MLX5E_TC_FLAG_NIC_OFFLOAD_BIT,
	MLX5E_TC_FLAG_ESW_OFFLOAD_BIT,
	MLX5E_TC_FLAG_LAST_EXPORTED_BIT = MLX5E_TC_FLAG_ESW_OFFLOAD_BIT,
};

#define MLX5_TC_FLAG(flag) BIT(MLX5E_TC_FLAG_##flag##_BIT)
#define MLX5E_TC_FLOW_BASE (MLX5E_TC_FLAG_LAST_EXPORTED_BIT + 1)

enum {
	MLX5E_TC_FLOW_FLAG_INGRESS	= MLX5E_TC_FLAG_INGRESS_BIT,
	MLX5E_TC_FLOW_FLAG_EGRESS	= MLX5E_TC_FLAG_EGRESS_BIT,
	MLX5E_TC_FLOW_FLAG_ESWITCH	= MLX5E_TC_FLAG_ESW_OFFLOAD_BIT,
	MLX5E_TC_FLOW_FLAG_NIC		= MLX5E_TC_FLAG_NIC_OFFLOAD_BIT,
	MLX5E_TC_FLOW_FLAG_OFFLOADED	= MLX5E_TC_FLOW_BASE,
	MLX5E_TC_FLOW_FLAG_HAIRPIN	= MLX5E_TC_FLOW_BASE + 1,
	MLX5E_TC_FLOW_FLAG_HAIRPIN_RSS	= MLX5E_TC_FLOW_BASE + 2,
	MLX5E_TC_FLOW_FLAG_SLOW		= MLX5E_TC_FLOW_BASE + 3,
	MLX5E_TC_FLOW_FLAG_DUP		= MLX5E_TC_FLOW_BASE + 4,
	MLX5E_TC_FLOW_FLAG_NOT_READY	= MLX5E_TC_FLOW_BASE + 5,
	MLX5E_TC_FLOW_FLAG_DELETED	= MLX5E_TC_FLOW_BASE + 6,
	MLX5E_TC_FLOW_FLAG_SIMPLE       = MLX5E_TC_FLOW_BASE + 7,
	MLX5E_TC_FLOW_FLAG_CT           = MLX5E_TC_FLOW_BASE + 8,
	MLX5E_TC_FLOW_FLAG_CT_ORIG      = MLX5E_TC_FLOW_BASE + 9,
};

#define MLX5E_TC_MAX_SPLITS 1

struct mlx5_nic_flow_attr {
	u32 action;
	u32 flow_tag;
	struct mlx5_modify_hdr *modify_hdr;
	u32 hairpin_tirn;
	u8 match_level;
	struct mlx5_flow_table	*hairpin_ft;
	struct mlx5_fc		*counter;
};

/* Helper struct for accessing a struct containing list_head array.
 * Containing struct
 *   |- Helper array
 *      [0] Helper item 0
 *          |- list_head item 0
 *          |- index (0)
 *      [1] Helper item 1
 *          |- list_head item 1
 *          |- index (1)
 * To access the containing struct from one of the list_head items:
 * 1. Get the helper item from the list_head item using
 *    helper item =
 *        container_of(list_head item, helper struct type, list_head field)
 * 2. Get the contining struct from the helper item and its index in the array:
 *    containing struct =
 *        container_of(helper item, containing struct type, helper field[index])
 */
struct encap_flow_item {
	struct mlx5e_encap_entry *e; /* attached encap instance */
	struct list_head list;
	int index;
};

struct mlx5e_tc_flow {
	struct rhash_head	node;
	struct mlx5e_priv	*priv;
	u64			cookie;
	unsigned long		flags;
	struct mlx5_flow_handle *rule[MLX5E_TC_MAX_SPLITS + 1];
	/* Flow can be associated with multiple encap IDs.
	 * The number of encaps is bounded by the number of supported
	 * destinations.
	 */
	struct encap_flow_item encaps[MLX5_MAX_FLOW_FWD_VPORTS];
	struct mlx5e_tc_flow    *peer_flow;
	struct mlx5e_mod_hdr_entry *mh; /* attached mod header instance */
	struct list_head	mod_hdr; /* flows sharing the same mod hdr ID */
	struct mlx5e_hairpin_entry *hpe; /* attached hairpin instance */
	struct list_head	hairpin; /* flows sharing the same hairpin */
	struct list_head	peer;    /* flows with peer flow */
	struct list_head	unready; /* flows not ready to be offloaded (e.g due to missing route) */
	int			tmp_efi_index;
	struct list_head	tmp_list; /* temporary flow list used by neigh update */
	struct net_device      *added_dev;
	refcount_t		refcnt;
	struct rcu_head		rcu_head;
	struct completion	init_done;

	u64			version;
	struct mlx5e_miniflow   *miniflow;
	struct mlx5_fc          *dummy_counter;
	struct list_head        miniflow_list;
	struct rcu_head		rcu;
	struct list_head        nft_node;
	spinlock_t		*dep_lock;

	union {
		struct mlx5_esw_flow_attr esw_attr[0];
		struct mlx5_nic_flow_attr nic_attr[0];
	};
};

struct mlx5e_tc_flow_parse_attr {
	const struct ip_tunnel_info *tun_info[MLX5_MAX_FLOW_FWD_VPORTS];
	struct ip_tunnel_info tun_info2[MLX5_MAX_FLOW_FWD_VPORTS];
	struct net_device *filter_dev;
	struct mlx5_flow_spec spec;
	int num_mod_hdr_actions;
	int max_mod_hdr_actions;
	void *mod_hdr_actions;
	int mirred_ifindex[MLX5_MAX_FLOW_FWD_VPORTS];
};

#define MLX5_MH_ACT_SZ MLX5_UN_SZ_BYTES(set_add_copy_action_in_auto)

struct pedit_headers {
	struct ethhdr  eth;
	struct vlan_hdr vlan;
	struct iphdr   ip4;
	struct ipv6hdr ip6;
	struct tcphdr  tcp;
	struct udphdr  udp;
};

struct pedit_headers_action {
	struct pedit_headers	vals;
	struct pedit_headers	masks;
	u32			pedits;
};

int mlx5e_tc_nic_init(struct mlx5e_priv *priv);
void mlx5e_tc_nic_cleanup(struct mlx5e_priv *priv);

int mlx5e_tc_esw_init(struct mlx5e_priv *priv);
void mlx5e_tc_esw_cleanup(struct mlx5e_priv *priv);

int mlx5e_configure_flower(struct net_device *dev, struct mlx5e_priv *priv,
			   struct tc_cls_flower_offload *f, unsigned long flags);
int mlx5e_delete_flower(struct net_device *dev, struct mlx5e_priv *priv,
			struct tc_cls_flower_offload *f, unsigned long flags);

int mlx5e_stats_flower(struct net_device *dev, struct mlx5e_priv *priv,
		       struct tc_cls_flower_offload *f, unsigned long flags);

struct mlx5e_encap_entry;
void mlx5e_tc_encap_flows_add(struct mlx5e_priv *priv,
			      struct mlx5e_encap_entry *e,
			      struct list_head *flow_list);
void mlx5e_tc_encap_flows_del(struct mlx5e_priv *priv,
			      struct mlx5e_encap_entry *e,
			      struct list_head *flow_list);
bool mlx5e_encap_take(struct mlx5e_encap_entry *e);
void mlx5e_encap_put(struct mlx5e_priv *priv, struct mlx5e_encap_entry *e);

void mlx5e_take_all_encap_flows(struct mlx5e_encap_entry *e, struct list_head *flow_list);
void mlx5e_put_encap_flow_list(struct mlx5e_priv *priv, struct list_head *flow_list);

struct mlx5e_neigh_hash_entry;
void mlx5e_tc_update_neigh_used_value(struct mlx5e_neigh_hash_entry *nhe);

int mlx5e_tc_num_filters(struct mlx5e_priv *priv, unsigned long flags);

void mlx5e_tc_reoffload_flows_work(struct work_struct *work);

void *mlx5e_lookup_tc_ht(struct mlx5e_priv *priv,
			 unsigned long *cookie,
			 int flags);
void mlx5e_flow_put(struct mlx5e_priv *priv,
		    struct mlx5e_tc_flow *flow);
void mlx5e_flow_put_lock(struct mlx5e_priv *priv,
		    struct mlx5e_tc_flow *flow, bool lock);
int mlx5e_tc_add_fdb_flow(struct mlx5e_priv *priv,
			  struct mlx5e_tc_flow *flow,
			  struct netlink_ext_ack *extack);
int mlx5e_alloc_flow(struct mlx5e_priv *priv, int attr_size,
		     u64 cookie, unsigned long flow_flags, gfp_t flags,
		     struct mlx5e_tc_flow_parse_attr **__parse_attr,
		     struct mlx5e_tc_flow **__flow);
int alloc_mod_hdr_actions(struct mlx5e_priv *priv,
			  struct pedit_headers_action *hdrs,
			  int namespace,
			  struct mlx5e_tc_flow_parse_attr *parse_attr,
			  gfp_t flags);
int parse_tc_pedit_action(struct mlx5e_priv *priv,
			  const struct flow_action_entry *act, int namespace,
			  struct mlx5e_tc_flow_parse_attr *parse_attr,
			  struct pedit_headers_action *hdrs,
			  struct netlink_ext_ack *extack);
int alloc_tc_pedit_action(struct mlx5e_priv *priv, int namespace,
			  struct mlx5e_tc_flow_parse_attr *parse_attr,
			  struct pedit_headers_action *hdrs,
			  u32 *action_flags,
			  struct netlink_ext_ack *extack);
bool mlx5e_is_valid_eswitch_fwd_dev(struct mlx5e_priv *priv,
				    struct net_device *out_dev);

#else /* CONFIG_MLX5_ESWITCH */
static inline int  mlx5e_tc_nic_init(struct mlx5e_priv *priv) { return 0; }
static inline void mlx5e_tc_nic_cleanup(struct mlx5e_priv *priv) {}
static inline int  mlx5e_tc_num_filters(struct mlx5e_priv *priv,
					unsigned long flags)
{
	return 0;
}
#endif

#endif /* __MLX5_EN_TC_H__ */
