#ifndef LINUX_26_17_COMPAT_H
#define LINUX_26_17_COMPAT_H

#include <linux/version.h>

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,17))
#include <linux/compat-2.6.19.h>
#include <linux/compat-3.15.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <net/sch_generic.h>
#include <linux/rcupdate.h>

#ifndef __packed
#define __packed                     __attribute__((packed))
#endif

#ifndef __percpu
#define __percpu
#endif

#ifndef pgprot_writecombine
#if defined(__i386__) || defined(__x86_64__)
static inline pgprot_t pgprot_writecombine(pgprot_t _prot)
{
	return __pgprot(pgprot_val(_prot) | (_PAGE_PWT));
}
#elif defined(CONFIG_PPC64)
static inline pgprot_t pgprot_writecombine(pgprot_t _prot)
{
	return __pgprot((pgprot_val(_prot) | _PAGE_NO_CACHE) &
				     ~(pgprot_t)_PAGE_GUARDED);
}
#else	/* !(defined(__i386__) || defined(__x86_64__)) */
#define pgprot_writecombine pgprot_noncached
#endif
#endif

#ifndef find_last_bit
/**
 * find_last_bit - find the last set bit in a memory region
 * @addr: The address to start the search at
 * @size: The maximum size to search
 *
 * Returns the bit number of the first set bit, or size.
 */
#define find_last_bit LINUX_BACKPORT(find_last_bit)
extern unsigned long find_last_bit(const unsigned long *addr,
				   unsigned long size);
#endif

/*
 * Getting something that works in C and CPP for an arg that may or may
 * not be defined is tricky.  Here, if we have "#define CONFIG_BOOGER 1"
 * we match on the placeholder define, insert the "0," for arg1 and generate
 * the triplet (0, 1, 0).  Then the last step cherry picks the 2nd arg (a one).
 * When CONFIG_BOOGER is not defined, we generate a (... 1, 0) pair, and when
 * the last step cherry picks the 2nd arg, we get a zero.
 */
#define __ARG_PLACEHOLDER_1 0,
#define config_enabled(cfg) _config_enabled(cfg)
#define _config_enabled(value) __config_enabled(__ARG_PLACEHOLDER_##value)
#define __config_enabled(arg1_or_junk) ___config_enabled(arg1_or_junk 1, 0)
#define ___config_enabled(__ignored, val, ...) val

/*
 * IS_ENABLED(CONFIG_FOO) evaluates to 1 if CONFIG_FOO is set to 'y' or 'm',
 * 0 otherwise.
 *
 */
