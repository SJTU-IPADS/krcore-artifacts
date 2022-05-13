/*
 * Copyright (c) 2015, Mellanox Technologies, Ltd.  All rights reserved.
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

#ifndef __MLX5_ESWITCH_H__
#define __MLX5_ESWITCH_H__

#include <linux/if_ether.h>
#include <linux/if_link.h>
#include <linux/atomic.h>
#include <linux/if_vlan.h>
#include <linux/bitmap.h>
#include <net/devlink.h>
#include <linux/mlx5/device.h>
#include <linux/mlx5/eswitch.h>
#include <linux/mlx5/vport.h>
#include <linux/mlx5/fs.h>
#include "lib/mpfs.h"
#include "mlx5_core.h"
#include "meddev/sf.h"

#ifdef CONFIG_MLX5_ESWITCH
enum {
	MLX5_ESW_UPLINK_REP_MODE_NEW_NETDEV,
	MLX5_ESW_UPLINK_REP_MODE_NIC_NETDEV,
};

#define MLX5_MAX_UC_PER_VPORT(dev) \
	(1 << MLX5_CAP_GEN(dev, log_max_current_uc_list))

#define MLX5_MAX_MC_PER_VPORT(dev) \
	(1 << MLX5_CAP_GEN(dev, log_max_current_mc_list))

#define MLX5_MAX_VLAN_PER_VPORT(dev) \
	(1 << MLX5_CAP_GEN(dev, log_max_vlan_list))

#define MLX5_MIN_BW_SHARE 1

#define MLX5_RATE_TO_BW_SHARE(rate, divider, limit) \
	min_t(u32, max_t(u32, DIV_ROUND_UP(rate, divider), MLX5_MIN_BW_SHARE), limit)

#define mlx5_esw_has_fwd_fdb(dev) \
	MLX5_CAP_ESW_FLOWTABLE(dev, fdb_multi_path_to_table)

#define FDB_MAX_CHAIN 3
#define FDB_SLOW_PATH_CHAIN (FDB_MAX_CHAIN + 1)
#define FDB_MAX_PRIO 16

#define VHCA_VPORT_MATCH_ID_BITS 16
#define VHCA_VPORT_MATCH_ID_SIZE BIT(VHCA_VPORT_MATCH_ID_BITS)
#define GEN_METADATA(match_id)	((match_id) << (32 - VHCA_VPORT_MATCH_ID_BITS))
#define GEN_MATCH_ID(metadata)	((metadata) >> (32 - VHCA_VPORT_MATCH_ID_BITS))

struct vport_ingress {
	struct mlx5_flow_table *acl;
	struct mlx5_flow_table *saved_orig_acl;
	struct mlx5_flow_group *allow_untagged_spoofchk_grp;
	struct mlx5_flow_group *allow_tagged_spoofchk_grp;
	struct mlx5_flow_group *drop_grp;
	struct mlx5_modify_hdr   *modify_metadata;
	struct mlx5_flow_handle  *modify_metadata_rule;
	struct mlx5_flow_handle  *allow_untagged_rule;
	struct list_head         allow_vlans_rules;
 	struct mlx5_flow_handle  *drop_rule;
	struct mlx5_fc           *drop_counter;
	struct mlx5_flow_handle  *allow_rule;
};

struct vport_egress {
	struct mlx5_flow_table *acl;
	struct mlx5_flow_group *allow_untagged_grp;
	struct mlx5_flow_group *allowed_vlans_grp;
	struct mlx5_flow_group *drop_grp;
	struct mlx5_flow_group *bounce_grp;
	struct mlx5_flow_handle  *allow_vst_vlan;
	struct mlx5_flow_handle  *drop_rule;
	struct mlx5_flow_handle  *allow_untagged_rule;
	struct mlx5_flow_handle *bounce_rule;
	struct list_head        allow_vlans_rules;
	struct mlx5_fc           *drop_counter;
	struct mlx5_flow_handle  *allowed_vlan;
	struct mlx5_flow_handle  *fwd_rule;
};

struct mlx5_vport_drop_stats {
	u64 rx_dropped;
	u64 tx_dropped;
};

struct mlx5_vport_info {
	u8                      mac[ETH_ALEN];
	u16                     vlan;
	u8                      qos;
	__be16			vlan_proto;
	u64                     node_guid;
	int                     link_state;
	u32                     min_rate;
	u32                     max_rate;
	bool                    spoofchk;
	bool                    trusted;
	bool                    roce;
	/* the admin approved vlan list */
	DECLARE_BITMAP(vlan_trunk_8021q_bitmap, VLAN_N_VID);
	u32			group;
};

