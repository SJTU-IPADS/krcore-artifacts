#ifndef _COMPAT_LINUX_SKBUFF_H
#define _COMPAT_LINUX_SKBUFF_H

#include "../../compat/config.h"

#include <linux/version.h>
#ifdef HAVE_LINUX_SIPHASH_H
#include <linux/siphash.h>
#endif

#include_next <linux/skbuff.h>


#ifndef HAVE_DEV_ALLOC_PAGES
static inline struct page *dev_alloc_pages(unsigned int order)
{
	gfp_t gfp_mask = GFP_ATOMIC | __GFP_NOWARN | __GFP_COLD | __GFP_COMP | __GFP_MEMALLOC;
	return alloc_pages_node(NUMA_NO_NODE, gfp_mask, order);
}
#endif
#ifndef HAVE_DEV_ALLOC_PAGE
static inline struct page *dev_alloc_page(void)
{
	return dev_alloc_pages(0);
}
#endif
#ifndef HAVE_SKB_PULL_INLINE
static inline unsigned char *skb_pull_inline(struct sk_buff *skb, unsigned int len)
{
	return unlikely(len > skb->len) ? NULL : __skb_pull(skb, len);
}
#endif /* HAVE_SKB_PULL_INLINE */

#ifndef SKB_TRUESIZE
#define SKB_TRUESIZE(X) ((X) +						\
			SKB_DATA_ALIGN(sizeof(struct sk_buff)) +	\
			SKB_DATA_ALIGN(sizeof(struct skb_shared_info)))
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,28))
#define v2_6_28_skb_add_rx_frag LINUX_BACKPORT(v2_6_28_skb_add_rx_frag)
extern void v2_6_28_skb_add_rx_frag(struct sk_buff *skb, int i, struct page *page,
			    int off, int size);
#define skb_add_rx_frag(skb, i, page, off, size, truesize) \
	v2_6_28_skb_add_rx_frag(skb, i, page, off, size)
#else /* LINUX_VERSION_CODE < KERNEL_VERSION(2,6,28) */
#ifdef HAVE_SKB_ADD_RX_FRAG_5_PARAMS
#define skb_add_rx_frag(skb, i, page, off, size, truesize) \
	skb_add_rx_frag(skb, i, page, off, size)
#endif /* HAVE_SKB_ADD_RX_FRAG_5_PARAMS */
#endif /*  LINUX_VERSION_CODE < KERNEL_VERSION(2,6,28) */

#ifndef HAVE_SKB_PUT_ZERO
#define skb_put_zero LINUX_BACKPORT(skb_put_zero)
static inline void *skb_put_zero(struct sk_buff *skb, unsigned int len)
{
	void *tmp = skb_put(skb, len);

	memset(tmp, 0, len);

	return tmp;
}
#endif

#ifndef HAVE_SKB_CLEAR_HASH
static inline void skb_clear_hash(struct sk_buff *skb)
{
#ifdef HAVE_SKB_RXHASH
	skb->rxhash = 0;
#endif
#ifdef HAVE_SKB_L4_RXHASH
	skb->l4_rxhash = 0;
#endif
}
#endif

#ifndef HAVE_SKB_FRAG_OFF_ADD
static inline void skb_frag_off_add(skb_frag_t *frag, int delta)
{
	frag->page_offset += delta;
}
#endif
#endif /* _COMPAT_LINUX_SKBUFF_H */
