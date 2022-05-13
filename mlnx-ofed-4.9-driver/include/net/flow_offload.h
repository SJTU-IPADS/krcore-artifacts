#ifndef _COMPAT_NET_FLOW_OFFLOAD_H
#define _COMPAT_NET_FLOW_OFFLOAD_H

#include "../../compat/config.h"

#ifdef HAVE_FLOW_RULE_MATCH_CVLAN
#include_next <net/flow_offload.h>
#else

#include <net/flow_dissector.h>
#include <net/ip_tunnels.h>

struct flow_match {
	struct flow_dissector	*dissector;
	void			*mask;
	void			*key;
};

struct flow_match_basic {
	struct flow_dissector_key_basic *key, *mask;
};

struct flow_match_control {
	struct flow_dissector_key_control *key, *mask;
};

struct flow_match_eth_addrs {
	struct flow_dissector_key_eth_addrs *key, *mask;
};

struct flow_match_vlan {
	struct flow_dissector_key_vlan *key, *mask;
};

struct flow_match_ipv4_addrs {
	struct flow_dissector_key_ipv4_addrs *key, *mask;
};

struct flow_match_ipv6_addrs {
	struct flow_dissector_key_ipv6_addrs *key, *mask;
};

struct flow_match_ip {
	struct flow_dissector_key_ip *key, *mask;
};

struct flow_match_ports {
	struct flow_dissector_key_ports *key, *mask;
};

struct flow_match_icmp {
	struct flow_dissector_key_icmp *key, *mask;
};

struct flow_match_tcp {
	struct flow_dissector_key_tcp *key, *mask;
};

struct flow_match_mpls {
	struct flow_dissector_key_mpls *key, *mask;
};

struct flow_match_enc_keyid {
	struct flow_dissector_key_keyid *key, *mask;
};

struct flow_match_enc_opts {
	struct flow_dissector_key_enc_opts *key, *mask;
};

struct flow_rule;

#define  flow_rule_match_basic LINUX_BACKPORT(flow_rule_match_basic)
void flow_rule_match_basic(const struct flow_rule *rule,
			   struct flow_match_basic *out);
#define  flow_rule_match_control LINUX_BACKPORT(flow_rule_match_control)
void flow_rule_match_control(const struct flow_rule *rule,
			     struct flow_match_control *out);
#define  flow_rule_match_eth_addrs LINUX_BACKPORT(flow_rule_match_eth_addrs)
void flow_rule_match_eth_addrs(const struct flow_rule *rule,
			       struct flow_match_eth_addrs *out);
#define  flow_rule_match_vlan LINUX_BACKPORT(flow_rule_match_vlan)
void flow_rule_match_vlan(const struct flow_rule *rule,
			  struct flow_match_vlan *out);
#define  flow_rule_match_cvlan LINUX_BACKPORT(flow_rule_match_cvlan)
void flow_rule_match_cvlan(const struct flow_rule *rule,
			   struct flow_match_vlan *out);
#define  flow_rule_match_ipv4_addrs LINUX_BACKPORT(flow_rule_match_ipv4_addrs)
void flow_rule_match_ipv4_addrs(const struct flow_rule *rule,
				struct flow_match_ipv4_addrs *out);
#define  flow_rule_match_ipv6_addrs LINUX_BACKPORT(flow_rule_match_ipv6_addrs)
void flow_rule_match_ipv6_addrs(const struct flow_rule *rule,
				struct flow_match_ipv6_addrs *out);
#define  flow_rule_match_ip LINUX_BACKPORT(flow_rule_match_ip)
void flow_rule_match_ip(const struct flow_rule *rule,
			struct flow_match_ip *out);
#define  flow_rule_match_ports LINUX_BACKPORT(flow_rule_match_ports)
void flow_rule_match_ports(const struct flow_rule *rule,
			   struct flow_match_ports *out);
#define  flow_rule_match_tcp LINUX_BACKPORT(flow_rule_match_tcp)
void flow_rule_match_tcp(const struct flow_rule *rule,
			 struct flow_match_tcp *out);
#define  flow_rule_match_icmp LINUX_BACKPORT(flow_rule_match_icmp)
void flow_rule_match_icmp(const struct flow_rule *rule,
			  struct flow_match_icmp *out);
#define  flow_rule_match_mpls LINUX_BACKPORT(flow_rule_match_mpls)
void flow_rule_match_mpls(const struct flow_rule *rule,
			  struct flow_match_mpls *out);
#define  flow_rule_match_enc_control LINUX_BACKPORT(flow_rule_match_enc_control)
void flow_rule_match_enc_control(const struct flow_rule *rule,
				 struct flow_match_control *out);
#define  flow_rule_match_enc_ipv4_addrs LINUX_BACKPORT(flow_rule_match_enc_ipv4_addrs)
void flow_rule_match_enc_ipv4_addrs(const struct flow_rule *rule,
				    struct flow_match_ipv4_addrs *out);
#define  flow_rule_match_enc_ipv6_addrs LINUX_BACKPORT(flow_rule_match_enc_ipv6_addrs)
void flow_rule_match_enc_ipv6_addrs(const struct flow_rule *rule,
				    struct flow_match_ipv6_addrs *out);
#define  flow_rule_match_enc_ip LINUX_BACKPORT(flow_rule_match_enc_ip)
void flow_rule_match_enc_ip(const struct flow_rule *rule,
			    struct flow_match_ip *out);
#define  flow_rule_match_enc_ports LINUX_BACKPORT(flow_rule_match_enc_ports)
void flow_rule_match_enc_ports(const struct flow_rule *rule,
			       struct flow_match_ports *out);
#define  flow_rule_match_enc_keyid LINUX_BACKPORT(flow_rule_match_enc_keyid)
void flow_rule_match_enc_keyid(const struct flow_rule *rule,
			       struct flow_match_enc_keyid *out);
