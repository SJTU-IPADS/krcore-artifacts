#ifndef __COMPAT_NET_IP_TUNNELS_H
#define __COMPAT_NET_IP_TUNNELS_H 1

#ifndef CONFIG_COMPAT_IP_TUNNELS
#include_next <net/ip_tunnels.h>
#else

#include <linux/if_tunnel.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/socket.h>
#include <linux/types.h>
#include <linux/u64_stats_sync.h>
#include <linux/bitops.h>

#include <net/dsfield.h>
#include <net/gro_cells.h>
#include <net/inet_ecn.h>
#include <net/netns/generic.h>
#include <net/rtnetlink.h>

#if IS_ENABLED(CONFIG_IPV6)
#include <net/ipv6.h>
#include <net/ip6_fib.h>
#include <net/ip6_route.h>
#endif

#define TUNNEL_CSUM	__cpu_to_be16(0x01)
#define TUNNEL_ROUTING	__cpu_to_be16(0x02)
#define TUNNEL_KEY	__cpu_to_be16(0x04)
#define TUNNEL_SEQ	__cpu_to_be16(0x08)
#define TUNNEL_STRICT	__cpu_to_be16(0x10)
#define TUNNEL_REC	__cpu_to_be16(0x20)
#define TUNNEL_VERSION	__cpu_to_be16(0x40)
#define TUNNEL_NO_KEY	__cpu_to_be16(0x80)

struct sk_buff *iptunnel_handle_offloads(struct sk_buff *skb, bool gre_csum,
					 int gso_type_mask);

static inline __be16 gre_tnl_flags_to_gre_flags(__be16 tflags)
{
	__be16 flags = 0;

	if (tflags & TUNNEL_CSUM)
		flags |= GRE_CSUM;
	if (tflags & TUNNEL_ROUTING)
		flags |= GRE_ROUTING;
	if (tflags & TUNNEL_KEY)
		flags |= GRE_KEY;
	if (tflags & TUNNEL_SEQ)
		flags |= GRE_SEQ;
	if (tflags & TUNNEL_STRICT)
		flags |= GRE_STRICT;
	if (tflags & TUNNEL_REC)
		flags |= GRE_REC;
	if (tflags & TUNNEL_VERSION)
		flags |= GRE_VERSION;

	return flags;
}

static inline int gre_calc_hlen(__be16 o_flags)
{
	int addend = 4;

	if (o_flags & TUNNEL_CSUM)
		addend += 4;
	if (o_flags & TUNNEL_KEY)
		addend += 4;
	if (o_flags & TUNNEL_SEQ)
		addend += 4;
	return addend;
}

/* Keep error state on tunnel for 30 sec */
#define IPTUNNEL_ERR_TIMEO	(30*HZ)

/* Used to memset ip_tunnel padding. */
#define IP_TUNNEL_KEY_SIZE	offsetofend(struct ip_tunnel_key, tp_dst)

/* Used to memset ipv4 address padding. */
#define IP_TUNNEL_KEY_IPV4_PAD	offsetofend(struct ip_tunnel_key, u.ipv4.dst)
#define IP_TUNNEL_KEY_IPV4_PAD_LEN				\
	(FIELD_SIZEOF(struct ip_tunnel_key, u) -		\
	 FIELD_SIZEOF(struct ip_tunnel_key, u.ipv4))

struct ip_tunnel_key {
	__be64			tun_id;
	union {
		struct {
			__be32	src;
			__be32	dst;
		} ipv4;
		struct {
			struct in6_addr src;
			struct in6_addr dst;
		} ipv6;
	} u;
	__be16			tun_flags;
	u8			tos;		/* TOS for IPv4, TC for IPv6 */
	u8			ttl;		/* TTL for IPv4, HL for IPv6 */
	__be32			label;		/* Flow Label for IPv6 */
	__be16			tp_src;
	__be16			tp_dst;
};

/* Flags for ip_tunnel_info mode. */
#define IP_TUNNEL_INFO_TX	0x01	/* represents tx tunnel parameters */
#define IP_TUNNEL_INFO_IPV6	0x02	/* key contains IPv6 addresses */
#define IP_TUNNEL_INFO_BRIDGE	0x04	/* represents a bridged tunnel id */

/* Maximum tunnel options length. */
#define IP_TUNNEL_OPTS_MAX					\
	GENMASK((FIELD_SIZEOF(struct ip_tunnel_info,		\
			      options_len) * BITS_PER_BYTE) - 1, 0)

struct ip_tunnel_info {
	struct ip_tunnel_key	key;
	u8			options_len;
	u8			mode;
};

/* Definition to suppress compiler warnings */
struct tnl_ptk_info {
	__be16 flags, prtoto;
	__be32 key, seq;
};

static inline unsigned short ip_tunnel_info_af(const struct ip_tunnel_info
					       *tun_info)
{
	return tun_info->mode & IP_TUNNEL_INFO_IPV6 ? AF_INET6 : AF_INET;
}

static inline __be64 key32_to_tunnel_id(__be32 key)
{
#ifdef __BIG_ENDIAN
	return (__force __be64)key;
#else
	return (__force __be64)((__force u64)key << 32);
#endif
}

/* Returns the least-significant 32 bits of a __be64. */
static inline __be32 tunnel_id_to_key32(__be64 tun_id)
{
#ifdef __BIG_ENDIAN
	return (__force __be32)tun_id;
#else
	return (__force __be32)((__force u64)tun_id >> 32);
#endif
}

#endif

#endif /* __COMPAT_NET_IP_TUNNELS_H */
