/* SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause) */
/*
 * Copyright (c) 2018 Mellanox Technologies. All rights reserved.
 */

#ifndef _MLX5_ESWITCH_
#define _MLX5_ESWITCH_

#include <linux/mlx5/driver.h>

#define MLX5_ESWITCH_MANAGER(mdev) MLX5_CAP_GEN(mdev, eswitch_manager)

enum {
	MLX5_ESWITCH_NONE,
	MLX5_ESWITCH_LEGACY,
	MLX5_ESWITCH_OFFLOADS
};

enum {
	REP_ETH,
	REP_IB,
	NUM_REP_TYPES,
};

enum {
	REP_UNREGISTERED,
	REP_REGISTERED,
	REP_LOADED,
};

enum mlx5_switchdev_event {
	MLX5_SWITCHDEV_EVENT_PAIR,
	MLX5_SWITCHDEV_EVENT_UNPAIR,
};

struct mlx5_eswitch_rep;

struct mlx5_eswitch_rep_ops {
	int (*load)(struct mlx5_core_dev *dev, struct mlx5_eswitch_rep *rep);
	void (*unload)(struct mlx5_eswitch_rep *rep);
	void *(*get_proto_dev)(struct mlx5_eswitch_rep *rep);
	int (*event)(struct mlx5_eswitch *esw,
		     struct mlx5_eswitch_rep *rep,
		     enum mlx5_switchdev_event event,
		     void *data);
};

struct mlx5_eswitch_rep_data {
	void *priv;
	atomic_t state;
};

struct mlx5_eswitch_rep {
	struct mlx5_eswitch_rep_data rep_data[NUM_REP_TYPES];
	u16		       vport;
	u8		       hw_id[ETH_ALEN];
	u16		       vlan;
	/* Only IB rep is using vport_index */
	u16		       vport_index;
	u32		       vlan_refcount;
	struct		       mlx5_eswitch *esw;
};

void mlx5_eswitch_register_vport_reps(struct mlx5_eswitch *esw,
				      const struct mlx5_eswitch_rep_ops *ops,
				      u8 rep_type);
void mlx5_eswitch_unregister_vport_reps(struct mlx5_eswitch *esw, u8 rep_type);
void *mlx5_eswitch_get_proto_dev(struct mlx5_eswitch *esw,
				 u16 vport_num,
				 u8 rep_type);
struct mlx5_eswitch_rep *mlx5_eswitch_vport_rep(struct mlx5_eswitch *esw,
						u16 vport_num);
void *mlx5_eswitch_uplink_get_proto_dev(struct mlx5_eswitch *esw, u8 rep_type);
struct mlx5_flow_handle *
mlx5_eswitch_add_send_to_vport_rule(struct mlx5_eswitch *on_esw,
				    struct mlx5_eswitch *from_esw,
				    struct mlx5_eswitch_rep *rep,
				    u32 sqn);
u16 mlx5_eswitch_get_total_vports(const struct mlx5_core_dev *dev);
u32 mlx5_eswitch_get_vport_metadata_for_match(struct mlx5_eswitch *esw, u16 vport);
u32 mlx5_eswitch_get_vport_metadata_mask(void);
int mlx5_eswitch_query_esw_vport_context(struct mlx5_eswitch *esw, u16 vport,
					 bool other_vport,
					 void *out, int outlen);

struct mlx5_core_dev *mlx5_eswitch_get_core_dev(struct mlx5_eswitch *esw);
#ifdef CONFIG_MLX5_ESWITCH
u16 mlx5_eswitch_get_encap_mode(struct mlx5_eswitch *esw);
u32 mlx5_eswitch_vport_match_metadata_enabled(struct mlx5_eswitch *esw);
bool mlx5_eswitch_is_manager_vport(const struct mlx5_eswitch *esw, u16 vport_num);
u8 mlx5_eswitch_mode(struct mlx5_eswitch *esw);
#else  /* CONFIG_MLX5_ESWITCH */
static inline u8 mlx5_eswitch_mode(struct mlx5_eswitch *esw){ return MLX5_ESWITCH_NONE; }
static inline u16 mlx5_eswitch_get_encap_mode(struct mlx5_eswitch *esw) { return 0; }
static inline u32 mlx5_eswitch_vport_match_metadata_enabled(struct mlx5_eswitch *esw) { return 0; }
static bool mlx5_eswitch_is_manager_vport(const struct mlx5_eswitch *esw,
					  u16 vport_num)
{
	return false;
}
#endif /* CONFIG_MLX5_ESWITCH */

#endif
