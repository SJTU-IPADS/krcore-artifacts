#ifndef LINUX_26_38_COMPAT_H
#define LINUX_26_38_COMPAT_H

#include <linux/version.h>

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,38))

#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/etherdevice.h>
#include <net/sch_generic.h>

#define __DEFERRED_WORK_INITIALIZER(n, f) {			\
	.work = __WORK_INITIALIZER((n).work, (f)),		\
	.timer = TIMER_DEFERRED_INITIALIZER(NULL, 0, 0),	\
	}

#define DECLARE_DEFERRED_WORK(n, f)                            \
	struct delayed_work n = __DEFERRED_WORK_INITIALIZER(n, f)

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,30))
static inline void bstats_update(struct gnet_stats_basic_packed *bstats,
				 const struct sk_buff *skb)
{
	bstats->bytes += qdisc_pkt_len((struct sk_buff *) skb);
	bstats->packets += skb_is_gso(skb) ? skb_shinfo(skb)->gso_segs : 1;
}
static inline void qdisc_bstats_update(struct Qdisc *sch,
				       const struct sk_buff *skb)
{
	bstats_update(&sch->bstats, skb);
}
#else
/*
 * kernels <= 2.6.30 do not pass a const skb to qdisc_pkt_len, and
 * gnet_stats_basic_packed did not exist (see c1a8f1f1c8)
 */
static inline void bstats_update(struct gnet_stats_basic *bstats,
				 struct sk_buff *skb)
{
	bstats->bytes += qdisc_pkt_len(skb);
	bstats->packets += skb_is_gso(skb) ? skb_shinfo(skb)->gso_segs : 1;
}
static inline void qdisc_bstats_update(struct Qdisc *sch,
				       struct sk_buff *skb)
{
	bstats_update(&sch->bstats, skb);
}
#endif


/* rename member in struct mmc_host in include/linux/mmc/host.h */
#define max_segs	max_hw_segs


/* Exponentially weighted moving average (EWMA) */

/* For more documentation see lib/average.c */

struct ewma {
	unsigned long internal;
	unsigned long factor;
	unsigned long weight;
};

#define ewma_init LINUX_BACKPORT(ewma_init)
extern void ewma_init(struct ewma *avg, unsigned long factor,
		      unsigned long weight);

#define ewma_add LINUX_BACKPORT(ewma_add)
extern struct ewma *ewma_add(struct ewma *avg, unsigned long val);

#define ewma_read LINUX_BACKPORT(ewma_read)
/**
 * ewma_read() - Get average value
 * @avg: Average structure
 *
 * Returns the average value held in @avg.
 */
static inline unsigned long ewma_read(const struct ewma *avg)
{
	return DIV_ROUND_CLOSEST(avg->internal, avg->factor);
}

#define pr_warn pr_warning
#ifndef create_freezable_workqueue
#define create_freezable_workqueue create_freezeable_workqueue
#endif

/* mask skb_checksum_start_offset as RHEL6.4 backports this */
#define skb_checksum_start_offset(a) compat_skb_checksum_start_offset(a)
static inline int skb_checksum_start_offset(const struct sk_buff *skb)
{
	return skb->csum_start - skb_headroom(skb);
}

/* from include/linux/printk.h */ 
#define pr_emerg_once(fmt, ...)					\
	printk_once(KERN_EMERG pr_fmt(fmt), ##__VA_ARGS__)
#define pr_alert_once(fmt, ...)					\
	printk_once(KERN_ALERT pr_fmt(fmt), ##__VA_ARGS__)
#define pr_crit_once(fmt, ...)					\
	printk_once(KERN_CRIT pr_fmt(fmt), ##__VA_ARGS__)
#define pr_err_once(fmt, ...)					\
	printk_once(KERN_ERR pr_fmt(fmt), ##__VA_ARGS__)
#define pr_warn_once(fmt, ...)					\
	printk_once(KERN_WARNING pr_fmt(fmt), ##__VA_ARGS__)
#define pr_notice_once(fmt, ...)				\
	printk_once(KERN_NOTICE pr_fmt(fmt), ##__VA_ARGS__)
#define pr_info_once(fmt, ...)					\
	printk_once(KERN_INFO pr_fmt(fmt), ##__VA_ARGS__)
#define pr_cont_once(fmt, ...)					\
	printk_once(KERN_CONT pr_fmt(fmt), ##__VA_ARGS__)
#if defined(DEBUG)
#define pr_debug_once(fmt, ...)					\
	printk_once(KERN_DEBUG pr_fmt(fmt), ##__VA_ARGS__)
#else
#define pr_debug_once(fmt, ...)					\
	no_printk(KERN_DEBUG pr_fmt(fmt), ##__VA_ARGS__)
#endif

#define ETH_P_LINK_CTL	0x886c		/* HPNA, wlan link local tunnel */

/**
 * is_unicast_ether_addr - Determine if the Ethernet address is unicast
 * @addr: Pointer to a six-byte array containing the Ethernet address
 *
 * Return true if the address is a unicast address.
 */
#define is_unicast_ether_addr LINUX_BACKPORT(is_unicast_ether_addr)
static inline int is_unicast_ether_addr(const u8 *addr)
{
	return !is_multicast_ether_addr(addr);
}

static inline int netdev_queue_numa_node_read(const struct netdev_queue *q)
{
#if defined(CONFIG_XPS) && defined(CONFIG_NUMA)
    return q->numa_node;
#else
    return NUMA_NO_NODE;
#endif
}

#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,38)) */

#endif /* LINUX_26_38_COMPAT_H */
