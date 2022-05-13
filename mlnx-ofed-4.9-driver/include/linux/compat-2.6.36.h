#ifndef LINUX_26_36_COMPAT_H
#define LINUX_26_36_COMPAT_H

#include <linux/version.h>

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,36))

#include <linux/smp_lock.h>
#include <linux/skbuff.h>

#ifndef kparam_block_sysfs_write
#define kparam_block_sysfs_write(a)
#endif
#ifndef kparam_unblock_sysfs_write
#define kparam_unblock_sysfs_write(a)
#endif

#define device_rename(dev, new_name) device_rename(dev, (char *)new_name)

#ifdef CONFIG_COMPAT_NO_PRINTK_NEEDED
/*
 * Dummy printk for disabled debugging statements to use whilst maintaining
 * gcc's format and side-effect checking.
 */
static inline __attribute__ ((format (printf, 1, 2)))
int no_printk(const char *s, ...) { return 0; }
#endif /* CONFIG_COMPAT_NO_PRINTK_NEEDED */

#ifndef alloc_workqueue
#define alloc_workqueue(name, flags, max_active) __create_workqueue(name, flags, max_active, 0)
#endif

#define EXTPROC	0200000
#define TIOCPKT_IOCTL		64

static inline void tty_lock(void) __acquires(kernel_lock)
{
#ifdef CONFIG_LOCK_KERNEL
	/* kernel_locked is 1 for !CONFIG_LOCK_KERNEL */
	WARN_ON(kernel_locked());
#endif
	lock_kernel();
}
static inline void tty_unlock(void) __releases(kernel_lock)
{
	unlock_kernel();
}
#define tty_locked()           (kernel_locked())

#define usleep_range(_min, _max)	msleep((_max) / 1000)

#define __rcu

static inline void pm_wakeup_event(struct device *dev, unsigned int msec) {}

static inline bool skb_defer_rx_timestamp(struct sk_buff *skb)
{
	return false;
}

#define skb_tx_timestamp LINUX_BACKPORT(skb_tx_timestamp)
static inline void skb_tx_timestamp(struct sk_buff *skb)
{
}

/*
 * System-wide workqueues which are always present.
 *
 * system_wq is the one used by schedule[_delayed]_work[_on]().
 * Multi-CPU multi-threaded.  There are users which expect relatively
 * short queue flush time.  Don't queue works which can run for too
 * long.
 *
 * system_long_wq is similar to system_wq but may host long running
 * works.  Queue flushing might take relatively long.
 *
 * system_nrt_wq is non-reentrant and guarantees that any given work
 * item is never executed in parallel by multiple CPUs.  Queue
 * flushing might take relatively long.
 */
#define system_wq LINUX_BACKPORT(system_wq)
extern struct workqueue_struct *system_wq;
#define system_long_wq LINUX_BACKPORT(system_long_wq)
extern struct workqueue_struct *system_long_wq;
#define system_nrt_wq LINUX_BACKPORT(system_nrt_wq)
extern struct workqueue_struct *system_nrt_wq;

int backport_system_workqueue_create(void);
void backport_system_workqueue_destroy(void);

#define schedule_work LINUX_BACKPORT(schedule_work)
int schedule_work(struct work_struct *work);
#define schedule_work_on LINUX_BACKPORT(schedule_work_on)
int schedule_work_on(int cpu, struct work_struct *work);
#define schedule_delayed_work LINUX_BACKPORT(schedule_delayed_work)
int schedule_delayed_work(struct delayed_work *dwork,
			  unsigned long delay);
#define schedule_delayed_work_on LINUX_BACKPORT(schedule_delayed_work_on)
int schedule_delayed_work_on(int cpu,
			     struct delayed_work *dwork,
			     unsigned long delay);
#define flush_scheduled_work LINUX_BACKPORT(flush_scheduled_work)
void flush_scheduled_work(void);

#ifndef CONFIG_COMPAT_IS_WORK_BUSY
enum {
	/* bit mask for work_busy() return values */
	WORK_BUSY_PENDING       = 1 << 0,
	WORK_BUSY_RUNNING       = 1 << 1,
};
#endif

#define work_busy LINUX_BACKPORT(work_busy)
extern unsigned int work_busy(struct work_struct *work);

#define br_port_exists(dev)	(dev->br_port)

#else

static inline int backport_system_workqueue_create(void)
{
	return 0;
}

static inline void backport_system_workqueue_destroy(void)
{
}

/*
 * This is not part of The 2.6.37 kernel yet but we
 * we use it to optimize the backport code we
 * need to implement. Instead of using ifdefs
 * to check what version of the check we use
 * we just replace all checks on current code
 * with this. I'll submit this upstream too, that
 * way all we'd have to do is to implement this
 * for older kernels, then we would not have to
 * edit the upstrema code for backport efforts.
 */
#define br_port_exists(dev)	(dev->priv_flags & IFF_BRIDGE_PORT)

#ifndef __rcu
#define __rcu
#endif

#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,36)) */

#endif /* LINUX_26_36_COMPAT_H */