#define IS_ENABLED(option) \
        (config_enabled(option) || config_enabled(option##_MODULE))

#ifndef pr_warning
#define pr_warning(fmt, ...) \
	printk(KERN_WARNING pr_fmt(fmt), ##__VA_ARGS__)
#endif
#ifndef pr_warn
#define pr_warn pr_warning
#endif
#ifndef pr_err
#define pr_err(fmt, ...) \
	printk(KERN_ERR pr_fmt(fmt), ##__VA_ARGS__)
#endif

#ifndef printk_once
#define printk_once(fmt, ...)                   \
({                                              \
        static bool __print_once;               \
                                                \
        if (!__print_once) {                    \
                __print_once = true;            \
                printk(fmt, ##__VA_ARGS__);     \
        }                                       \
})
#endif

#ifndef pr_info_once
#define pr_info_once(fmt, ...)                                  \
        printk_once(KERN_INFO pr_fmt(fmt), ##__VA_ARGS__)
#endif

#ifndef KERN_CONT
#define KERN_CONT   ""
#endif

#ifndef pr_cont
#define pr_cont(fmt, ...) \
	printk(KERN_CONT fmt, ##__VA_ARGS__)
#endif

/*
 * This looks more complex than it should be. But we need to
 * get the type for the ~ right in round_down (it needs to be
 * as wide as the result!), and we want to evaluate the macro
 * arguments just once each.
 */
#define __round_mask(x, y) ((__typeof__(x))((y)-1))
#define round_up(x, y) ((((x)-1) | __round_mask(x, y))+1)
#define round_down(x, y) ((x) & ~__round_mask(x, y))

static inline void put_pid(struct pid *pid)
{
	return;
}

static inline struct pid *get_task_pid(struct task_struct *task, int type)
{
	/* Not supported */
	return NULL;
}

/* dummy struct */
struct srcu_struct {
};

static inline int init_srcu_struct(struct srcu_struct *srcu)
{
	/* SRCU is not supported on SLES10,and its logic do nothing, added dummy stubs to prevent
	  * the need for multiple ifdefs via backporting.
	*/
	return 0;
}

static inline void cleanup_srcu_struct(struct srcu_struct *sp)
{
	return;
}

static inline int srcu_read_lock(struct srcu_struct *srcu)
{
	return 0;
}

static inline void srcu_read_unlock(struct srcu_struct *srcu, int key)
{
	return;
}

static inline void synchronize_srcu(struct srcu_struct *srcu)
{
	return;
}

#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(2,6,17) */

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,17) || defined(COMPAT_VMWARE))
#ifndef __WARN
#define __WARN(foo) dump_stack()
#endif

#ifndef __WARN_printf
#define __WARN_printf(arg...)   do { printk(arg); __WARN(); } while (0)
#endif

#ifndef WARN
#define WARN(condition, format...) ({					\
	int __ret_warn_on = !!(condition);				\
	if (unlikely(__ret_warn_on))					\
		__WARN_printf(format);					\
	unlikely(__ret_warn_on);					\
})
#endif

#ifndef pr_fmt
#define pr_fmt(fmt) fmt
#endif

#ifndef sysfs_attr_init
#define sysfs_attr_init(attr) do {} while (0)
#endif

#ifndef MAX_IDR_MASK
#define MAX_IDR_SHIFT (sizeof(int)*8 - 1)
#define MAX_IDR_BIT (1U << MAX_IDR_SHIFT)
#define MAX_IDR_MASK (MAX_IDR_BIT - 1)
#endif
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(2,6,17) || defined(COMPAT_VMWARE) */

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,17))

static inline void netif_tx_wake_all_queues(struct net_device *dev)
{
	netif_wake_queue(dev);
}
static inline void netif_tx_start_all_queues(struct net_device *dev)
{
	netif_start_queue(dev);
}
static inline void netif_tx_stop_all_queues(struct net_device *dev)
{
	netif_stop_queue(dev);
}

/* Are all TX queues of the device empty?  */
static inline bool qdisc_all_tx_empty(const struct net_device *dev)
{
	return skb_queue_empty(&dev->qdisc->q);
}

#ifndef max3
#define max3(x, y, z) ({			\
	typeof(x) _max1 = (x);			\
	typeof(y) _max2 = (y);			\
	typeof(z) _max3 = (z);			\
	(void) (&_max1 == &_max2);		\
	(void) (&_max1 == &_max3);		\
	_max1 > _max2 ? (_max1 > _max3 ? _max1 : _max3) : \
		(_max2 > _max3 ? _max2 : _max3); })
#endif

#define NETIF_F_LOOPBACK       (1 << 31) /* Enable loopback */

#ifndef NETIF_F_RXCSUM
#define NETIF_F_RXCSUM		(1 << 29)
#endif

#ifndef rtnl_dereference
#define rtnl_dereference(p)                                     \
        rcu_dereference_protected(p, lockdep_rtnl_is_held())
#endif

#ifndef rcu_dereference_protected
#define rcu_dereference_protected(p, c) \
		rcu_dereference((p))
#endif

#ifndef rcu_dereference_bh
#define rcu_dereference_bh(p) \
		rcu_dereference((p))
#endif

#ifdef CONFIG_PHYS_ADDR_T_64BIT
typedef u64 phys_addr_t;
#else
typedef u32 phys_addr_t;
#endif

#ifndef PCI_VDEVICE
#define PCI_VDEVICE(vendor, device)             \
	PCI_VENDOR_ID_##vendor, (device),       \
	PCI_ANY_ID, PCI_ANY_ID, 0, 0
#endif

#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(2,6,17)*/
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,17) || defined(COMPAT_VMWARE))

#ifndef DEFINE_PCI_DEVICE_TABLE
#define DEFINE_PCI_DEVICE_TABLE(_table) \
	const struct pci_device_id _table[]
#endif

#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(2,6,17) || defined(COMPAT_VMWARE) */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,17))

#ifndef FIELD_SIZEOF
#define FIELD_SIZEOF(t, f) (sizeof(((t*)0)->f))
#endif

