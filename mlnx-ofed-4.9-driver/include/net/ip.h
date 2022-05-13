#ifndef _COMPAT_NET_IP_H
#define _COMPAT_NET_IP_H 1

#include_next <net/ip.h>

#ifndef NET_INC_STATS_USER
#define NET_INC_STATS_USER NET_INC_STATS
#endif

#endif	/* _COMPAT_NET_IP_H */
