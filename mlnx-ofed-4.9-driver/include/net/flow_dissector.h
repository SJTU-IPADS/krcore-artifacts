#ifndef _COMPAT_NET_FLOW_DISSECTOR_H
#define _COMPAT_NET_FLOW_DISSECTOR_H

#include "../../compat/config.h"

#ifndef CONFIG_COMPAT_FLOW_DISSECTOR
#include_next <net/flow_dissector.h>
#else/*CONFIG_COMPAT_FLOW_DISSECTOR*/
#define HAVE_SKB_FLOW_DISSECT_FLOW_KEYS_HAS_3_PARAMS 1
#define HAVE_FLOW_DISSECTOR_KEY_VLAN 1
#define HAVE_FLOW_DISSECTOR_KEY_IP 1
#define HAVE_FLOW_DISSECTOR_KEY_TCP 1
#define HAVE_FLOW_DISSECTOR_KEY_ENC_IP 1
#define HAVE_FLOW_DISSECTOR_KEY_ENC_KEYID 1

#include <linux/types.h>
#include <linux/in6.h>
#include <uapi/linux/if_ether.h>

#undef FLOW_DIS_IS_FRAGMENT
#undef FLOW_DIS_FIRST_FRAG
#undef FLOW_DIS_ENCAPSULATION
#undef FLOW_DISSECTOR_F_PARSE_1ST_FRAG
#undef FLOW_DISSECTOR_F_STOP_AT_L3
#undef FLOW_DISSECTOR_F_STOP_AT_FLOW_LABEL
#undef FLOW_DISSECTOR_F_STOP_AT_ENCAP
#undef FLOW_KEYS_HASH_START_FIELD
#undef FLOW_KEYS_HASH_OFFSET
#undef FLOW_KEYS_DIGEST_LEN

#define flow_dissect_ret LINUX_BACKPORT(flow_dissect_ret)
#define flow_dissector_key_id LINUX_BACKPORT(flow_dissector_key_id)
#define FLOW_DISSECT_RET_OUT_GOOD LINUX_BACKPORT(FLOW_DISSECT_RET_OUT_GOOD)
#define FLOW_DISSECT_RET_OUT_BAD LINUX_BACKPORT(FLOW_DISSECT_RET_OUT_BAD)
#define FLOW_DISSECT_RET_PROTO_AGAIN LINUX_BACKPORT(FLOW_DISSECT_RET_PROTO_AGAIN)
#define FLOW_DISSECT_RET_IPPROTO_AGAIN LINUX_BACKPORT(FLOW_DISSECT_RET_IPPROTO_AGAIN)
#define FLOW_DISSECT_RET_CONTINUE LINUX_BACKPORT(FLOW_DISSECT_RET_CONTINUE)
#define FLOW_DISSECTOR_KEY_CONTROL LINUX_BACKPORT(FLOW_DISSECTOR_KEY_CONTROL)
#define FLOW_DISSECTOR_KEY_BASIC LINUX_BACKPORT(FLOW_DISSECTOR_KEY_BASIC)
#define FLOW_DISSECTOR_KEY_IPV4_ADDRS LINUX_BACKPORT(FLOW_DISSECTOR_KEY_IPV4_ADDRS)
#define FLOW_DISSECTOR_KEY_IPV6_ADDRS LINUX_BACKPORT(FLOW_DISSECTOR_KEY_IPV6_ADDRS)
#define FLOW_DISSECTOR_KEY_PORTS LINUX_BACKPORT(FLOW_DISSECTOR_KEY_PORTS)
#define FLOW_DISSECTOR_KEY_ICMP LINUX_BACKPORT(FLOW_DISSECTOR_KEY_ICMP)
#define FLOW_DISSECTOR_KEY_ETH_ADDRS LINUX_BACKPORT(FLOW_DISSECTOR_KEY_ETH_ADDRS)
#define FLOW_DISSECTOR_KEY_TIPC LINUX_BACKPORT(FLOW_DISSECTOR_KEY_TIPC)
#define FLOW_DISSECTOR_KEY_ARP LINUX_BACKPORT(FLOW_DISSECTOR_KEY_ARP)
#define FLOW_DISSECTOR_KEY_VLAN LINUX_BACKPORT(FLOW_DISSECTOR_KEY_VLAN)
#define FLOW_DISSECTOR_KEY_FLOW_LABEL LINUX_BACKPORT(FLOW_DISSECTOR_KEY_FLOW_LABEL)
#define FLOW_DISSECTOR_KEY_GRE_KEYID LINUX_BACKPORT(FLOW_DISSECTOR_KEY_GRE_KEYID)
#define FLOW_DISSECTOR_KEY_MPLS_ENTROPY LINUX_BACKPORT(FLOW_DISSECTOR_KEY_MPLS_ENTROPY)
#define FLOW_DISSECTOR_KEY_ENC_KEYID LINUX_BACKPORT(FLOW_DISSECTOR_KEY_ENC_KEYID)
#define FLOW_DISSECTOR_KEY_ENC_IPV4_ADDRS LINUX_BACKPORT(FLOW_DISSECTOR_KEY_ENC_IPV4_ADDRS)
#define FLOW_DISSECTOR_KEY_ENC_IPV6_ADDRS LINUX_BACKPORT(FLOW_DISSECTOR_KEY_ENC_IPV6_ADDRS)
#define FLOW_DISSECTOR_KEY_ENC_CONTROL LINUX_BACKPORT(FLOW_DISSECTOR_KEY_ENC_CONTROL)
#define FLOW_DISSECTOR_KEY_ENC_PORTS LINUX_BACKPORT(FLOW_DISSECTOR_KEY_ENC_PORTS)
#define FLOW_DISSECTOR_KEY_MPLS LINUX_BACKPORT(FLOW_DISSECTOR_KEY_MPLS)
#define FLOW_DISSECTOR_KEY_TCP LINUX_BACKPORT(FLOW_DISSECTOR_KEY_TCP)
#define FLOW_DISSECTOR_KEY_IP LINUX_BACKPORT(FLOW_DISSECTOR_KEY_IP)
#define FLOW_DISSECTOR_KEY_ENC_IP LINUX_BACKPORT(FLOW_DISSECTOR_KEY_ENC_IP)
#define FLOW_DISSECTOR_KEY_MAX LINUX_BACKPORT(FLOW_DISSECTOR_KEY_MAX)

