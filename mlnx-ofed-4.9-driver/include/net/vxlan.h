#ifndef COMPAT_NET_VXLAN_H
#define COMPAT_NET_VXLAN_H

#include "../../compat/config.h"
#ifdef HAVE_NET_VXLAN_H
#include_next <net/vxlan.h>
#endif

#ifndef IANA_VXLAN_UDP_PORT 
#define IANA_VXLAN_UDP_PORT     4789
#endif

#ifndef HAVE_VXLAN_VNI_FIELD
static inline __be32 vxlan_vni_field(__be32 vni)
{
#if defined(__BIG_ENDIAN)
	return (__force __be32)((__force u32)vni << 8);
#else
	return (__force __be32)((__force u32)vni >> 8);
#endif
}
#endif

#undef VXLAN_HF_VNI
#define VXLAN_HF_VNI	cpu_to_be32(BIT(27))

#ifndef HAVE_NETIF_IS_VXLAN
static inline bool netif_is_vxlan(const struct net_device *dev)
{
	return dev->rtnl_link_ops &&
		!strcmp(dev->rtnl_link_ops->kind, "vxlan");
}
#endif

#endif /* COMPAT_NET_VXLAN_H */

