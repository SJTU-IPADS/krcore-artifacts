#ifndef LINUX_3_5_COMPAT_H
#define LINUX_3_5_COMPAT_H

#include <linux/version.h>

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,5,0))

#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/etherdevice.h>
#include <linux/net.h>

#include <linux/pkt_sched.h>

/*
 * This backports:
 *
 *   From 76e3cc126bb223013a6b9a0e2a51238d1ef2e409 Mon Sep 17 00:00:00 2001
 *   From: Eric Dumazet <edumazet@google.com>
 *   Date: Thu, 10 May 2012 07:51:25 +0000
 *   Subject: [PATCH] codel: Controlled Delay AQM
 */

/* CODEL */

#if !defined(CONFIG_TCA_CODEL_UNSPEC)
enum {
	TCA_CODEL_UNSPEC,
	TCA_CODEL_TARGET,
	TCA_CODEL_LIMIT,
	TCA_CODEL_INTERVAL,
	TCA_CODEL_ECN,
	__TCA_CODEL_MAX
};
#endif

#define TCA_CODEL_MAX	(__TCA_CODEL_MAX - 1)

#if !defined(CONFIG_TC_CODEL_XSTATS)
struct tc_codel_xstats {
	__u32	maxpacket; /* largest packet we've seen so far */
	__u32	count;	   /* how many drops we've done since the last time we
			    * entered dropping state
			    */
	__u32	lastcount; /* count at entry to dropping state */
	__u32	ldelay;    /* in-queue delay seen by most recently dequeued packet */
	__s32	drop_next; /* time to drop next packet */
	__u32	drop_overlimit; /* number of time max qdisc packet limit was hit */
	__u32	ecn_mark;  /* number of packets we ECN marked instead of dropped */
	__u32	dropping;  /* are we in dropping state ? */
};
#endif

/* This backports:
 *
 * commit 4b549a2ef4bef9965d97cbd992ba67930cd3e0fe
 * Author: Eric Dumazet <edumazet@google.com>
 * Date:   Fri May 11 09:30:50 2012 +0000
 *    fq_codel: Fair Queue Codel AQM
 */

/* FQ_CODEL */

#if !defined(CONFIG_TCA_FQ_CODEL_UNSPEC)
enum {
	TCA_FQ_CODEL_UNSPEC,
	TCA_FQ_CODEL_TARGET,
	TCA_FQ_CODEL_LIMIT,
	TCA_FQ_CODEL_INTERVAL,
	TCA_FQ_CODEL_ECN,
	TCA_FQ_CODEL_FLOWS,
	TCA_FQ_CODEL_QUANTUM,
	__TCA_FQ_CODEL_MAX
};
#endif

#define TCA_FQ_CODEL_MAX	(__TCA_FQ_CODEL_MAX - 1)

#ifndef CONFIG_TCA_FQ_CODEL_XSTATS_QDISC
enum {
	TCA_FQ_CODEL_XSTATS_QDISC,
	TCA_FQ_CODEL_XSTATS_CLASS,
};
#endif

#if !defined(CONFIG_TC_FQ_CODEL_QD_STATS)
struct tc_fq_codel_qd_stats {
	__u32	maxpacket;	/* largest packet we've seen so far */
	__u32	drop_overlimit; /* number of time max qdisc
				 * packet limit was hit
				 */
	__u32	ecn_mark;	/* number of packets we ECN marked
				 * instead of being dropped
				 */
	__u32	new_flow_count; /* number of time packets
				 * created a 'new flow'
				 */
	__u32	new_flows_len;	/* count of flows in new list */
	__u32	old_flows_len;	/* count of flows in old list */
};
#endif

#if !defined(CONFIG_TC_FQ_CODEL_CL_STATS)
struct tc_fq_codel_cl_stats {
	__s32	deficit;
	__u32	ldelay;		/* in-queue delay seen by most recently
				 * dequeued packet
				 */
	__u32	count;
	__u32	lastcount;
	__u32	dropping;
	__s32	drop_next;
};
#endif

