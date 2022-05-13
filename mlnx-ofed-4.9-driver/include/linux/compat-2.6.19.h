#ifndef LINUX_26_19_COMPAT_H
#define LINUX_26_19_COMPAT_H

#include <linux/version.h>

/* Compat work for 2.6.18 */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,19))
#include <linux/mm.h>
#include <linux/dma-mapping.h>
#include <linux/log2.h>
#include <linux/random.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/in.h>
#include <linux/bitmap.h>
#include <linux/ethtool.h>
#include <linux/string.h>
#include <linux/compiler.h>

/*
 * definitions for some exported functions under compat-2.6.(16|18)/
 */
#define kobject_create_and_add LINUX_BACKPORT(kobject_create_and_add)
struct kobject *kobject_create_and_add(const char *name, struct kobject
				       *parent);
#define kobject_init_and_add LINUX_BACKPORT(kobject_init_and_add)
int kobject_init_and_add(struct kobject *kobj, struct kobj_type *ktype,
			 struct kobject *parent, const char *fmt, ...);
#define kvfree LINUX_BACKPORT(kvfree)
void kvfree(const void *addr);

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,16))
#include <linux/uaccess.h>
#include <linux/skbuff.h>

#ifndef CONFIG_COMPAT_NETIF_HAS_PICK_TX
#define __netdev_pick_tx LINUX_BACKPORT(__netdev_pick_tx)
u16 __netdev_pick_tx(struct net_device *dev, struct sk_buff *skb);

#define __skb_tx_hash LINUX_BACKPORT(__skb_tx_hash)
u16 __skb_tx_hash(const struct net_device *dev,
		  const struct sk_buff *skb);

/*
 * Returns a Tx hash for the given packet when dev->real_num_tx_queues is used
 * as a distribution range limit for the returned value.
 */
static inline u16 skb_tx_hash(const struct net_device *dev,
			      const struct sk_buff *skb)
{
	return __skb_tx_hash(dev, skb);
}
#endif
#endif /* LINUX_VERSION_CODE > KERNEL_VERSION(2,6,16) */

#define roundup(x, y)	((((x) + ((y) - 1)) / (y)) * (y))

#define CONFIG_COMPAT_MLNX_EN_ONLY 1
#define HAVE_OLD_NAPI 1
#define HAVE_OLD_NUMA 1
#define HAVE_NO_AFFINITY 1
#define HAVE_NO_DEBUGFS 1
#define HAVE_CANNOT_KZALLOC_THAT_MUCH 1

#ifndef __packed
#define __packed                   __attribute__((packed))
#endif

static inline long IS_ERR_OR_NULL(const void *ptr)
{
	return !ptr || IS_ERR_VALUE((unsigned long)ptr);
}

/* Taken from compat-2.6.37.h */

static inline void *dma_zalloc_coherent(struct device *dev, size_t size,
					dma_addr_t *dma_handle, gfp_t flag)
{
	void *ret = dma_alloc_coherent(dev, size, dma_handle, flag);
	if (ret)
		memset(ret, 0, size);
	return ret;
}
/*
 * Determine if an address is within the vmalloc range
 *
 * On nommu, vmalloc/vfree wrap through kmalloc/kfree directly, so there
 * is no special casing required.
 */
static inline int isvmalloc_addr(const void *x)
{
#ifdef CONFIG_MMU
	unsigned long addr = (unsigned long) x;

	return addr >= VMALLOC_START && addr < VMALLOC_END;
#else
	return 0;
#endif
}

/* Taken from compat-3.15.h */
extern void kvfree(const void *addr);

typedef unsigned long             uintptr_t;

#define ETH_GSTRING_LEN            32

#define order_base_2(n) ilog2(roundup_pow_of_two(n))

#ifdef __GNUC__
#define __always_unused			__attribute__((unused))
#else
#define __always_unused			/* unimplemented */
#endif

#ifndef NETIF_F_HW_VLAN_CTAG_RX
#define NETIF_F_HW_VLAN_CTAG_RX NETIF_F_HW_VLAN_RX
#endif

#ifndef NETIF_F_HW_VLAN_CTAG_TX
#define NETIF_F_HW_VLAN_CTAG_TX NETIF_F_HW_VLAN_TX
#endif

#ifndef NETIF_F_HW_VLAN_CTAG_FILTER
#define NETIF_F_HW_VLAN_CTAG_FILTER NETIF_F_HW_VLAN_FILTER
#endif

#ifndef prandom_u32
#define prandom_u32() random32()
#endif

#ifndef NETIF_F_GSO_UDP_TUNNEL
#define NETIF_F_GSO_UDP_TUNNEL (1 << 25)
#endif