/* Vport context events */
enum mlx5_eswitch_vport_event {
	MLX5_VPORT_UC_ADDR_CHANGE = BIT(0),
	MLX5_VPORT_MC_ADDR_CHANGE = BIT(1),
	MLX5_VPORT_VLAN_CHANGE = BIT(2),
	MLX5_VPORT_PROMISC_CHANGE = BIT(3),
};

struct mlx5_vport {
	struct mlx5_core_dev    *dev;
	int                     vport;
	struct hlist_head       uc_list[MLX5_L2_ADDR_HASH_SIZE];
	struct hlist_head       mc_list[MLX5_L2_ADDR_HASH_SIZE];
	/* The requested vlan list from the vport side */
	DECLARE_BITMAP(req_vlan_bitmap, VLAN_N_VID);
	/* Actual accepted vlans on the acl tables */
	DECLARE_BITMAP(acl_vlan_8021q_bitmap, VLAN_N_VID);
	struct mlx5_flow_handle *promisc_rule;
	struct mlx5_flow_handle *allmulti_rule;
	struct work_struct      vport_change_handler;

	struct vport_ingress    ingress;
	struct vport_egress     egress;

	struct mlx5_vport_info  info;

	struct {
		bool            enabled;
		u32             esw_tsar_ix;
		u32             bw_share;
		struct mlx5_vgroup *group;
	} qos;

	bool                    enabled;
	enum mlx5_eswitch_vport_event enabled_events;
	u16 match_id;
	u32 bond_metadata;
};

enum offloads_fdb_flags {
	ESW_FDB_CHAINS_AND_PRIOS_SUPPORTED = BIT(0),
};

extern const unsigned int ESW_POOLS[4];

#define PRIO_LEVELS 2
struct mlx5_eswitch_fdb {
	union {
		struct legacy_fdb {
			struct mlx5_flow_table *fdb;
			struct mlx5_flow_group *addr_grp;
			struct mlx5_flow_group *allmulti_grp;
			struct mlx5_flow_group *promisc_grp;
			struct mlx5_flow_table *vepa_fdb;
			struct mlx5_flow_handle *vepa_uplink_rule;
			struct mlx5_flow_handle *vepa_star_rule;
		} legacy;

		struct offloads_fdb {
			struct mlx5_flow_namespace *ns;
			struct mlx5_flow_table *slow_fdb;
			struct mlx5_flow_group *send_to_vport_grp;
			struct mlx5_flow_group *peer_miss_grp;
			struct mlx5_flow_handle **peer_miss_rules;
			struct mlx5_flow_group *miss_grp;
			struct mlx5_flow_handle *miss_rule_uni;
			struct mlx5_flow_handle *miss_rule_multi;
			int vlan_push_pop_refcount;

			struct {
				struct mlx5_flow_table *fdb;
				u32 num_rules;
			} fdb_prio[FDB_MAX_CHAIN + 1][FDB_MAX_PRIO + 1][PRIO_LEVELS];
			/* Protects fdb tables */
			struct mutex fdb_lock;

			int fdb_left[ARRAY_SIZE(ESW_POOLS)];
		} offloads;
	};
	u32 flags;
};

struct mlx5_acl_vlan {
	struct mlx5_flow_handle	*acl_vlan_rule;
	struct list_head	list;
};

struct mlx5_esw_offload {
	struct mlx5_flow_table *ft_offloads;
	struct mlx5_flow_group *vport_rx_group;
	struct mlx5_eswitch_rep *vport_reps;
	struct list_head peer_flows;
	struct mutex peer_mutex;
	struct mutex encap_tbl_lock; /* protects encap_tbl */
	DECLARE_HASHTABLE(encap_tbl, 8);
	struct mod_hdr_tbl mod_hdr;
	const struct mlx5_eswitch_rep_ops *rep_ops[NUM_REP_TYPES];
	u8 inline_mode;
	atomic64_t num_flows;
	u8 encap;
	int uplink_rep_mode;
	struct list_head rep_bond_metadata_list;
};

/* E-Switch MC FDB table hash node */
struct esw_mc_addr { /* SRIOV only */
	struct l2addr_node     node;
	struct mlx5_flow_handle *uplink_rule; /* Forward to uplink rule */
	u32                    refcnt;
};

struct mlx5_host_work {
	struct work_struct	work;
	struct mlx5_eswitch	*esw;
};

struct mlx5_esw_functions {
	struct mlx5_nb		nb;
	u16			num_vfs;
};

struct mlx5_esw_handler {
	atomic_t		in_progress;
	struct work_struct      start_handler;
	struct work_struct      stop_handler;
	struct netlink_ext_ack	*extack;
	struct mlx5_lag		*ldev;
};