/*
 * Backport work for QoS dependencies (kernel/pm_qos_params.c)
 * pm-qos stuff written by mark gross mgross@linux.intel.com.
 *
 * ipw2100 now makes use of:
 *
 * pm_qos_add_requirement(),
 * pm_qos_update_requirement() and
 * pm_qos_remove_requirement() from it
 *
 * mac80211 uses the network latency to determine if to enable or not
 * dynamic PS. mac80211 also and registers a notifier for when
 * the latency changes. Since older kernels do no thave pm-qos stuff
 * we just implement it completley here and register it upon cfg80211
 * init. I haven't tested ipw2100 on 2.6.24 though.
 *
 * This pm-qos implementation is copied verbatim from the kernel
 * written by mark gross mgross@linux.intel.com. You don't have
 * to do anythinig to use pm-qos except use the same exported
 * routines as used in newer kernels. The backport_pm_qos_power_init()
 * defned below is used by the compat module to initialize pm-qos.
 */
int backport_pm_qos_power_init(void);
int backport_pm_qos_power_deinit(void);

int backport_system_workqueue_create(void);
void backport_system_workqueue_destroy(void);

#define FMODE_PATH	((__force fmode_t)0x4000)

#define alloc_workqueue(name, flags, max_active) __create_workqueue(name, max_active)

#define bitmap_set LINUX_BACKPORT(bitmap_set)
extern void bitmap_set(unsigned long *map, int i, int len);
#define bitmap_clear LINUX_BACKPORT(bitmap_clear)
extern void bitmap_clear(unsigned long *map, int start, int nr);
#define bitmap_find_next_zero_area LINUX_BACKPORT(bitmap_find_next_zero_area)
extern unsigned long bitmap_find_next_zero_area(unsigned long *map,
					 unsigned long size,
					 unsigned long start,
					 unsigned int nr,
					 unsigned long align_mask);

#if !defined(CONFIG_COMPAT_IFLA_VF_LINK_STATE_MAX)
enum {
	IFLA_VF_LINK_STATE_AUTO,	/* link state of the uplink */
	IFLA_VF_LINK_STATE_ENABLE,	/* link always up */
	IFLA_VF_LINK_STATE_DISABLE,	/* link always down */
	__IFLA_VF_LINK_STATE_MAX,
};
#endif

#ifndef CONFIG_COMPAT_IS_REINIT_COMPLETION
#define CONFIG_COMPAT_IS_REINIT_COMPLETION
static inline void reinit_completion(struct completion *x)
{
	x->done = 0;
}

#endif

/**
 * list_prev_entry - get the prev element in list
 * @pos:	the type * to cursor
 * @member:	the name of the list_struct within the struct.
 */
#define list_prev_entry(pos, member) \
	list_entry((pos)->member.prev, typeof(*(pos)), member)

/**
 * list_for_each_entry_from - iterate over list of given type from the current point
 * @pos:	the type * to use as a loop cursor.
 * @head:	the head for your list.
 * @member:	the name of the list_struct within the struct.
 *
 * Iterate over list of given type, continuing from current position.
 */
#define list_for_each_entry_from(pos, head, member) 			\
	for (; &pos->member != (head);					\
	     pos = list_next_entry(pos, member))

#define min3(x, y, z) ({			\
	typeof(x) _min1 = (x);			\
	typeof(y) _min2 = (y);			\
	typeof(z) _min3 = (z);			\
	(void) (&_min1 == &_min2);		\
	(void) (&_min1 == &_min3);		\
	_min1 < _min2 ? (_min1 < _min3 ? _min1 : _min3) : \
		(_min2 < _min3 ? _min2 : _min3); })

#define max3(x, y, z) ({			\
	typeof(x) _max1 = (x);			\
	typeof(y) _max2 = (y);			\
	typeof(z) _max3 = (z);			\
	(void) (&_max1 == &_max2);		\
	(void) (&_max1 == &_max3);		\
	_max1 > _max2 ? (_max1 > _max3 ? _max1 : _max3) : \
		(_max2 > _max3 ? _max2 : _max3); })

#ifndef AF_IB
#define AF_IB		27      /* Native InfiniBand address    */
#define PF_IB		AF_IB
#endif /* AF_IB */

struct net {
	atomic_t		count;		/* To decided when the network
						 *  namespace should be freed.
						 */
	atomic_t		use_count;	/* To track references we
						 * destroy on demand
						 */
	struct list_head	list;		/* list of network namespaces */
	struct work_struct	work;		/* work struct for freeing */

	struct proc_dir_entry	*proc_net;
	struct proc_dir_entry	*proc_net_stat;
	struct proc_dir_entry	*proc_net_root;

	struct net_device       *loopback_dev;          /* The loopback */

	struct list_head	dev_base_head;
	struct hlist_head	*dev_name_head;
	struct hlist_head	*dev_index_head;
};