typedef int netdev_tx_t;

#define VLAN_N_VID              4096

/* Logging, debugging and troubleshooting/diagnostic helpers. */

/* netdev_printk helpers, similar to dev_printk */

#ifndef netdev_name
#define netdev_name(__dev) \
	((__dev->reg_state != NETREG_REGISTERED) ? \
		"(unregistered net_device)" : __dev->name)
#endif

#define netdev_printk(level, netdev, format, args...) \
	do {} while (0)
/*
#define netdev_printk(level, netdev, format, args...)		\
	dev_printk(level, (netdev)->dev.parent,			\
		   "%s: " format,				\
		   netdev_name(netdev), ##args)
*/

#define netdev_emerg(dev, format, args...)			\
	netdev_printk(KERN_EMERG, dev, format, ##args)
#define netdev_alert(dev, format, args...)			\
	netdev_printk(KERN_ALERT, dev, format, ##args)
#define netdev_crit(dev, format, args...)			\
	netdev_printk(KERN_CRIT, dev, format, ##args)
#define netdev_err(dev, format, args...)			\
	netdev_printk(KERN_ERR, dev, format, ##args)
#define netdev_warn(dev, format, args...)			\
	netdev_printk(KERN_WARNING, dev, format, ##args)
#define netdev_notice(dev, format, args...)			\
	netdev_printk(KERN_NOTICE, dev, format, ##args)
#define netdev_info(dev, format, args...)			\
	netdev_printk(KERN_INFO, dev, format, ##args)

/* mask netdev_dbg as RHEL6 backports this */
#if !defined(netdev_dbg)

#if defined(DEBUG)
#define netdev_dbg(__dev, format, args...)			\
	netdev_printk(KERN_DEBUG, __dev, format, ##args)
#elif defined(CONFIG_DYNAMIC_DEBUG)
#define netdev_dbg(__dev, format, args...)			\
do {								\
	dynamic_dev_dbg((__dev)->dev.parent, "%s: " format,	\
			netdev_name(__dev), ##args);		\
} while (0)
#else
#define netdev_dbg(__dev, format, args...)			\
({								\
	if (0)							\
		netdev_printk(KERN_DEBUG, __dev, format, ##args); \
	0;							\
})
#endif

#endif

/* mask netdev_vdbg as RHEL6 backports this */
#if !defined(netdev_dbg)

#if defined(VERBOSE_DEBUG)
#define netdev_vdbg	netdev_dbg
#else

#define netdev_vdbg(dev, format, args...)			\
({								\
	if (0)							\
		netdev_printk(KERN_DEBUG, dev, format, ##args);	\
	0;							\
})
#endif

#endif

/*
 * netdev_WARN() acts like dev_printk(), but with the key difference
 * of using a WARN/WARN_ON to get the message out, including the
 * file/line information and a backtrace.
 */
#define netdev_WARN(dev, format, args...)			\
	WARN(1, "netdevice: %s\n" format, netdev_name(dev), ##args);

/* netif printk helpers, similar to netdev_printk */

#define netif_printk(priv, type, level, dev, fmt, args...)	\
do {					  			\
	if (netif_msg_##type(priv))				\
		netdev_printk(level, (dev), fmt, ##args);	\
} while (0)

#define netif_emerg(priv, type, dev, fmt, args...)		\
	netif_printk(priv, type, KERN_EMERG, dev, fmt, ##args)
#define netif_alert(priv, type, dev, fmt, args...)		\
	netif_printk(priv, type, KERN_ALERT, dev, fmt, ##args)
#define netif_crit(priv, type, dev, fmt, args...)		\
	netif_printk(priv, type, KERN_CRIT, dev, fmt, ##args)
#define netif_err(priv, type, dev, fmt, args...)		\
	netif_printk(priv, type, KERN_ERR, dev, fmt, ##args)
#define netif_warn(priv, type, dev, fmt, args...)		\
	netif_printk(priv, type, KERN_WARNING, dev, fmt, ##args)
#define netif_notice(priv, type, dev, fmt, args...)		\
	netif_printk(priv, type, KERN_NOTICE, dev, fmt, ##args)
#define netif_info(priv, type, dev, fmt, args...)		\
	netif_printk(priv, type, KERN_INFO, (dev), fmt, ##args)

/* mask netif_dbg as RHEL6 backports this */
#if !defined(netif_dbg)

#if defined(DEBUG)
#define netif_dbg(priv, type, dev, format, args...)		\
	netif_printk(priv, type, KERN_DEBUG, dev, format, ##args)
#elif defined(CONFIG_DYNAMIC_DEBUG)
#define netif_dbg(priv, type, netdev, format, args...)		\
do {								\
	if (netif_msg_##type(priv))				\
		dynamic_dev_dbg((netdev)->dev.parent,		\
				"%s: " format,			\
				netdev_name(netdev), ##args);	\
} while (0)
#else
#define netif_dbg(priv, type, dev, format, args...)			\
({									\
	if (0)								\
		netif_printk(priv, type, KERN_DEBUG, dev, format, ##args); \
	0;								\
})
#endif

#endif

/* mask netif_vdbg as RHEL6 backports this */
#if !defined(netif_vdbg)

#if defined(VERBOSE_DEBUG)
#define netif_vdbg	netdev_dbg
#else
#define netif_vdbg(priv, type, dev, format, args...)		\
({								\
	if (0)							\
		netif_printk(KERN_DEBUG, dev, format, ##args);	\
	0;							\
})
#endif
#endif

static inline bool ether_addr_equal(const u8 *addr1, const u8 *addr2)
{
#if defined(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS)
	u32 fold = ((*(const u32 *)addr1) ^ (*(const u32 *)addr2)) |
			((*(const u16 *)(addr1 + 4)) ^ (*(const u16 *)(addr2 + 4)));

	return fold == 0;
#else
	const u16 *a = (const u16 *)addr1;
	const u16 *b = (const u16 *)addr2;

	return ((a[0] ^ b[0]) | (a[1] ^ b[1]) | (a[2] ^ b[2])) == 0;
#endif
}

static inline bool ether_addr_equal_64bits(const u8 addr1[6+2],
					   const u8 addr2[6+2])
{
#if defined(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS) && BITS_PER_LONG == 64
	u64 fold = (*(const u64 *)addr1) ^ (*(const u64 *)addr2);
 
#ifdef __BIG_ENDIAN
	return (fold >> 16) == 0;
#else
	return (fold << 16) == 0;
#endif
#else
	return ether_addr_equal(addr1, addr2);
#endif
}

static inline bool is_unicast_ether_addr(const u8 *addr)
{
	return !is_multicast_ether_addr(addr);
}

#define for_each_set_bit(bit, addr, size) \
	for ((bit) = find_first_bit((addr), (size));            \
	     (bit) < (size);                                    \
	     (bit) = find_next_bit((addr), (size), (bit) + 1))

#define netdev_hw_addr_list_for_each(ha, l) \
	list_for_each_entry(ha, &(l)->list, list)

#define netdev_for_each_mc_addr(ha, dev) \
	netdev_hw_addr_list_for_each(ha, &(dev)->mc)

#define order_base_2(n) ilog2(roundup_pow_of_two(n))

static inline void _compat__list_splice(const struct list_head *list,
					struct list_head *prev,
					struct list_head *next)
{
	struct list_head *first = list->next;
	struct list_head *last = list->prev;

	first->prev = prev;
	prev->next = first;

	last->next = next;
	next->prev = last;
}

/**
 * list_splice_tail_init - join two lists and reinitialise the emptied list
 * @list: the new list to add.
 * @head: the place to add it in the first list.
 *
 * Each of the lists is a queue.
 * The list at @list is reinitialised
 */
static inline void list_splice_tail_init(struct list_head *list,
					 struct list_head *head)
{
	if (!list_empty(list)) {
		_compat__list_splice(list, head->prev, head);
		INIT_LIST_HEAD(list);
	}
}

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,16))
/**
 * list_prev_entry - get the prev element in list
 * @pos:	the type * to cursor
 * @member:	the name of the list_head within the struct.
 */
#define list_prev_entry(pos, member) \
	list_entry((pos)->member.prev, typeof(*(pos)), member)

#define list_for_each_entry_continue_reverse(pos, head, member)		\
	for (pos = list_prev_entry(pos, member);			\
	     &pos->member != (head);					\
	     pos = list_prev_entry(pos, member))
#endif

#undef WARN_ON
#define WARN_ON(condition) ({						\
	int __ret_warn_on = !!(condition);				\
	if (unlikely(__ret_warn_on))					\
		dump_stack();						\
	unlikely(__ret_warn_on);					\
})

static inline const char *kstrdup_const(const char *s, gfp_t gfp)
{
	return kstrdup(s, gfp);
}

static inline void kfree_const(const void *x)
{
	kfree(x);
}

#define SUPPORTED_Backplane             (1 << 16)
#define SUPPORTED_1000baseKX_Full       (1 << 17)
#define SUPPORTED_10000baseKX4_Full     (1 << 18)
#define SUPPORTED_10000baseKR_Full      (1 << 19)
#define ADVERTISED_1000baseKX_Full      (1 << 17)
#define ADVERTISED_10000baseKX4_Full    (1 << 18)
#define ADVERTISED_10000baseKR_Full     (1 << 19)

#define PORT_DA                 0x05
#define PORT_NONE               0xef
#define PORT_OTHER              0xff

static inline unsigned int skb_frag_size(const skb_frag_t *frag)
{
	return frag->size;
}

static inline struct page *skb_frag_page(const skb_frag_t *frag)
{
	return frag->page;
}

static inline dma_addr_t skb_frag_dma_map(struct device *dev,
					  const skb_frag_t *frag,
					  size_t offset, size_t size,
					  enum dma_data_direction dir)
{
	return dma_map_page(dev, skb_frag_page(frag),
			    frag->page_offset + offset, size, dir);
}

#ifdef NET_SKBUFF_DATA_USES_OFFSET
static inline unsigned int skb_end_offset(const struct sk_buff *skb)
{
	return skb->end;
}
#else
static inline unsigned int skb_end_offset(const struct sk_buff *skb)
{
	return skb->end - skb->head;
}
#endif

static inline void ethtool_cmd_speed_set(struct ethtool_cmd *ep,
					 __u32 speed)
{
	ep->speed = (__u16)speed;
}

static inline __u32 ethtool_cmd_speed(const struct ethtool_cmd *ep)
{
	return ep->speed;
}

static inline void set_dev_node(struct device *dev, int node)
{
}

#define skb_get_queue_mapping LINUX_BACKPORT(skb_get_queue_mapping)
static inline u16 skb_get_queue_mapping(struct sk_buff *skb)
{
#ifdef HAVE_ALLOC_ETHERDEV_MQ
	return skb->queue_mapping;
#else
	return 0;
#endif
}

#define skb_record_rx_queue LINUX_BACKPORT(skb_record_rx_queue)
static inline void skb_record_rx_queue(struct sk_buff *skb, u16 rx_queue)
{
}

/**
 * __skb_frag_set_page - sets the page contained in a paged fragment
 * @frag: the paged fragment
 * @page: the page to set
 *
 * Sets the fragment @frag to contain @page.
 */
#define __skb_frag_set_page LINUX_BACKPORT(__skb_frag_set_page)
static inline void __skb_frag_set_page(skb_frag_t *frag, struct page *page)
{
	frag->page = page;
}

/**
 * skb_frag_set_page - sets the page contained in a paged fragment of an skb
 * @skb: the buffer
 * @f: the fragment offset
 * @page: the page to set
 *
 * Sets the @f'th fragment of @skb to contain @page.
 */
#define skb_frag_set_page LINUX_BACKPORT(skb_frag_set_page)
static inline void skb_frag_set_page(struct sk_buff *skb, int f,
				     struct page *page)
{
	__skb_frag_set_page(&skb_shinfo(skb)->frags[f], page);
}

#define skb_frag_size_set LINUX_BACKPORT(skb_frag_size_set)
static inline void skb_frag_size_set(skb_frag_t *frag, unsigned int size)
{
	frag->size = size;
}

#define __WARN_printf(arg...)   do { printk(KERN_WARNING arg); } while (0)

#define WARN(condition, format...) ({           \
	int __ret_warn_on = !!(condition);      \
	if (unlikely(__ret_warn_on))            \
		__WARN_printf(format);          \
	unlikely(__ret_warn_on);                \
})

#define WARN_ONCE(condition, format...) ({      \
	static int __warned;                    \
	int __ret_warn_once = !!(condition);    \
	if (unlikely(__ret_warn_once))          \
		if (WARN(!__warned, format))    \
			__warned = 1;           \
	unlikely(__ret_warn_once);              \
})

#include <linux/etherdevice.h>

/**
 * eth_broadcast_addr - Assign broadcast address
 * @addr: Pointer to a six-byte array containing the Ethernet address
 *
 * Assign the broadcast address to the given address array.
 */
#define eth_broadcast_addr LINUX_BACKPORT(eth_broadcast_addr)
static inline void eth_broadcast_addr(u8 *addr)
{
	memset(addr, 0xff, ETH_ALEN);
}

/**
 * eth_zero_addr - Assign zero address
 * @addr: Pointer to a six-byte array containing the Ethernet address
 *
 * Assign the zero address to the given address array.
 */
#define eth_zero_addr LINUX_BACKPORT(eth_zero_addr)
static inline void eth_zero_addr(u8 *addr)
{
	memset(addr, 0x00, ETH_ALEN);
}

#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,19)) */

#endif /* LINUX_26_19_COMPAT_H */