struct mlx5_vgroup {
	struct mlx5_core_dev *dev;
	u32		     group_id;
	u32		     num_vports;
	u32		     tsar_ix;
	u32		     max_rate;
	u32		     min_rate;
	u32		     bw_share;
	struct		     kobject kobj;
	struct		     list_head list;
};

struct mlx5_smart_nic_vport {
	struct mlx5_eswitch    *esw;
	struct kobject          kobj;
	int                     vport;
};

struct mlx5_smart_nic_sysfs {
	struct kobject              *kobj;
	struct mlx5_smart_nic_vport *vport;
};

enum {
	MLX5_ESWITCH_VPORT_MATCH_METADATA = BIT(0),
};

struct mlx5_mdev_table;

struct mlx5_eswitch {
	struct mlx5_core_dev    *dev;
	struct mlx5_core_dev    *peer_dev;
	struct mlx5_nb          nb;
	/* legacy data structures */
	struct mlx5_eswitch_fdb fdb_table;
	struct hlist_head       mc_table[MLX5_L2_ADDR_HASH_SIZE];
	struct esw_mc_addr mc_promisc;
	/* end of legacy */
	struct workqueue_struct *work_queue;
	struct mlx5_vport       *vports;
	u32                     flags;
	int                     total_vports;
	int                     enabled_vports;
	/* Synchronize between vport change events
	 * and async SRIOV admin state changes
	 */
	struct mutex            state_lock;

	/* Protects eswitch mode changes occurring via sriov
	 * state change, devlink commands.
	 */
	struct mutex mode_lock;

	struct {
		bool            enabled;
		u32             root_tsar_ix;
		struct mlx5_vgroup *group0;
		struct list_head groups;
	} qos;

	struct mlx5_esw_offload offloads;
	int                     mode;
	int                     nvports;
	u16                     manager_vport;
	u16                     first_host_vport;
	struct mlx5_esw_functions esw_funcs;
#ifdef CONFIG_MLX5_MDEV
	struct mlx5_mdev_table *mdev_table;
#endif
	struct mlx5_esw_handler	handler;
	struct mlx5_smart_nic_sysfs smart_nic_sysfs;
};

void esw_offloads_disable(struct mlx5_eswitch *esw);
int esw_offloads_enable(struct mlx5_eswitch *esw);
void esw_offloads_cleanup_reps(struct mlx5_eswitch *esw);
int esw_offloads_init_reps(struct mlx5_eswitch *esw);
void esw_vport_cleanup_ingress_rules(struct mlx5_eswitch *esw,
				     struct mlx5_vport *vport);
int esw_vport_enable_ingress_acl(struct mlx5_eswitch *esw,
				 struct mlx5_vport *vport);
void esw_vport_cleanup_egress_rules(struct mlx5_eswitch *esw,
				    struct mlx5_vport *vport);
int esw_vport_enable_egress_acl(struct mlx5_eswitch *esw,
				struct mlx5_vport *vport);
void esw_vport_disable_egress_acl(struct mlx5_eswitch *esw,
				  struct mlx5_vport *vport);
void esw_vport_disable_ingress_acl(struct mlx5_eswitch *esw,
				   struct mlx5_vport *vport);

void esw_offloads_start_handler(struct work_struct *work);
void esw_offloads_stop_handler(struct work_struct *work);

void esw_vport_del_ingress_acl_modify_metadata(struct mlx5_eswitch *esw,
					       struct mlx5_vport *vport);
int esw_sf_vport_add_fdb_peer_miss_rule(struct mlx5_eswitch *esw,
					struct mlx5_vport *vport);
void esw_sf_vport_del_fdb_peer_miss_rule(struct mlx5_eswitch *esw,
					 struct mlx5_vport *vport);
int esw_set_egress_fwd2vport(struct mlx5_eswitch *esw, u16 esw_vport_num,
			     u16 dst_vport_num);
void esw_del_egress_fwd2vport(struct mlx5_eswitch *esw, u16 esw_vport_num);
void esw_replace_ingress_match_metadata(struct mlx5_eswitch *esw,
					u16 esw_vport_num, u32 metadata);
u16 esw_get_unique_match_id(void);
void esw_free_unique_match_id(u16 match_id);

/* E-Switch API */
int mlx5_eswitch_init(struct mlx5_core_dev *dev);
void mlx5_eswitch_cleanup(struct mlx5_eswitch *esw);
int mlx5_eswitch_enable_locked(struct mlx5_eswitch *esw, int mode, int num_vfs);
int mlx5_eswitch_enable(struct mlx5_eswitch *esw, int mode, int num_vfs);
void mlx5_eswitch_disable_locked(struct mlx5_eswitch *esw, bool clear_vf);
void mlx5_eswitch_disable(struct mlx5_eswitch *esw, bool clear_vf);
int mlx5_eswitch_set_vport_state(struct mlx5_eswitch *esw,
				 u16 vport, int link_state);