#ifdef CONFIG_NET
/* Init's network namespace */
#define init_net LINUX_BACKPORT(init_net)
extern struct net init_net;
#define INIT_NET_NS(net_ns) .net_ns = &init_net,
#else
#define INIT_NET_NS(net_ns)
#endif

#ifndef ACCESS_ONCE
#define ACCESS_ONCE
#endif

#define netdev_notifier_info_to_dev LINUX_BACKPORT(netdev_notifier_info_to_dev)
static inline struct net_device *
netdev_notifier_info_to_dev(void *ptr)
{
	return (struct net_device *)ptr;
}

#ifndef alloc_ordered_workqueue
#define alloc_ordered_workqueue(name, flags) create_singlethread_workqueue(name)
#endif

static inline __printf(2, 3)
void dev_notice(struct device *dev, const char *fmt, ...)
{}

#ifndef swap
/*
 * 787  * swap - swap value of @a and @b
 * 788  */
#define swap(a, b) \
	do { typeof(a) __tmp = (a); (a) = (b); (b) = __tmp; } while (0)
#endif

#ifndef prandom_u32
#define prandom_u32() random32()
#endif

#define __dev_get_by_index(a, b) __dev_get_by_index((b))

#define __CONFIGFS_ATTR(_name, _mode, _show, _store)                    \
{                                                                       \
        .attr   = {                                                     \
                        .ca_name = __stringify(_name),                  \
                        .ca_mode = _mode,                               \
                        .ca_owner = THIS_MODULE,                        \
        },                                                              \
        .show   = _show,                                                \
        .store  = _store,                                               \
}

#ifndef PTR_ALIGN
#define PTR_ALIGN(p, a)		((typeof(p))ALIGN((unsigned long)(p), (a)))
#endif

static inline int is_kdump_kernel(void)
{
	return 0;
}

/*
 * Net namespace inlines
 */
static inline
struct net *dev_net(const struct net_device *dev)
{
#ifdef CONFIG_NET_NS
	/*
	 * compat-wirelss backport note:
	 * For older kernels we may just need to always return init_net,
	 * not sure when we added dev->nd_net.
	 */
	return dev->nd_net;
#else
	return &init_net;
#endif
}
#ifdef CONFIG_NET_NS
static inline
int net_eq(const struct net *net1, const struct net *net2)
{
	return net1 == net2;
}
#else
static inline
int net_eq(const struct net *net1, const struct net *net2)
{
	return 1;
}
#endif

