#ifndef COMPAT_LINUX_SWITCHDEV_H
#define COMPAT_LINUX_SWITCHDEV_H

#include "../../compat/config.h"

#ifdef HAVE_SWITCHDEV_H
#include_next <net/switchdev.h>
#else /* HAVE_SWITCHDEV_H */

#ifdef CONFIG_MLX5_ESWITCH
#define HAVE_SWITCHDEV_H_COMPAT

/*
 * include/net/switchdev.h - Switch device API
 * Copyright (c) 2014-2015 Jiri Pirko <jiri@resnulli.us>
 * Copyright (c) 2014-2015 Scott Feldman <sfeldma@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#define SWITCHDEV_F_NO_RECURSE		BIT(0)
#define SWITCHDEV_F_SKIP_EOPNOTSUPP	BIT(1)
#define SWITCHDEV_F_DEFER		BIT(2)

enum switchdev_attr_id {
	SWITCHDEV_ATTR_ID_UNDEFINED,
	SWITCHDEV_ATTR_ID_PORT_PARENT_ID,
	SWITCHDEV_ATTR_ID_PORT_STP_STATE,
	SWITCHDEV_ATTR_ID_PORT_BRIDGE_FLAGS,
	SWITCHDEV_ATTR_ID_PORT_BRIDGE_FLAGS_SUPPORT,
	SWITCHDEV_ATTR_ID_PORT_MROUTER,
	SWITCHDEV_ATTR_ID_BRIDGE_AGEING_TIME,
	SWITCHDEV_ATTR_ID_BRIDGE_VLAN_FILTERING,
	SWITCHDEV_ATTR_ID_BRIDGE_MC_DISABLED,
};
#ifndef SWITCHDEV_ATTR_ID_PORT_PARENT_ID
#define SWITCHDEV_ATTR_PORT_PARENT_ID SWITCHDEV_ATTR_ID_PORT_PARENT_ID
#endif

struct switchdev_attr {
	struct net_device *orig_dev;
	enum switchdev_attr_id id;
	u32 flags;
	union {
		struct netdev_phys_item_id ppid;	/* PORT_PARENT_ID */
	} u;
};

#endif /* CONFIG_MLX5_ESWITCH */
#endif /* HAVE_SWITCHDEV_H */
#endif /* COMPAT_LINUX_SWITCHDEV_H */