#define flow_dissector_key_control LINUX_BACKPORT(flow_dissector_key_control)
#define flow_dissector_key_basic LINUX_BACKPORT(flow_dissector_key_basic)
#define flow_dissector_key_tags LINUX_BACKPORT(flow_dissector_key_tags)
#define flow_dissector_key_vlan LINUX_BACKPORT(flow_dissector_key_vlan)
#define flow_dissector_key_mpls LINUX_BACKPORT(flow_dissector_key_mpls)
#define flow_dissector_key_keyid LINUX_BACKPORT(flow_dissector_key_keyid)
#define flow_dissector_key_ipv4_addrs LINUX_BACKPORT(flow_dissector_key_ipv4_addrs)
#define flow_dissector_key_ipv6_addrs LINUX_BACKPORT(flow_dissector_key_ipv6_addrs)
#define flow_dissector_key_tipc LINUX_BACKPORT(flow_dissector_key_tipc)
#define flow_dissector_key_addrs LINUX_BACKPORT(flow_dissector_key_addrs)
#define flow_dissector_key_arp LINUX_BACKPORT(flow_dissector_key_arp)
#define flow_dissector_key_ports LINUX_BACKPORT(flow_dissector_key_ports)
#define flow_dissector_key_icmp LINUX_BACKPORT(flow_dissector_key_icmp)
#define flow_dissector_key_eth_addrs LINUX_BACKPORT(flow_dissector_key_eth_addrs)
#define flow_dissector_key_tcp LINUX_BACKPORT(flow_dissector_key_tcp)
#define flow_dissector_key_ip LINUX_BACKPORT(flow_dissector_key_ip)
#define flow_dissector_key LINUX_BACKPORT(flow_dissector_key)
#define flow_dissector LINUX_BACKPORT(flow_dissector)
#define flow_keys LINUX_BACKPORT(flow_keys)
#define flow_keys_digest LINUX_BACKPORT(flow_keys_digest)

#define flow_keys_dissector LINUX_BACKPORT(flow_keys_dissector)
#define flow_keys_buf_dissector LINUX_BACKPORT(flow_keys_buf_dissector)

