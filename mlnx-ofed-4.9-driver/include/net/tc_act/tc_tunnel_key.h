#ifndef _COMPAT_NET_TC_ACT_TC_TUNNEL_KEY_H
#define _COMPAT_NET_TC_ACT_TC_TUNNEL_KEY_H 1
#include "../../../compat/config.h"

#ifndef CONFIG_COMPAT_TCF_TUNNEL_KEY_MOD
#ifdef HAVE_IS_TCF_TUNNEL
#include_next <net/tc_act/tc_tunnel_key.h>
#endif
#else /* CONFIG_COMPAT_TCF_TUNNEL_KEY_MOD */
#ifdef CONFIG_NET_SCHED_NEW
/*
 * Copyright (c) 2016, Amir Vadai <amir@vadai.me>
 * Copyright (c) 2016, Mellanox Technologies. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <net/act_api.h>
#include <linux/tc_act/tc_tunnel_key.h>
#include <net/dst_metadata.h>

struct tcf_tunnel_key_params {
	struct rcu_head		rcu;
	int			tcft_action;
	int			action;
	struct metadata_dst     *tcft_enc_metadata;
};

struct tcf_tunnel_key {
	struct tc_action	      common;
	struct tcf_tunnel_key_params __rcu *params;
};

#define to_tunnel_key(a) ((struct tcf_tunnel_key *)a)
#define HAVE_IS_TCF_TUNNEL 1
static inline bool is_tcf_tunnel_set(const struct tc_action *a)
{
#ifdef CONFIG_NET_CLS_ACT
	struct tcf_tunnel_key *t = to_tunnel_key(a);
	struct tcf_tunnel_key_params *params = rtnl_dereference(t->params);

	if (a->ops && a->ops->type == TCA_ACT_TUNNEL_KEY)
		return params->tcft_action == TCA_TUNNEL_KEY_ACT_SET;
#endif
	return false;
}

static inline bool is_tcf_tunnel_release(const struct tc_action *a)
{
#ifdef CONFIG_NET_CLS_ACT
	struct tcf_tunnel_key *t = to_tunnel_key(a);
	struct tcf_tunnel_key_params *params = rtnl_dereference(t->params);

	if (a->ops && a->ops->type == TCA_ACT_TUNNEL_KEY)
		return params->tcft_action == TCA_TUNNEL_KEY_ACT_RELEASE;
#endif
	return false;
}

#define HAVE_TCF_TUNNEL_INFO 1
static inline struct ip_tunnel_info *tcf_tunnel_info(const struct tc_action *a)
{
#ifdef CONFIG_NET_CLS_ACT
	struct tcf_tunnel_key *t = to_tunnel_key(a);
	struct tcf_tunnel_key_params *params = rtnl_dereference(t->params);

	return &params->tcft_enc_metadata->u.tun_info;
#else
	return NULL;
#endif
}

#else	/* CONFIG_NET_SCHED_NEW */

#include <uapi/linux/tc_act/tc_tunnel_key.h>

static inline bool is_tcf_tunnel_key(const struct tc_action *a)
{
#ifdef CONFIG_NET_CLS_ACT
	if (a->ops && a->ops->type == TCA_ACT_TUNNEL_KEY)
		return true;
#endif
	return false;
}

#include <net/ip_tunnels.h>
#include <net/netlink.h>
#include <linux/skbuff.h>

static const struct nla_policy tunnel_key_pol[TCA_TUNNEL_KEY_MAX + 1] = {
	[TCA_TUNNEL_KEY_PARMS]	    = { .len = sizeof(struct tc_tunnel_key) },
	[TCA_TUNNEL_KEY_ENC_IPV4_SRC] = { .type = NLA_U32 },
	[TCA_TUNNEL_KEY_ENC_IPV4_DST] = { .type = NLA_U32 },
	[TCA_TUNNEL_KEY_ENC_IPV6_SRC] = { .len = sizeof(struct in6_addr) },
	[TCA_TUNNEL_KEY_ENC_IPV6_DST] = { .len = sizeof(struct in6_addr) },
	[TCA_TUNNEL_KEY_ENC_KEY_ID]   = { .type = NLA_U32 },
	[TCA_TUNNEL_KEY_ENC_DST_PORT] = { .type = NLA_U16 },
#ifdef HAVE_TCA_TUNNEL_KEY_ENC_TOS
	[TCA_TUNNEL_KEY_ENC_TOS]      = { .type = NLA_U8 },
	[TCA_TUNNEL_KEY_ENC_TTL]      = { .type = NLA_U8 },
#endif
};