#if !defined(CONFIG_TC_FQ_CODEL_XSTATS)
struct tc_fq_codel_xstats {
	__u32	type;
	union {
		struct tc_fq_codel_qd_stats qdisc_stats;
		struct tc_fq_codel_cl_stats class_stats;
	};
};
#endif

#ifndef HAVE_IEEE_GET_SET_MAXRATE
#ifndef IEEE_8021QAZ_MAX_TCS
#define IEEE_8021QAZ_MAX_TCS 8
#endif

struct ieee_maxrate {
	u64 tc_maxrate[IEEE_8021QAZ_MAX_TCS];
};
#endif

/* Backports tty_lock: Localise the lock */
#define tty_lock(__tty) tty_lock()
#define tty_unlock(__tty) tty_unlock()

#define net_ratelimited_function(function, ...)			\
do {								\
	if (net_ratelimit())					\
		function(__VA_ARGS__);				\
} while (0)

#define net_emerg_ratelimited(fmt, ...)				\
	net_ratelimited_function(pr_emerg, fmt, ##__VA_ARGS__)
#define net_alert_ratelimited(fmt, ...)				\
	net_ratelimited_function(pr_alert, fmt, ##__VA_ARGS__)
#define net_crit_ratelimited(fmt, ...)				\
	net_ratelimited_function(pr_crit, fmt, ##__VA_ARGS__)
#define net_err_ratelimited(fmt, ...)				\
	net_ratelimited_function(pr_err, fmt, ##__VA_ARGS__)
#define net_notice_ratelimited(fmt, ...)			\
	net_ratelimited_function(pr_notice, fmt, ##__VA_ARGS__)
#define net_warn_ratelimited(fmt, ...)				\
	net_ratelimited_function(pr_warn, fmt, ##__VA_ARGS__)
#define net_info_ratelimited(fmt, ...)				\
	net_ratelimited_function(pr_info, fmt, ##__VA_ARGS__)
#define net_dbg_ratelimited(fmt, ...)				\
	net_ratelimited_function(pr_debug, fmt, ##__VA_ARGS__)

static inline int net_sysctl_init(void) { return 0; }
static inline struct ctl_table_header *register_net_sysctl(struct net *net,
	const char *path, struct ctl_table *table)
{
	return NULL;
}

#ifndef CONFIG_COMPAT_IS_IP_TOS2PRIO
#define ip_tos2prio LINUX_BACKPORT(ip_tos2prio)
extern const __u8 ip_tos2prio[16];
#endif

#define dev_uc_add_excl LINUX_BACKPORT(dev_uc_add_excl)
#ifdef CONFIG_COMPAT_DEV_UC_MC_ADD_CONST
extern int dev_uc_add_excl(struct net_device *dev, const unsigned char *addr);
#else
extern int dev_uc_add_excl(struct net_device *dev, unsigned char *addr);
#endif

#define dev_mc_add_excl LINUX_BACKPORT(dev_mc_add_excl)
#ifdef CONFIG_COMPAT_DEV_UC_MC_ADD_CONST
extern int dev_mc_add_excl(struct net_device *dev, const unsigned char *addr);
#else
extern int dev_mc_add_excl(struct net_device *dev, unsigned char *addr);
#endif

#define SK_CAN_REUSE 1

#define ether_addr_equal_64bits LINUX_BACKPORT(ether_addr_equal_64bits)
static inline bool ether_addr_equal_64bits(const u8 addr1[6+2],
					   const u8 addr2[6+2])
{
	return !compare_ether_addr_64bits(addr1, addr2);
}

/**
 * ether_addr_equal - Compare two Ethernet addresses
 * @addr1: Pointer to a six-byte array containing the Ethernet address
 * @addr2: Pointer other six-byte array containing the Ethernet address
 *
 * Compare two ethernet addresses, returns true if equal
 */
#define ether_addr_equal LINUX_BACKPORT(ether_addr_equal)
static inline bool ether_addr_equal(const u8 *addr1, const u8 *addr2)
{
	return !compare_ether_addr(addr1, addr2);
}

#include <linux/skbuff.h>

#define skb_end_offset LINUX_BACKPORT(skb_end_offset)
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
#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(3,5,0)) */

#endif /* LINUX_3_5_COMPAT_H */
