#ifndef LINUX_3_3_COMPAT_H
#define LINUX_3_3_COMPAT_H

#include <linux/version.h>

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,3,0))

/* include to override NL80211_FEATURE_SK_TX_STATUS */
#include <linux/nl80211.h>
#include <linux/skbuff.h>
#include <net/sch_generic.h>

#if !((LINUX_VERSION_CODE >= KERNEL_VERSION(3,2,9) && LINUX_VERSION_CODE < KERNEL_VERSION(3,3,0)) || (LINUX_VERSION_CODE >= KERNEL_VERSION(3,0,23) && LINUX_VERSION_CODE < KERNEL_VERSION(3,1,0)))
/* mask qdisc_cb_private_validate as RHEL6.3 backports it */
#define qdisc_cb_private_validate(a, b) compat_qdisc_cb_private_validate(a, b)
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,37))
static inline void qdisc_cb_private_validate(const struct sk_buff *skb, int sz)
{
	BUILD_BUG_ON(sizeof(skb->cb) < sizeof(struct qdisc_skb_cb) + sz);
}
#else
static inline void qdisc_cb_private_validate(const struct sk_buff *skb, int sz)
{
	/* XXX ? */
}
#endif
#endif

#define __pskb_copy LINUX_BACKPORT(__pskb_copy)
extern struct sk_buff *__pskb_copy(struct sk_buff *skb,
				   int headroom, gfp_t gfp_mask);

#define NL80211_FEATURE_SK_TX_STATUS 0

#ifndef CONFIG_COMPAT_NETDEV_FEATURES
typedef u32 netdev_features_t;
#endif /* CONFIG_COMPAT_NETDEV_FEATURES */

#ifndef module_driver
/* source include/linux/device.h */
/**
 * module_driver() - Helper macro for drivers that don't do anything
 * special in module init/exit. This eliminates a lot of boilerplate.
 * Each module may only use this macro once, and calling it replaces
 * module_init() and module_exit().
 *
 * Use this macro to construct bus specific macros for registering
 * drivers, and do not use it on its own.
 */
#define module_driver(__driver, __register, __unregister) \
static int __init __driver##_init(void) \
{ \
	return __register(&(__driver)); \
} \
module_init(__driver##_init); \
static void __exit __driver##_exit(void) \
{ \
	__unregister(&(__driver)); \
} \
module_exit(__driver##_exit);
#endif

/* source include/linux/usb.h */
/**
 * module_usb_driver() - Helper macro for registering a USB driver
 * @__usb_driver: usb_driver struct
 *
 * Helper macro for USB drivers which do not do anything special in module
 * init/exit. This eliminates a lot of boilerplate. Each module may only
 * use this macro once, and calling it replaces module_init() and module_exit()
 */
#define module_usb_driver(__usb_driver) \
	module_driver(__usb_driver, usb_register, \
		       usb_deregister)

#define netdev_tx_sent_queue LINUX_BACKPORT(netdev_tx_sent_queue)
static inline void netdev_tx_sent_queue(struct netdev_queue *dev_queue,
				       unsigned int bytes)
{
}

#define netdev_tx_completed_queue LINUX_BACKPORT(netdev_tx_completed_queue)
static inline void netdev_tx_completed_queue(struct netdev_queue *dev_queue,
					    unsigned pkts, unsigned bytes)
{
}

#define netdev_tx_reset_queue LINUX_BACKPORT(netdev_tx_reset_queue)
static inline void netdev_tx_reset_queue(struct netdev_queue *q)
{
#ifdef CONFIG_BQL
	clear_bit(__QUEUE_STATE_STACK_XOFF, &q->state);
	dql_reset(&q->dql);
#endif
}

#define NETIF_F_LOOPBACK       (1 << 31) /* Enable loopback */

#ifndef NETIF_F_RXCSUM
#define NETIF_F_RXCSUM		(1 << 29)
#endif

#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(3,3,0)) */

#endif /* LINUX_3_3_COMPAT_H */
