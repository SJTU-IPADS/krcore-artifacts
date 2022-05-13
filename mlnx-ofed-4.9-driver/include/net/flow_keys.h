#ifndef COMPAT_LINUX_FLOW_KEYS_H
#define COMPAT_LINUX_FLOW_KEYS_H

#include "../../compat/config.h"

#ifdef HAVE_NET_FLOW_KEYS_H
#include_next <net/flow_keys.h>
#else /* HAVE_NET_FLOW_KEYS_H */

struct flow_keys {
	/* (src,dst) must be grouped, in the same way than in IP header */
	__be32 src;
	__be32 dst;
	union {
		__be32 ports;
		__be16 port16[2];
	};
	u8 ip_proto;
};

#define skb_flow_dissect LINUX_BACKPORT(skb_flow_dissect)
extern bool skb_flow_dissect(const struct sk_buff *skb, struct flow_keys *flow);

#endif /* HAVE_NET_FLOW_KEYS_H */

#endif /* COMPAT_LINUX_FLOW_KEYS_H */