int mlx5_eswitch_set_vport_vlan(struct mlx5_eswitch *esw,
				int vport, u16 vlan, u8 qos, __be16 vlan_proto);
int mlx5_eswitch_set_vport_spoofchk(struct mlx5_eswitch *esw,
				    u16 vport, bool spoofchk);
int mlx5_eswitch_set_vport_trust(struct mlx5_eswitch *esw,
				 u16 vport_num, bool setting);
int mlx5_eswitch_set_vport_rate(struct mlx5_eswitch *esw, u16 vport,
				u32 max_rate, u32 min_rate);
int mlx5_eswitch_set_vepa(struct mlx5_eswitch *esw, u8 setting);
int mlx5_eswitch_get_vepa(struct mlx5_eswitch *esw, u8 *setting);
int mlx5_eswitch_get_vport_config(struct mlx5_eswitch *esw,
				  u16 vport, struct ifla_vf_info *ivi);
int mlx5_eswitch_get_vport_stats(struct mlx5_eswitch *esw,
				 u16 vport,
				 struct ifla_vf_stats *vf_stats);
int mlx5_eswitch_vport_update_group(struct mlx5_eswitch *esw, int vport_num,
				    u32 group_id);
int mlx5_eswitch_set_vgroup_rate(struct mlx5_eswitch *esw, int group_id,
				 u32 max_rate);
int mlx5_eswitch_set_vgroup_max_rate(struct mlx5_eswitch *esw, int group_id,
				     u32 max_rate);
int mlx5_eswitch_set_vgroup_min_rate(struct mlx5_eswitch *esw, int group_id,
				     u32 min_rate);
void mlx5_eswitch_enable_vport(struct mlx5_eswitch *esw,
			       struct mlx5_vport *vport,
			       enum mlx5_eswitch_vport_event enable_events);
void mlx5_eswitch_disable_vport(struct mlx5_eswitch *esw,
				struct mlx5_vport *vport);

#ifndef HAVE_STRUCT_IFLA_VF_STATS_TX_BROADCAST
struct ifla_vf_stats_backport {
	__u64 tx_broadcast;
	__u64 tx_multicast;
};

int mlx5_eswitch_get_vport_stats_backport(struct mlx5_eswitch *esw,
					  int vport,
					  struct ifla_vf_stats_backport *vf_stats_backport);
#endif
void mlx5_eswitch_del_send_to_vport_rule(struct mlx5_flow_handle *rule);
int mlx5_eswitch_query_vport_drop_stats(struct mlx5_core_dev *dev,
					struct mlx5_vport *vport,
					struct mlx5_vport_drop_stats *stats);
int mlx5_eswitch_add_vport_trunk_range(struct mlx5_eswitch *esw,
				       int vport, u16 start_vlan, u16 end_vlan);
int mlx5_eswitch_del_vport_trunk_range(struct mlx5_eswitch *esw,
				       int vport, u16 start_vlan, u16 end_vlan);
int mlx5_eswitch_modify_esw_vport_context(struct mlx5_eswitch *esw, u16 vport,
					  bool other_vport,
					  void *in, int inlen);
int mlx5_eswitch_query_esw_vport_context(struct mlx5_eswitch *esw, u16 vport,
					 bool other_vport,
					 void *out, int outlen);
int mlx5_esw_set_uplink_rep_mode(struct mlx5_core_dev *mdev, int mode);

struct mlx5_flow_spec;
struct mlx5_esw_flow_attr;

struct mlx5_flow_handle *
mlx5_eswitch_add_offloaded_rule(struct mlx5_eswitch *esw,
				struct mlx5_flow_spec *spec,
				struct mlx5_esw_flow_attr *attr);
struct mlx5_flow_handle *
mlx5_eswitch_add_fwd_rule(struct mlx5_eswitch *esw,
			  struct mlx5_flow_spec *spec,
			  struct mlx5_esw_flow_attr *attr);
void
mlx5_eswitch_del_offloaded_rule(struct mlx5_eswitch *esw,
				struct mlx5_flow_handle *rule,
				struct mlx5_esw_flow_attr *attr);
void
mlx5_eswitch_del_fwd_rule(struct mlx5_eswitch *esw,
			  struct mlx5_flow_handle *rule,
			  struct mlx5_esw_flow_attr *attr);

bool
mlx5_eswitch_prios_supported(struct mlx5_eswitch *esw);

u16
mlx5_eswitch_get_prio_range(struct mlx5_eswitch *esw);

u32
mlx5_eswitch_get_chain_range(struct mlx5_eswitch *esw);

