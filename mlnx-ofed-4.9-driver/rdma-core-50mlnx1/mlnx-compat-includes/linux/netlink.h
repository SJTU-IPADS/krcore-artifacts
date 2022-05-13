#ifndef _MLNX_COMPAT_LINUX_NETLINK_H
#define _MLNX_COMPAT_LINUX_NETLINK_H 1

#if !defined _LINUX_SOCKET_H && !defined _BITS_SOCKADDR_H && !defined sa_family_t
typedef unsigned short sa_family_t;
#endif

#include_next <linux/netlink.h>

#ifndef NETLINK_RDMA
#define NETLINK_RDMA           20
#endif

#endif /* _MLNX_COMPAT_LINUX_NETLINK_H */