#define flow_get_u32_src LINUX_BACKPORT(flow_get_u32_src)
#define flow_get_u32_dst LINUX_BACKPORT(flow_get_u32_dst)
#define make_flow_keys_digest LINUX_BACKPORT(make_flow_keys_digest)
#define flow_keys_have_l4 LINUX_BACKPORT(flow_keys_have_l4)
#define flow_hash_from_keys LINUX_BACKPORT(flow_hash_from_keys)
#define dissector_uses_key LINUX_BACKPORT(dissector_uses_key)
#define init_default_flow_dissectors LINUX_BACKPORT(init_default_flow_dissectors)

#ifndef HAVE_SKB_FLOW_DISSECT
#define skb_flow_dissector_target LINUX_BACKPORT(skb_flow_dissector_target)
#define skb_flow_dissect_flow_keys LINUX_BACKPORT(skb_flow_dissect_flow_keys)
#define __skb_flow_dissect LINUX_BACKPORT(__skb_flow_dissect)
#define skb_flow_dissect LINUX_BACKPORT(skb_flow_dissect)
#define skb_flow_dissector_init LINUX_BACKPORT(skb_flow_dissector_init)
#define skb_get_poff LINUX_BACKPORT(skb_get_poff)
#define __skb_get_poff LINUX_BACKPORT(__skb_get_poff)
#define __skb_flow_get_ports LINUX_BACKPORT(__skb_flow_get_ports)

#ifndef CONFIG_NET_SCHED_NEW
#define __skb_get_hash_symmetric LINUX_BACKPORT(__skb_get_hash_symmetric)
#define __skb_get_hash LINUX_BACKPORT(__skb_get_hash)
#define __get_hash_from_flowi6 LINUX_BACKPORT(__get_hash_from_flowi6)
#define __get_hash_from_flowi4 LINUX_BACKPORT(__get_hash_from_flowi4)
#define skb_get_hash_perturb LINUX_BACKPORT(skb_get_hash_perturb)
#endif /* CONFIG_NET_SCHED_NEW */
#endif /* HAVE_SKB_FLOW_DISSECT */
/**
 * struct flow_dissector_key_control:
 * @thoff: Transport header offset
 */
struct flow_dissector_key_control {
	u16	thoff;
	u16	addr_type;
	u32	flags;
};

#define FLOW_DIS_IS_FRAGMENT	BIT(0)
#define FLOW_DIS_FIRST_FRAG	BIT(1)
#define FLOW_DIS_ENCAPSULATION	BIT(2)

enum flow_dissect_ret {
	FLOW_DISSECT_RET_OUT_GOOD,
	FLOW_DISSECT_RET_OUT_BAD,
	FLOW_DISSECT_RET_PROTO_AGAIN,
	FLOW_DISSECT_RET_IPPROTO_AGAIN,
	FLOW_DISSECT_RET_CONTINUE,
};

/**
 * struct flow_dissector_key_basic:
 * @thoff: Transport header offset
 * @n_proto: Network header protocol (eg. IPv4/IPv6)
 * @ip_proto: Transport header protocol (eg. TCP/UDP)
 */
struct flow_dissector_key_basic {
	__be16	n_proto;
	u8	ip_proto;
	u8	padding;
};

struct flow_dissector_key_tags {
	u32	flow_label;
};

struct flow_dissector_key_vlan {
	u16	vlan_id:12,
		vlan_priority:3;
	u16	padding;
};

struct flow_dissector_key_mpls {
	u32	mpls_ttl:8,
		mpls_bos:1,
		mpls_tc:3,
		mpls_label:20;
};

struct flow_dissector_key_keyid {
	__be32	keyid;
};

/**
 * struct flow_dissector_key_ipv4_addrs:
 * @src: source ip address
 * @dst: destination ip address
 */
struct flow_dissector_key_ipv4_addrs {
	/* (src,dst) must be grouped, in the same way than in IP header */
	__be32 src;
	__be32 dst;
};

/**
 * struct flow_dissector_key_ipv6_addrs:
 * @src: source ip address
 * @dst: destination ip address
 */
struct flow_dissector_key_ipv6_addrs {
	/* (src,dst) must be grouped, in the same way than in IP header */
	struct in6_addr src;
	struct in6_addr dst;
};

/**
 * struct flow_dissector_key_tipc:
 * @key: source node address combined with selector
 */
struct flow_dissector_key_tipc {
	__be32 key;
};

/**
 * struct flow_dissector_key_addrs:
 * @v4addrs: IPv4 addresses
 * @v6addrs: IPv6 addresses
 */