struct mlx5_flow_handle *
mlx5_eswitch_create_vport_rx_rule(struct mlx5_eswitch *esw, u16 vport,
				  struct mlx5_flow_destination *dest);

enum {
	SET_VLAN_STRIP	= BIT(0),
	SET_VLAN_INSERT	= BIT(1)
};

enum mlx5_flow_match_level {
	MLX5_MATCH_NONE	= MLX5_INLINE_MODE_NONE,
	MLX5_MATCH_L2	= MLX5_INLINE_MODE_L2,
	MLX5_MATCH_L3	= MLX5_INLINE_MODE_IP,
	MLX5_MATCH_L4	= MLX5_INLINE_MODE_TCP_UDP,
};

/* current maximum for flow based vport multicasting */
#define MLX5_MAX_FLOW_FWD_VPORTS 2

enum {
	MLX5_ESW_DEST_ENCAP         = BIT(0),
	MLX5_ESW_DEST_ENCAP_VALID   = BIT(1),
};

struct mlx5_esw_flow_attr {
	struct mlx5_eswitch_rep *in_rep;
	struct mlx5_core_dev	*in_mdev;
	struct mlx5_core_dev    *counter_dev;

	int split_count;
	int out_count;

	int	action;
	__be16	vlan_proto[MLX5_FS_VLAN_DEPTH];
	u16	vlan_vid[MLX5_FS_VLAN_DEPTH];
	u8	vlan_prio[MLX5_FS_VLAN_DEPTH];
	u8	total_vlan;
	bool	vlan_handled;
	struct {
		u32 flags;
		struct mlx5_eswitch_rep *rep;
		struct mlx5_pkt_reformat *pkt_reformat;
		struct mlx5_core_dev *mdev;
	} dests[MLX5_MAX_FLOW_FWD_VPORTS];
	struct  mlx5_modify_hdr *modify_hdr;
	u8	inner_match_level;
	u8	outer_match_level;
	bool	is_tunnel_flow;
	struct mlx5_fc *counter;
	u32	chain;
	u16	prio;
	u32	dest_chain;
	struct mlx5e_tc_flow_parse_attr *parse_attr;
};

int mlx5_devlink_eswitch_mode_set(struct devlink *devlink, u16 mode,
				  struct netlink_ext_ack *extack);
int mlx5_devlink_eswitch_mode_get(struct devlink *devlink, u16 *mode);
int mlx5_devlink_eswitch_inline_mode_set(struct devlink *devlink, u8 mode,
					 struct netlink_ext_ack *extack);
int mlx5_devlink_eswitch_inline_mode_get(struct devlink *devlink, u8 *mode);
int mlx5_eswitch_inline_mode_get(struct mlx5_eswitch *esw, u8 *mode);
int mlx5_eswitch_vport_modify_other_hca_cap_roce(struct mlx5_eswitch *esw,
						 struct mlx5_vport *vport, bool value);
int mlx5_eswitch_vport_get_other_hca_cap_roce(struct mlx5_eswitch *esw,
					      struct mlx5_vport *vport, bool *value);

int mlx5_devlink_eswitch_encap_mode_set(struct devlink *devlink, u8 encap,
					struct netlink_ext_ack *extack);
int mlx5_devlink_eswitch_encap_mode_get(struct devlink *devlink, u8 *encap);
void *mlx5_eswitch_get_uplink_priv(struct mlx5_eswitch *esw, u8 rep_type);

int mlx5_eswitch_add_vlan_action(struct mlx5_eswitch *esw,
				 struct mlx5_esw_flow_attr *attr);
int mlx5_eswitch_del_vlan_action(struct mlx5_eswitch *esw,
				 struct mlx5_esw_flow_attr *attr);
int __mlx5_eswitch_set_vport_vlan(struct mlx5_eswitch *esw, int vport,
				  u16 vlan, u8 qos, __be16 proto, u8 set_flags);

static inline bool mlx5_eswitch_vlan_actions_supported(struct mlx5_core_dev *dev,
						       u8 vlan_depth)
{
	bool ret = MLX5_CAP_ESW_FLOWTABLE_FDB(dev, pop_vlan) &&
		   MLX5_CAP_ESW_FLOWTABLE_FDB(dev, push_vlan);

	if (vlan_depth == 1)
		return ret;

	return  ret && MLX5_CAP_ESW_FLOWTABLE_FDB(dev, pop_vlan_2) &&
		MLX5_CAP_ESW_FLOWTABLE_FDB(dev, push_vlan_2);
}

bool mlx5_esw_lag_prereq(struct mlx5_core_dev *dev0,
			 struct mlx5_core_dev *dev1);
