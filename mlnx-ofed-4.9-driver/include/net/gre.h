#ifndef _COMPAT_NET_GRE_H
#define _COMPAT_NET_GRE_H 1

#include "../../compat/config.h"

#include_next <net/gre.h>

#ifndef HAVE_NETIF_IS_GRETAP
static inline bool netif_is_gretap(const struct net_device *dev)
{
	return dev->rtnl_link_ops &&
		!strcmp(dev->rtnl_link_ops->kind, "gretap");
}

static inline bool netif_is_ip6gretap(const struct net_device *dev)
{
	return dev->rtnl_link_ops &&
		!strcmp(dev->rtnl_link_ops->kind, "ip6gretap");
}
#endif

#endif /* _COMPAT_NET_GRE_H */
