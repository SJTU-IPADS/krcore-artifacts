#ifndef _COMPAT_NET_DST_H
#define _COMPAT_NET_DST_H 1

#include "../../compat/config.h"

#include_next <net/dst.h>

#ifndef HAVE_SKB_DST_UPDATE_PMTU
#define skb_dst_update_pmtu LINUX_BACKPORT(skb_dst_update_pmtu)
static inline void skb_dst_update_pmtu(struct sk_buff *skb, u32 mtu)
{
	struct dst_entry *dst = skb_dst(skb);

	if (dst && dst->ops->update_pmtu)
#if defined HAVE_UPDATE_PMTU_4_PARAMS
		dst->ops->update_pmtu(dst, NULL, skb, mtu);
#else
		dst->ops->update_pmtu(dst, mtu);
#endif
}
#endif

#endif	/* _COMPAT_NET_DST_H */
