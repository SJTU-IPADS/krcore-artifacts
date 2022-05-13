#ifndef LINUX_3_10_COMPAT_H
#define LINUX_3_10_COMPAT_H

#include <linux/version.h>

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0))
#include <linux/random.h>
#include <linux/netdevice.h>

#ifndef NETIF_F_HW_VLAN_CTAG_RX
#define NETIF_F_HW_VLAN_CTAG_RX NETIF_F_HW_VLAN_RX
#endif

#ifndef NETIF_F_HW_VLAN_CTAG_TX
#define NETIF_F_HW_VLAN_CTAG_TX NETIF_F_HW_VLAN_TX
#endif

#ifndef NETIF_F_HW_VLAN_CTAG_FILTER
#define NETIF_F_HW_VLAN_CTAG_FILTER NETIF_F_HW_VLAN_FILTER
#endif

#ifndef HAVE_PRANDOM_U32
#define prandom_u32() random32()
#endif

#ifndef NETIF_F_GSO_UDP_TUNNEL
#define NETIF_F_GSO_UDP_TUNNEL (1 << 25)
#endif

#else /* (LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0)) */

#include <linux/random.h>
#include <linux/netdev_features.h>

#ifndef NETIF_F_HW_VLAN_RX
#define NETIF_F_HW_VLAN_RX NETIF_F_HW_VLAN_CTAG_RX
#endif

#ifndef NETIF_F_HW_VLAN_TX
#define NETIF_F_HW_VLAN_TX NETIF_F_HW_VLAN_CTAG_TX
#endif

#ifndef NETIF_F_HW_VLAN_FILTER
#define NETIF_F_HW_VLAN_FILTER NETIF_F_HW_VLAN_CTAG_FILTER
#endif

#ifndef NETIF_F_ALL_CSUM
#define NETIF_F_ALL_CSUM NETIF_F_CSUM_MASK
#endif

#define random32() prandom_u32()

#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0)) */

#endif /* LINUX_3_10_COMPAT_H */