struct flow_dissector_key_addrs {
	union {
		struct flow_dissector_key_ipv4_addrs v4addrs;
		struct flow_dissector_key_ipv6_addrs v6addrs;
		struct flow_dissector_key_tipc tipckey;
	};
};

/**
 * flow_dissector_key_arp:
 *	@ports: Operation, source and target addresses for an ARP header
 *              for Ethernet hardware addresses and IPv4 protocol addresses
 *		sip: Sender IP address
 *		tip: Target IP address
 *		op:  Operation
 *		sha: Sender hardware address
 *		tpa: Target hardware address
 */
struct flow_dissector_key_arp {
	__u32 sip;
	__u32 tip;
	__u8 op;
	unsigned char sha[ETH_ALEN];
	unsigned char tha[ETH_ALEN];
};

/**
 * flow_dissector_key_tp_ports:
 *	@ports: port numbers of Transport header
 *		src: source port number
 *		dst: destination port number
 */
struct flow_dissector_key_ports {
	union {
		__be32 ports;
		struct {
			__be16 src;
			__be16 dst;
		};
	};
};

/**
 * flow_dissector_key_icmp:
 *	@ports: type and code of ICMP header
 *		icmp: ICMP type (high) and code (low)
 *		type: ICMP type
 *		code: ICMP code
 */
struct flow_dissector_key_icmp {
	union {
		__be16 icmp;
		struct {
			u8 type;
			u8 code;
		};
	};
};

/**
 * struct flow_dissector_key_eth_addrs:
 * @src: source Ethernet address
 * @dst: destination Ethernet address
 */
struct flow_dissector_key_eth_addrs {
	/* (dst,src) must be grouped, in the same way than in ETH header */
	unsigned char dst[ETH_ALEN];
	unsigned char src[ETH_ALEN];
};

/**
 * struct flow_dissector_key_tcp:
 * @flags: flags
 */
struct flow_dissector_key_tcp {
	__be16 flags;
};

/**
 * struct flow_dissector_key_ip:
 * @tos: tos
 * @ttl: ttl
 */
struct flow_dissector_key_ip {
	__u8	tos;
	__u8	ttl;
};

enum flow_dissector_key_id {
	FLOW_DISSECTOR_KEY_CONTROL, /* struct flow_dissector_key_control */
	FLOW_DISSECTOR_KEY_BASIC, /* struct flow_dissector_key_basic */
	FLOW_DISSECTOR_KEY_IPV4_ADDRS, /* struct flow_dissector_key_ipv4_addrs */
	FLOW_DISSECTOR_KEY_IPV6_ADDRS, /* struct flow_dissector_key_ipv6_addrs */
	FLOW_DISSECTOR_KEY_PORTS, /* struct flow_dissector_key_ports */
	FLOW_DISSECTOR_KEY_ICMP, /* struct flow_dissector_key_icmp */
	FLOW_DISSECTOR_KEY_ETH_ADDRS, /* struct flow_dissector_key_eth_addrs */
#if 0
	FLOW_DISSECTOR_KEY_TIPC, /* struct flow_dissector_key_tipc */
#endif
	FLOW_DISSECTOR_KEY_ARP, /* struct flow_dissector_key_arp */
	FLOW_DISSECTOR_KEY_VLAN, /* struct flow_dissector_key_flow_vlan */
	FLOW_DISSECTOR_KEY_FLOW_LABEL, /* struct flow_dissector_key_flow_tags */
#if 0
	FLOW_DISSECTOR_KEY_GRE_KEYID, /* struct flow_dissector_key_keyid */
#endif
	FLOW_DISSECTOR_KEY_MPLS_ENTROPY, /* struct flow_dissector_key_keyid */
	FLOW_DISSECTOR_KEY_ENC_KEYID, /* struct flow_dissector_key_keyid */
	FLOW_DISSECTOR_KEY_ENC_IPV4_ADDRS, /* struct flow_dissector_key_ipv4_addrs */
	FLOW_DISSECTOR_KEY_ENC_IPV6_ADDRS, /* struct flow_dissector_key_ipv6_addrs */
	FLOW_DISSECTOR_KEY_ENC_CONTROL, /* struct flow_dissector_key_control */
	FLOW_DISSECTOR_KEY_ENC_PORTS, /* struct flow_dissector_key_ports */
	FLOW_DISSECTOR_KEY_MPLS, /* struct flow_dissector_key_mpls */
	FLOW_DISSECTOR_KEY_TCP, /* struct flow_dissector_key_tcp */
	FLOW_DISSECTOR_KEY_CVLAN, /* struct flow_dissector_key_vlan */
	FLOW_DISSECTOR_KEY_IP, /* struct flow_dissector_key_ip */
	FLOW_DISSECTOR_KEY_ENC_IP, /* struct flow_dissector_key_ip */
	FLOW_DISSECTOR_KEY_ENC_OPTS, /* struct flow_dissector_key_enc_opts */