/* __list_splice as re-implemented on 2.6.27, we backport it */
static inline void __compat_list_splice_new_27(const struct list_head *list,
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

#define mod_delayed_work LINUX_BACKPORT(mod_delayed_work)
bool mod_delayed_work(struct workqueue_struct *wq, struct delayed_work *dwork,
		      unsigned long delay);

/* Backports tty_lock: Localise the lock */
#define tty_lock(__tty) tty_lock()
#define tty_unlock(__tty) tty_unlock()

#define tty_port_register_device(port, driver, index, device) \
	tty_register_device(driver, index, device)

#define ULLONG_MAX      (~0ULL)
#define EPROBE_DEFER    517     /* Driver requests probe retry */

static inline u64 div_u64_rem(u64 dividend, u32 divisor, u32 *remainder)
{
	*remainder = dividend % divisor;
	return dividend / divisor;
}

static inline u64 div_u64(u64 dividend, u32 divisor)
{
	u32 remainder;
	return div_u64_rem(dividend, divisor, &remainder);
}

/* 
 * kstrto* was included in kernel 2.6.38.4 and causes conflicts with the
 * version included in compat-wireless. We use strict_strtol to check if
 * kstrto* is already available.
 */

/* Internal, do not use. */
#define _kstrtoul LINUX_BACKPORT(_kstrtoul)
int __must_check _kstrtoul(const char *s, unsigned int base, unsigned long *res);
#define _kstrtol LINUX_BACKPORT(_kstrtol)
int __must_check _kstrtol(const char *s, unsigned int base, long *res);

#define kstrtoull LINUX_BACKPORT(kstrtoull)
int __must_check kstrtoull(const char *s, unsigned int base, unsigned long long *res);
#define kstrtoll LINUX_BACKPORT(kstrtoll)
int __must_check kstrtoll(const char *s, unsigned int base, long long *res);
static inline int __must_check kstrtoul(const char *s, unsigned int base, unsigned long *res)
{
	/*
	 * We want to shortcut function call, but
	 * __builtin_types_compatible_p(unsigned long, unsigned long long) = 0.
	 */
	if (sizeof(unsigned long) == sizeof(unsigned long long) &&
	    __alignof__(unsigned long) == __alignof__(unsigned long long))
		return kstrtoull(s, base, (unsigned long long *)res);
	else
		return _kstrtoul(s, base, res);
}

static inline int __must_check kstrtol(const char *s, unsigned int base, long *res)
{
	/*
	 * We want to shortcut function call, but
	 * __builtin_types_compatible_p(long, long long) = 0.
	 */
	if (sizeof(long) == sizeof(long long) &&
	    __alignof__(long) == __alignof__(long long))
		return kstrtoll(s, base, (long long *)res);
	else
		return _kstrtol(s, base, res);
}

#define kstrtouint LINUX_BACKPORT(kstrtouint)
int __must_check kstrtouint(const char *s, unsigned int base, unsigned int *res);
#define kstrtoint LINUX_BACKPORT(kstrtoint)
int __must_check kstrtoint(const char *s, unsigned int base, int *res);

static inline int __must_check kstrtou64(const char *s, unsigned int base, u64 *res)
{
	return kstrtoull(s, base, res);
}

static inline int __must_check kstrtos64(const char *s, unsigned int base, s64 *res)
{
	return kstrtoll(s, base, res);
}

static inline int __must_check kstrtou32(const char *s, unsigned int base, u32 *res)
{
	return kstrtouint(s, base, res);
}

static inline int __must_check kstrtos32(const char *s, unsigned int base, s32 *res)
{
	return kstrtoint(s, base, res);
}

#define kstrtou16 LINUX_BACKPORT(kstrtou16)
int __must_check kstrtou16(const char *s, unsigned int base, u16 *res);
#define kstrtos16 LINUX_BACKPORT(kstrtos16)
int __must_check kstrtos16(const char *s, unsigned int base, s16 *res);
#define kstrtou8 LINUX_BACKPORT(kstrtou8)
int __must_check kstrtou8(const char *s, unsigned int base, u8 *res);
#define kstrtos8 LINUX_BACKPORT(kstrtos8)
int __must_check kstrtos8(const char *s, unsigned int base, s8 *res);


#define INIT_DEFERRABLE_WORK(_work, _func) INIT_DELAYED_WORK_DEFERRABLE(_work, _func)

#define pr_devel(fmt, ...) \
	printk(KERN_DEBUG pr_fmt(fmt), ##__VA_ARGS__)

typedef	int (*notifier_fn_t)(struct notifier_block *nb,
			unsigned long action, void *data);

struct raw_notifier_head {
	struct notifier_block *head;
};

#define RAW_INIT_NOTIFIER_HEAD(name) do {	\
		(name)->head = NULL;		\
	} while (0)

#define RAW_NOTIFIER_INIT(name)	{				\
		.head = NULL }

#define RAW_NOTIFIER_HEAD(name)					\
	struct raw_notifier_head name =				\
		RAW_NOTIFIER_INIT(name)

#define raw_notifier_chain_register LINUX_BACKPORT(raw_notifier_chain_register)
extern int raw_notifier_chain_register(struct raw_notifier_head *nh,
		struct notifier_block *nb);
#define raw_notifier_chain_unregister LINUX_BACKPORT(raw_notifier_chain_unregister)
extern int raw_notifier_chain_unregister(struct raw_notifier_head *nh,
		struct notifier_block *nb);
#define raw_notifier_call_chain LINUX_BACKPORT(raw_notifier_call_chain)
extern int raw_notifier_call_chain(struct raw_notifier_head *nh,
		unsigned long val, void *v);
#define __raw_notifier_call_chain LINUX_BACKPORT(__raw_notifier_call_chain)
extern int __raw_notifier_call_chain(struct raw_notifier_head *nh,
	unsigned long val, void *v, int nr_to_call, int *nr_calls);

#ifndef CONFIG_COMPAT_IS_IP_TOS2PRIO
#include <net/route.h>
#define ip_tos2prio LINUX_BACKPORT(ip_tos2prio)
extern const __u8 ip_tos2prio[16];
#define rt_tos2priority LINUX_BACKPORT(rt_tos2priority)
static inline char rt_tos2priority(u8 tos)
{
	return ip_tos2prio[IPTOS_TOS(tos)>>1];
}
#endif /* CONFIG_COMPAT_IS_IP_TOS2PRIO */

#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,17)) */

#endif /* LINUX_26_17_COMPAT_H */
