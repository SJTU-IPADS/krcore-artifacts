#ifndef LINUX_IF_VLAN_H
#define LINUX_IF_VLAN_H

#include "../../compat/config.h"

#include_next <linux/if_vlan.h>

#ifndef skb_vlan_tag_present
#define skb_vlan_tag_present vlan_tx_tag_present
#define skb_vlan_tag_get vlan_tx_tag_get
#define skb_vlan_tag_get_id vlan_tx_tag_get_id
#endif

#ifndef skb_vlan_tag_get_prio
#define skb_vlan_tag_get_prio(__skb)   ((__skb)->vlan_tci & VLAN_PRIO_MASK)
#endif/*skb_vlan_tag_get_prio*/

#ifndef HAVE_IS_VLAN_DEV
static inline int is_vlan_dev(struct net_device *dev)
{
	return dev->priv_flags & IFF_802_1Q_VLAN;
}
#endif

#ifndef ETH_P_8021AD
#define ETH_P_8021AD    0x88A8          /* 802.1ad Service VLAN         */
#endif

#ifndef HAVE_VLAN_GET_PROTOCOL
/**
 * vlan_get_protocol - get protocol EtherType.
 * @skb: skbuff to query
 * @type: first vlan protocol
 * @depth: buffer to store length of eth and vlan tags in bytes
 *
 * Returns the EtherType of the packet, regardless of whether it is
 * vlan encapsulated (normal or hardware accelerated) or not.
 */
static inline __be16 __vlan_get_protocol(struct sk_buff *skb, __be16 type,
					 int *depth)
{
	unsigned int vlan_depth = skb->mac_len;

	/* if type is 802.1Q/AD then the header should already be
	 * present at mac_len - VLAN_HLEN (if mac_len > 0), or at
	 * ETH_HLEN otherwise
	 */
	if (type == htons(ETH_P_8021Q) || type == htons(ETH_P_8021AD)) {
		if (vlan_depth) {
			if (WARN_ON(vlan_depth < VLAN_HLEN))
				return 0;
			vlan_depth -= VLAN_HLEN;
		} else {
			vlan_depth = ETH_HLEN;
		}
		do {
			struct vlan_hdr *vh;

			if (unlikely(!pskb_may_pull(skb,
						    vlan_depth + VLAN_HLEN)))
				return 0;

			vh = (struct vlan_hdr *)(skb->data + vlan_depth);
			type = vh->h_vlan_encapsulated_proto;
			vlan_depth += VLAN_HLEN;
		} while (type == htons(ETH_P_8021Q) ||
			 type == htons(ETH_P_8021AD));
	}

	if (depth)
		*depth = vlan_depth;

	return type;
}
#endif

#ifndef HAVE_VLAN_GET_ENCAP_LEVEL
static inline int vlan_get_encap_level(struct net_device *dev)
{
	BUG_ON(!is_vlan_dev(dev));
	return 0;
}
#endif

#endif /* LINUX_IF_VLAN_H */