#define flow_rule_match_enc_opts LINUX_BACKPORT(flow_rule_match_enc_opts)
void flow_rule_match_enc_opts(const struct flow_rule *rule,
			      struct flow_match_enc_opts *out);
#define flow_rule_match_cvlan LINUX_BACKPORT(flow_rule_match_cvlan)
void flow_rule_match_cvlan(const struct flow_rule *rule,
                           struct flow_match_vlan *out);

enum flow_action_id {
	FLOW_ACTION_ACCEPT		= 0,
	FLOW_ACTION_DROP,
	FLOW_ACTION_TRAP,
	FLOW_ACTION_GOTO,
	FLOW_ACTION_REDIRECT,
	FLOW_ACTION_MIRRED,
	FLOW_ACTION_VLAN_PUSH,
	FLOW_ACTION_VLAN_POP,
	FLOW_ACTION_VLAN_MANGLE,
	FLOW_ACTION_TUNNEL_ENCAP,
	FLOW_ACTION_TUNNEL_DECAP,
	FLOW_ACTION_MANGLE,
	FLOW_ACTION_ADD,
	FLOW_ACTION_CSUM,
	FLOW_ACTION_MARK,
	FLOW_ACTION_WAKE,
	FLOW_ACTION_QUEUE,
	FLOW_ACTION_SAMPLE,
	FLOW_ACTION_POLICE,
#ifdef HAVE_MINIFLOW
	FLOW_ACTION_CT,
#endif
};

/* This is mirroring enum pedit_header_type definition for easy mapping between
 * tc pedit action. Legacy TCA_PEDIT_KEY_EX_HDR_TYPE_NETWORK is mapped to
 * FLOW_ACT_MANGLE_UNSPEC, which is supported by no driver.
 */
enum flow_action_mangle_base {
	FLOW_ACT_MANGLE_UNSPEC		= 0,
	FLOW_ACT_MANGLE_HDR_TYPE_ETH,
	FLOW_ACT_MANGLE_HDR_TYPE_IP4,
	FLOW_ACT_MANGLE_HDR_TYPE_IP6,
	FLOW_ACT_MANGLE_HDR_TYPE_TCP,
	FLOW_ACT_MANGLE_HDR_TYPE_UDP,
};

struct flow_action_entry {
	enum flow_action_id		id;
	union {
		u32			chain_index;	/* FLOW_ACTION_GOTO */
		struct net_device	*dev;		/* FLOW_ACTION_REDIRECT */
		struct {				/* FLOW_ACTION_VLAN */
			u16		vid;
			__be16		proto;
			u8		prio;
		} vlan;
		struct {				/* FLOW_ACTION_PACKET_EDIT */
			enum flow_action_mangle_base htype;
			u32		offset;
			u32		mask;
			u32		val;
		} mangle;
		const struct ip_tunnel_info *tunnel;	/* FLOW_ACTION_TUNNEL_ENCAP */
		u32			csum_flags;	/* FLOW_ACTION_CSUM */
		u32			mark;		/* FLOW_ACTION_MARK */
		struct {				/* FLOW_ACTION_QUEUE */
			u32		ctx;
			u32		index;
			u8		vf;
		} queue;
		struct {				/* FLOW_ACTION_SAMPLE */
			struct psample_group	*psample_group;
			u32			rate;
			u32			trunc_size;
			bool			truncate;
		} sample;
		struct {				/* FLOW_ACTION_POLICE */
			s64			burst;
			u64			rate_bytes_ps;
		} police;
	};
};

struct flow_action {
	unsigned int			num_entries;
	struct flow_action_entry 	entries[0];
};

static inline bool flow_action_has_entries(const struct flow_action *action)
{
	return action->num_entries;
}

/**
 * flow_action_has_one_action() - check if exactly one action is present
 * @action: tc filter flow offload action
 *
 * Returns true if exactly one action is present.
 */
static inline bool flow_offload_has_one_action(const struct flow_action *action)
{
	return action->num_entries == 1;
}

#define flow_action_for_each(__i, __act, __actions)			\
        for (__i = 0, __act = &(__actions)->entries[0]; __i < (__actions)->num_entries; __act = &(__actions)->entries[++__i])

struct flow_rule {
	struct flow_match	match;
	struct flow_action	action;
};

struct flow_rule *flow_rule_alloc(unsigned int num_actions);

#if !defined(HAVE_FLOW_DISSECTOR_USES_KEY) && !defined(CONFIG_COMPAT_FLOW_DISSECTOR)
static bool dissector_uses_key(const struct flow_dissector *flow_dissector,
			       enum flow_dissector_key_id key_id)
{
	return flow_dissector->used_keys & (1 << key_id);
}

static void *skb_flow_dissector_target(struct flow_dissector *flow_dissector,
				       enum flow_dissector_key_id key_id,
				       void *target_container)
{
	return ((char *) target_container) + flow_dissector->offset[key_id];
}
#endif

static inline bool flow_rule_match_key(const struct flow_rule *rule,
				       enum flow_dissector_key_id key)
{
	return dissector_uses_key(rule->match.dissector, key);
}

struct flow_stats {
	u64	pkts;
	u64	bytes;
	u64	lastused;
};

static inline void flow_stats_update(struct flow_stats *flow_stats,
				     u64 bytes, u64 pkts, u64 lastused)
{
	flow_stats->pkts	+= pkts;
	flow_stats->bytes	+= bytes;
	flow_stats->lastused	= max_t(u64, flow_stats->lastused, lastused);
}
#endif /* HAVE_FLOW_RULE_MATCH_CVLAN */
#endif /* _NET_FLOW_OFFLOAD_H */