struct netlink_tunnel_key {
	struct tc_tunnel_key tunnel_key;
	__be32 ipv4_src;
	__be32 ipv4_dst;
	struct in6_addr ipv6_src;
	struct in6_addr ipv6_dst;
	u32 id;
	u16 dstport;
	u8 tos;
	u8 ttl;
};

static struct netlink_tunnel_key to_tunnel_key_comp(const struct tc_action *a)
{
	struct nlattr *tb[TCA_TUNNEL_KEY_MAX + 1];
	struct netlink_tunnel_key t = { .id = 0 };
	struct sk_buff *skb;
	struct nlattr *nla;

	if (!a->ops || !a->ops->dump || !is_tcf_tunnel_key(a))
		return t;

	skb = alloc_skb(256, GFP_KERNEL);
	if (!skb)
		return t;

	if (a->ops->dump(skb, (struct tc_action *) a, 0, 0) < 0)
		goto freeskb;

	nla = (struct nlattr *) skb->data;
	if (nla_parse(tb, TCA_TUNNEL_KEY_MAX, nla, skb->len, tunnel_key_pol, NULL) < 0)
		goto freeskb;

	if (!tb[TCA_TUNNEL_KEY_PARMS])
		goto freeskb;

	t.tunnel_key = *((struct tc_tunnel_key *) nla_data(tb[TCA_TUNNEL_KEY_PARMS]));
	if (tb[TCA_TUNNEL_KEY_ENC_IPV4_SRC])
		t.ipv4_src = nla_get_be32(tb[TCA_TUNNEL_KEY_ENC_IPV4_SRC]);
	if (tb[TCA_TUNNEL_KEY_ENC_IPV4_DST])
		t.ipv4_dst = nla_get_be32(tb[TCA_TUNNEL_KEY_ENC_IPV4_DST]);
	if (tb[TCA_TUNNEL_KEY_ENC_IPV6_SRC])
		t.ipv6_src =
			nla_get_in6_addr(tb[TCA_TUNNEL_KEY_ENC_IPV6_SRC]);
	if (tb[TCA_TUNNEL_KEY_ENC_IPV6_DST])
		t.ipv6_dst =
			nla_get_in6_addr(tb[TCA_TUNNEL_KEY_ENC_IPV6_DST]);
	if (tb[TCA_TUNNEL_KEY_ENC_KEY_ID])
		t.id = nla_get_u32(tb[TCA_TUNNEL_KEY_ENC_KEY_ID]);
	if (tb[TCA_TUNNEL_KEY_ENC_DST_PORT])
		t.dstport = nla_get_u16(tb[TCA_TUNNEL_KEY_ENC_DST_PORT]);
	if (tb[TCA_TUNNEL_KEY_ENC_TOS])
		t.tos = nla_get_u8(tb[TCA_TUNNEL_KEY_ENC_TOS]);
	if (tb[TCA_TUNNEL_KEY_ENC_TTL])
		t.ttl = nla_get_u8(tb[TCA_TUNNEL_KEY_ENC_TTL]);

freeskb:
	kfree_skb(skb);

	return t;
}

static inline bool is_tcf_tunnel_set(const struct tc_action *a)
{
	struct tc_tunnel_key t = to_tunnel_key_comp(a).tunnel_key;

	return t.t_action == TCA_TUNNEL_KEY_ACT_SET;
}

static inline bool is_tcf_tunnel_release(const struct tc_action *a)
{
	struct tc_tunnel_key t = to_tunnel_key_comp(a).tunnel_key;

	return t.t_action == TCA_TUNNEL_KEY_ACT_RELEASE;
}

static inline void tcf_tunnel_info_compat(const struct tc_action *a,
					  struct ip_tunnel_info *info)
{
	struct netlink_tunnel_key nt = to_tunnel_key_comp(a);
	struct ip_tunnel_key *key;

	memset(info, 0, sizeof(*info));

	key = &info->key;

	if (nt.ipv4_dst || nt.ipv4_src) {
		key->u.ipv4.src = nt.ipv4_src;
		key->u.ipv4.dst = nt.ipv4_dst;
	} else if (memchr_inv(&nt.ipv6_dst, 0, sizeof(nt.ipv6_dst))) {
		key->u.ipv6.src = nt.ipv6_src;
		key->u.ipv6.dst = nt.ipv6_dst;
		info->mode |= IP_TUNNEL_INFO_IPV6;
	}

	key->tp_dst = nt.dstport;
	key->tun_id = key32_to_tunnel_id(nt.id);
	key->tos = nt.tos;
	key->ttl = nt.ttl;
}

#endif	/* CONFIG_NET_SCHED_NEW */

#endif	/* CONFIG_COMPAT_TCF_TUNNEL_KEY_MOD */

#endif	/* _COMPAT_NET_TC_ACT_TC_TUNNEL_KEY_H */