	FLOW_DISSECTOR_KEY_MAX,
};

#define FLOW_DISSECTOR_F_PARSE_1ST_FRAG		BIT(0)
#define FLOW_DISSECTOR_F_STOP_AT_L3		BIT(1)
#define FLOW_DISSECTOR_F_STOP_AT_FLOW_LABEL	BIT(2)
#define FLOW_DISSECTOR_F_STOP_AT_ENCAP		BIT(3)

struct flow_dissector_key {
	enum flow_dissector_key_id key_id;
	size_t offset; /* offset of struct flow_dissector_key_*
			  in target the struct */
};

struct flow_dissector {
	unsigned int used_keys; /* each bit repesents presence of one key id */
	unsigned short int offset[FLOW_DISSECTOR_KEY_MAX];
};

struct flow_keys {
	struct flow_dissector_key_control control;
#define FLOW_KEYS_HASH_START_FIELD basic
	struct flow_dissector_key_basic basic;
	struct flow_dissector_key_tags tags;
	struct flow_dissector_key_vlan vlan;
	struct flow_dissector_key_keyid keyid;
	struct flow_dissector_key_ports ports;
	struct flow_dissector_key_addrs addrs;
};

#define FLOW_KEYS_HASH_OFFSET		\
	offsetof(struct flow_keys, FLOW_KEYS_HASH_START_FIELD)

__be32 flow_get_u32_src(const struct flow_keys *flow);
__be32 flow_get_u32_dst(const struct flow_keys *flow);

extern struct flow_dissector flow_keys_dissector;
extern struct flow_dissector flow_keys_buf_dissector;

/* struct flow_keys_digest:
 *
 * This structure is used to hold a digest of the full flow keys. This is a
 * larger "hash" of a flow to allow definitively matching specific flows where
 * the 32 bit skb->hash is not large enough. The size is limited to 16 bytes so
 * that it can by used in CB of skb (see sch_choke for an example).
 */
#define FLOW_KEYS_DIGEST_LEN	16
struct flow_keys_digest {
	u8	data[FLOW_KEYS_DIGEST_LEN];
};

void make_flow_keys_digest(struct flow_keys_digest *digest,
			   const struct flow_keys *flow);

static inline bool flow_keys_have_l4(const struct flow_keys *keys)
{
	return (keys->ports.ports || keys->tags.flow_label);
}

u32 flow_hash_from_keys(struct flow_keys *keys);

static inline bool dissector_uses_key(const struct flow_dissector *flow_dissector,
				      enum flow_dissector_key_id key_id)
{
	return flow_dissector->used_keys & (1 << key_id);
}
static inline void *skb_flow_dissector_target(struct flow_dissector *flow_dissector,
					      enum flow_dissector_key_id key_id,
					      void *target_container)
{
	return ((char *)target_container) + flow_dissector->offset[key_id];
}

#ifndef HAVE_SKB_FLOW_DISSECT
bool skb_flow_dissect_flow_keys(const struct sk_buff *skb,
				struct flow_keys *flow,
				unsigned int flags);

bool __skb_flow_dissect(const struct sk_buff *skb,
			struct flow_dissector *flow_dissector,
			void *target_container,
			void *data, __be16 proto, int nhoff, int hlen,
			unsigned int flags);

static inline bool skb_flow_dissect(const struct sk_buff *skb,
				    struct flow_dissector *flow_dissector,
				    void *target_container,
				    unsigned int flags)
{
	return __skb_flow_dissect(skb, flow_dissector, target_container,
				  NULL, 0, 0, 0, flags);
}

void skb_flow_dissector_init(struct flow_dissector *flow_dissector,
			     const struct flow_dissector_key *key,
			     unsigned int key_count);

#endif
int init_default_flow_dissectors(void);

#ifdef HAVE_NET_FLOW_KEYS_H
#undef flow_keys
#undef flow_hash_from_keys
#endif

#endif

#endif