bool mlx5_esw_multipath_prereq(struct mlx5_core_dev *dev0,
			       struct mlx5_core_dev *dev1);

const u32 *mlx5_esw_query_functions(struct mlx5_core_dev *dev);

#define MLX5_DEBUG_ESWITCH_MASK BIT(3)

#define esw_info(__dev, format, ...)			\
	dev_info((__dev)->device, "E-Switch: " format, ##__VA_ARGS__)

#define esw_warn(__dev, format, ...)			\
	dev_warn((__dev)->device, "E-Switch: " format, ##__VA_ARGS__)

#define esw_debug(dev, format, ...)				\
	mlx5_core_dbg_mask(dev, MLX5_DEBUG_ESWITCH_MASK, format, ##__VA_ARGS__)

/* The returned number is valid only when the dev is eswitch manager. */
static inline u16 mlx5_eswitch_manager_vport(struct mlx5_core_dev *dev)
{
	return mlx5_core_is_ecpf_esw_manager(dev) ?
		MLX5_VPORT_ECPF : MLX5_VPORT_PF;
}

static inline u16 mlx5_eswitch_first_host_vport_num(struct mlx5_core_dev *dev)
{
	return mlx5_core_is_ecpf_esw_manager(dev) ?
		MLX5_VPORT_PF : MLX5_VPORT_FIRST_VF;
}

static inline bool mlx5_eswitch_is_funcs_handler(struct mlx5_core_dev *dev)
{
	/* Ideally device should have the functions changed supported
	 * capability regardless of it being ECPF or PF wherever such
	 * event should be processed such as on eswitch manager device.
	 * However, some ECPF based device might not have this capability
	 * set. Hence OR for ECPF check to cover such device.
	 */
	return MLX5_CAP_ESW(dev, esw_functions_changed) ||
	       mlx5_core_is_ecpf_esw_manager(dev);
}

static inline int mlx5_eswitch_uplink_idx(struct mlx5_eswitch *esw)
{
	/* Uplink always locate at the last element of the array.*/
	return esw->total_vports - 1;
}

static inline int mlx5_eswitch_ecpf_idx(struct mlx5_eswitch *esw)
{
	return esw->total_vports - 2;
}

/* SF vport numbers in device range from the esw_sf_base_id and log_max_esw_sf.
 * Below helpers perform conversion from SF vport index in software array
 * to vport number and vice versa.
 */
static inline u16 mlx5_eswitch_sf_vport_base_id(const struct mlx5_core_dev *dev)
{
	return MLX5_CAP_ESW(dev, esw_sf_base_id);
}

static inline u16 mlx5_eswitch_max_sfs(const struct mlx5_core_dev *dev)
{
	return mlx5_core_is_sf_supported(dev) ?
			1 << MLX5_CAP_ESW(dev, log_max_esw_sf) : 0;
}

static inline int
mlx5_eswitch_sf_index(const struct mlx5_eswitch *esw, u16 vport_num)
{
	return vport_num - mlx5_eswitch_sf_vport_base_id(esw->dev) +
		MLX5_VPORT_PF_PLACEHOLDER + mlx5_core_max_vfs(esw->dev);
}

static inline u16
mlx5_eswitch_sf_vport_num(const struct mlx5_eswitch *esw, int idx)
{
	return mlx5_eswitch_sf_vport_base_id(esw->dev) + idx -
		(MLX5_VPORT_PF_PLACEHOLDER + mlx5_core_max_vfs(esw->dev));
}

static inline bool
mlx5_eswitch_is_sf_vport(const struct mlx5_eswitch *esw, u16 vport_num)
{
	return mlx5_core_is_sf_supported(esw->dev) &&
		vport_num >= mlx5_eswitch_sf_vport_base_id(esw->dev) &&
		vport_num < (mlx5_eswitch_sf_vport_base_id(esw->dev) +
			     mlx5_eswitch_max_sfs(esw->dev));
}

static inline int mlx5_eswitch_vport_num_to_index(struct mlx5_eswitch *esw,
						  u16 vport_num)
{
	if (vport_num == MLX5_VPORT_ECPF) {
		if (!mlx5_ecpf_vport_exists(esw->dev) &&
		    !mlx5_core_is_ecpf_esw_manager(esw->dev))
			esw_warn(esw->dev, "ECPF vport doesn't exist!\n");
		return mlx5_eswitch_ecpf_idx(esw);
	}

	if (vport_num == MLX5_VPORT_UPLINK)
		return mlx5_eswitch_uplink_idx(esw);

	if (mlx5_eswitch_is_sf_vport(esw, vport_num))
		return mlx5_eswitch_sf_index(esw, vport_num);

	/* PF and VF vports start from 0 to max_vfs */
	return vport_num;
}

static inline u16 mlx5_eswitch_index_to_vport_num(struct mlx5_eswitch *esw,
						  int index)
{
	if (index == mlx5_eswitch_ecpf_idx(esw) &&
	    (mlx5_ecpf_vport_exists(esw->dev) ||
	     mlx5_core_is_ecpf_esw_manager(esw->dev)))
		return MLX5_VPORT_ECPF;

	if (index == mlx5_eswitch_uplink_idx(esw))
		return MLX5_VPORT_UPLINK;

	/* SF vports indices are after VFs and before ECPF */
	if (mlx5_core_is_sf_supported(esw->dev) &&
	    index > mlx5_core_max_vfs(esw->dev))
		return mlx5_eswitch_sf_vport_num(esw, index);

	/* PF and VF vports start from 0 to max_vfs */
	return index;
}

/* TODO: This mlx5e_tc function shouldn't be called by eswitch */
void mlx5e_tc_clean_fdb_peer_flows(struct mlx5_eswitch *esw);

/* The vport getter/iterator are only valid after esw->total_vports
 * and vport->vport are initialized in mlx5_eswitch_init.
 */
#define mlx5_esw_for_all_vports(esw, i, vport)		\
	for ((i) = MLX5_VPORT_PF;			\
	     (vport) = &(esw)->vports[i],		\
	     (i) < (esw)->total_vports; (i)++)

#define mlx5_esw_for_all_vports_reverse(esw, i, vport)	\
	for ((i) = (esw)->total_vports - 1;		\
	     (vport) = &(esw)->vports[i],		\
	     (i) >= MLX5_VPORT_PF; (i)--)

#define mlx5_esw_for_each_vf_vport(esw, i, vport, nvfs)	\
	for ((i) = MLX5_VPORT_FIRST_VF;			\
	     (vport) = &(esw)->vports[(i)],		\
	     (i) <= (nvfs); (i)++)

#define mlx5_esw_for_each_vf_vport_reverse(esw, i, vport, nvfs)	\
	for ((i) = (nvfs);					\
	     (vport) = &(esw)->vports[(i)],			\
	     (i) >= MLX5_VPORT_FIRST_VF; (i)--)

/* The rep getter/iterator are only valid after esw->total_vports
 * and vport->vport are initialized in mlx5_eswitch_init.
 */
#define mlx5_esw_for_all_reps(esw, i, rep)			\
	for ((i) = MLX5_VPORT_PF;				\
	     (rep) = &(esw)->offloads.vport_reps[i],		\
	     (i) < (esw)->total_vports; (i)++)

#define mlx5_esw_for_all_reps_reverse(esw, i, rep)              \
  for (--(i);							\
       (rep) = &(esw)->offloads.vport_reps[i], (i) >= 0;	\
       (i)--)

#define mlx5_esw_for_each_vf_rep(esw, i, rep, nvfs)		\
	for ((i) = MLX5_VPORT_FIRST_VF;				\
	     (rep) = &(esw)->offloads.vport_reps[i],		\
	     (i) <= (nvfs); (i)++)

static inline int mlx5_eswitch_sf_start_idx(const struct mlx5_eswitch *esw)
{
	return MLX5_VPORT_PF_PLACEHOLDER + mlx5_core_max_vfs(esw->dev);
}

static inline int mlx5_eswitch_sf_end(const struct mlx5_eswitch *esw)
{
	return mlx5_eswitch_sf_start_idx(esw) + mlx5_eswitch_max_sfs(esw->dev);
}

#define mlx5_esw_for_each_sf_rep(esw, i, rep)			\
	for ((i) = mlx5_eswitch_sf_start_idx(esw);		\
	     (rep) = &(esw)->offloads.vport_reps[(i)],		\
	     (i) < mlx5_eswitch_sf_end(esw); (i++))		\

#define mlx5_esw_for_each_vf_rep_reverse(esw, i, rep, nvfs)	\
	for ((i) = (nvfs);					\
	     (rep) = &(esw)->offloads.vport_reps[i],		\
	     (i) >= MLX5_VPORT_FIRST_VF; (i)--)

#define mlx5_esw_for_each_vf_vport_num(esw, vport, nvfs)	\
	for ((vport) = MLX5_VPORT_FIRST_VF; (vport) <= (nvfs); (vport)++)

#define mlx5_esw_for_each_vf_vport_num_reverse(esw, vport, nvfs)	\
	for ((vport) = (nvfs); (vport) >= MLX5_VPORT_FIRST_VF; (vport)--)

/* Includes host PF (vport 0) if it's not esw manager. */
#define mlx5_esw_for_each_host_func_rep(esw, i, rep, nvfs)	\
	for ((i) = (esw)->first_host_vport;			\
	     (rep) = &(esw)->offloads.vport_reps[i],		\
	     (i) <= (nvfs); (i)++)

#define mlx5_esw_for_each_host_func_rep_reverse(esw, i, rep, nvfs)	\
	for ((i) = (nvfs);						\
	     (rep) = &(esw)->offloads.vport_reps[i],			\
	     (i) >= (esw)->first_host_vport; (i)--)

#define mlx5_esw_for_each_host_func_vport(esw, vport, nvfs)	\
	for ((vport) = (esw)->first_host_vport;			\
	     (vport) <= (nvfs); (vport)++)

#define mlx5_esw_for_each_host_func_vport_reverse(esw, vport, nvfs)	\
	for ((vport) = (nvfs);						\
	     (vport) >= (esw)->first_host_vport; (vport)--)

struct mlx5_vport *__must_check
mlx5_eswitch_get_vport(struct mlx5_eswitch *esw, u16 vport_num);

bool mlx5_eswitch_is_vf_vport(const struct mlx5_eswitch *esw, u16 vport_num);

int mlx5_esw_funcs_changed_handler(struct notifier_block *nb, unsigned long type, void *data);

static inline bool
mlx5_esw_is_manager_vport(const struct mlx5_core_dev *dev, u16 vport_num)
{
	return MLX5_VPORT_MANAGER(dev) &&
	       dev->priv.eswitch->manager_vport == vport_num;
}

void
mlx5_eswitch_enable_pf_vf_vports(struct mlx5_eswitch *esw,
				 enum mlx5_eswitch_vport_event enabled_events);
void mlx5_eswitch_disable_pf_vf_vports(struct mlx5_eswitch *esw);
int mlx5_eswitch_setup_sf_vport(struct mlx5_eswitch *esw, u16 vport_num);
void mlx5_eswitch_cleanup_sf_vport(struct mlx5_eswitch *esw, u16 vport_num);
int mlx5_eswitch_set_vport_mac(struct mlx5_eswitch *esw,
			       u16 vport, u8 mac[ETH_ALEN]);
int mlx5_eswitch_get_vport_mac(struct mlx5_eswitch *esw,
			       u16 vport, u8 *mac);

#else  /* CONFIG_MLX5_ESWITCH */
/* eswitch API stubs */
static inline int mlx5_eswitch_set_vport_mac(struct mlx5_eswitch *esw,
			       u16 vport, u8 mac[ETH_ALEN]){ return 0; }
static inline int mlx5_eswitch_get_vport_mac(struct mlx5_eswitch *esw,
			       u16 vport, u8 *mac){ return 0; }
static inline int mlx5_eswitch_setup_sf_vport(struct mlx5_eswitch *esw, u16 vport_num){ return 0; }
static inline void mlx5_eswitch_cleanup_sf_vport(struct mlx5_eswitch *esw, u16 vport_num){ return; }
static inline int  mlx5_eswitch_init(struct mlx5_core_dev *dev) { return 0; }
static inline void mlx5_eswitch_cleanup(struct mlx5_eswitch *esw) {}
static inline int mlx5_eswitch_enable_locked(struct mlx5_eswitch *esw, int mode, int num_vfs) {	return 0; }
static inline int mlx5_eswitch_enable(struct mlx5_eswitch *esw, int mode, int num_vfs) { return 0; }
static inline void mlx5_eswitch_disable_locked(struct mlx5_eswitch *esw, bool clear_vf) { }
static inline void mlx5_eswitch_disable(struct mlx5_eswitch *esw,  bool clear_vf) {}
static inline bool mlx5_esw_lag_prereq(struct mlx5_core_dev *dev0, struct mlx5_core_dev *dev1) { return true; }
static inline bool mlx5_eswitch_is_funcs_handler(struct mlx5_core_dev *dev) { return false; }
static inline const u32 *mlx5_esw_query_functions(struct mlx5_core_dev *dev)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static inline u16 mlx5_eswitch_max_sfs(const struct mlx5_core_dev *dev)
{
	return 0;
}

#define FDB_MAX_CHAIN 1
#define FDB_SLOW_PATH_CHAIN (FDB_MAX_CHAIN + 1)
#define FDB_MAX_PRIO 1

static inline bool
mlx5_esw_is_manager_vport(const struct mlx5_core_dev *dev, u16 vport_num)
{
	return vport_num ? true : false;
}

#endif /* CONFIG_MLX5_ESWITCH */

int mlx5_eswitch_compat_sysfs_init(struct net_device *netdev);
void mlx5_eswitch_compat_sysfs_cleanup(struct net_device *netdev);

#endif /* __MLX5_ESWITCH_H__ */
