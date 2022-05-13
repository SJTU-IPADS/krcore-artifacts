#ifndef LINUX_26_32_COMPAT_H
#define LINUX_26_32_COMPAT_H

#include <linux/version.h>

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,32))

#include <linux/netdevice.h>
#include <linux/compat.h>
#include <net/iw_handler.h>
#include <linux/workqueue.h>
#include <net/genetlink.h>

static inline void flush_delayed_work(struct delayed_work *dwork)
{
	if (del_timer_sync(&dwork->timer)) {
		/*
		 * This is what would happen on 2.6.32 but since we don't have
		 * access to the singlethread_cpu we can't really backport this,
		 * so avoid really *flush*ing the work... Oh well. Any better ideas?

		struct cpu_workqueue_struct *cwq;
		cwq = wq_per_cpu(keventd_wq, get_cpu());
		__queue_work(cwq, &dwork->work);
		put_cpu();

		*/
	}
	flush_work(&dwork->work);
}

/*
 * struct genl_multicast_group was made netns aware through
 * patch "genetlink: make netns aware" by johannes, we just
 * force this to always use the default init_net
 */
#define genl_info_net(x) &init_net
/* Just use init_net for older kernels */
#define get_net_ns_by_pid(x) &init_net

/* net namespace is lost */
#define genlmsg_multicast_netns(a, b, c, d, e)	genlmsg_multicast(b, c, d, e)
#define genlmsg_multicast_allns(a, b, c, d)	genlmsg_multicast(a, b, c, d)
#define genlmsg_unicast(net, skb, pid)	genlmsg_unicast(skb, pid)

#define dev_change_net_namespace(a, b, c) (-EOPNOTSUPP)

#define SET_NETDEV_DEVTYPE(netdev, type)

#ifdef __KERNEL__
/* Driver transmit return codes */
enum netdev_tx {
	BACKPORT_NETDEV_TX_OK = NETDEV_TX_OK,       /* driver took care of packet */
	BACKPORT_NETDEV_TX_BUSY = NETDEV_TX_BUSY,         /* driver tx path was busy*/
	BACKPORT_NETDEV_TX_LOCKED = NETDEV_TX_LOCKED,  /* driver tx lock was already taken */
};
typedef enum netdev_tx netdev_tx_t;
#endif /* __KERNEL__ */


#define lockdep_assert_held(l)			do { } while (0)

/*
 * Similar to the struct tm in userspace <time.h>, but it needs to be here so
 * that the kernel source is self contained.
 */
struct tm {
	/*
	 * the number of seconds after the minute, normally in the range
	 * 0 to 59, but can be up to 60 to allow for leap seconds
	 */
	int tm_sec;
	/* the number of minutes after the hour, in the range 0 to 59*/
	int tm_min;
	/* the number of hours past midnight, in the range 0 to 23 */
	int tm_hour;
	/* the day of the month, in the range 1 to 31 */
	int tm_mday;
	/* the number of months since January, in the range 0 to 11 */
	int tm_mon;
	/* the number of years since 1900 */
	long tm_year;
	/* the number of days since Sunday, in the range 0 to 6 */
	int tm_wday;
	/* the number of days since January 1, in the range 0 to 365 */
	int tm_yday;
};

#define time_to_tm LINUX_BACKPORT(time_to_tm)
void time_to_tm(time_t totalsecs, int offset, struct tm *result);

#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,32)) */

#endif /* LINUX_26_32_COMPAT_H */
