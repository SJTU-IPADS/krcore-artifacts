#ifndef LINUX_26_28_COMPAT_H
#define LINUX_26_28_COMPAT_H

#include <linux/version.h>

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,28))

#include <linux/skbuff.h>
#include <linux/if_ether.h>
#include <linux/usb.h>
#include <linux/types.h>
#include <linux/types.h>
#include <linux/cpumask.h>

#ifndef ETH_P_PAE
#define ETH_P_PAE 0x888E      /* Port Access Entity (IEEE 802.1X) */
#endif

#include <linux/pci.h>

typedef struct cpumask { DECLARE_BITMAP(bits, NR_CPUS); } compat_cpumask_t;

#if defined(CONFIG_X86) || defined(CONFIG_X86_64) || defined(CONFIG_PPC)
/*
 * CONFIG_PHYS_ADDR_T_64BIT was added as new to all architectures
 * as of 2.6.28 but x86 and ppc had it already. x86 only got phys_addr_t
 * as of 2.6.25 but then is backported in compat-2.6.25.h
 */
#else
#if defined(CONFIG_64BIT) || defined(CONFIG_X86_PAE) || defined(CONFIG_PPC64) || defined(CONFIG_PHYS_64BIT)
#define CONFIG_PHYS_ADDR_T_64BIT 1
typedef u64 phys_addr_t;
#else
typedef u32 phys_addr_t;
#endif

#endif /* non x86 and ppc */

#ifndef WARN_ONCE
#define WARN_ONCE(condition, format...) ({                      \
	static int __warned;                                    \
	int __ret_warn_once = !!(condition);                    \
								\
	if (unlikely(__ret_warn_once))                          \
		if (WARN(!__warned, format))                    \
			__warned = 1;                           \
	unlikely(__ret_warn_once);                              \
})
#endif /* From include/asm-generic/bug.h */

#define pci_ioremap_bar LINUX_BACKPORT(pci_ioremap_bar)
void __iomem *pci_ioremap_bar(struct pci_dev *pdev, int bar);

/**
 *	skb_queue_is_last - check if skb is the last entry in the queue
 *	@list: queue head
 *	@skb: buffer
 *
 *	Returns true if @skb is the last buffer on the list.
 */
static inline bool skb_queue_is_last(const struct sk_buff_head *list,
				     const struct sk_buff *skb)
{
	return (skb->next == (struct sk_buff *) list);
}

/**
 *	skb_queue_next - return the next packet in the queue
 *	@list: queue head
 *	@skb: current buffer
 *
 *	Return the next packet in @list after @skb.  It is only valid to
 *	call this if skb_queue_is_last() evaluates to false.
 */
static inline struct sk_buff *skb_queue_next(const struct sk_buff_head *list,
                                             const struct sk_buff *skb)
{
	/* This BUG_ON may seem severe, but if we just return then we
	 * are going to dereference garbage.
	 */
	BUG_ON(skb_queue_is_last(list, skb));
	return skb->next;
}

/**
 *	__skb_queue_head_init - initialize non-spinlock portions of sk_buff_head
 *	@list: queue to initialize
 *
 *	This initializes only the list and queue length aspects of
 *	an sk_buff_head object.  This allows to initialize the list
 *	aspects of an sk_buff_head without reinitializing things like
 *	the spinlock.  It can also be used for on-stack sk_buff_head
 *	objects where the spinlock is known to not be used.
 */
static inline void __skb_queue_head_init(struct sk_buff_head *list)
{
	list->prev = list->next = (struct sk_buff *)list;
	list->qlen = 0;
}

static inline void __skb_queue_splice(const struct sk_buff_head *list,
				      struct sk_buff *prev,
				      struct sk_buff *next)
{
	struct sk_buff *first = list->next;
	struct sk_buff *last = list->prev;

	first->prev = prev;
	prev->next = first;

	last->next = next;
	next->prev = last;
}

/**
 *	skb_queue_splice - join two skb lists, this is designed for stacks
 *	@list: the new list to add
 *	@head: the place to add it in the first list
 */
static inline void skb_queue_splice(const struct sk_buff_head *list,
				    struct sk_buff_head *head)
{
	if (!skb_queue_empty(list)) {
		__skb_queue_splice(list, (struct sk_buff *) head, head->next);
		head->qlen += list->qlen;
	}
}

/**
 *	skb_queue_splice - join two skb lists and reinitialise the emptied list
 *	@list: the new list to add
 *	@head: the place to add it in the first list
 *
 *	The list at @list is reinitialised
 */
static inline void skb_queue_splice_init(struct sk_buff_head *list,
					 struct sk_buff_head *head)
{
	if (!skb_queue_empty(list)) {
		__skb_queue_splice(list, (struct sk_buff *) head, head->next);
		head->qlen += list->qlen;
		__skb_queue_head_init(list);
	}
}

/**
 *	skb_queue_splice_tail - join two skb lists and reinitialise the emptied list
 *	@list: the new list to add
 *	@head: the place to add it in the first list
 *
 *	Each of the lists is a queue.
 *	The list at @list is reinitialised
 */
static inline void skb_queue_splice_tail_init(struct sk_buff_head *list,
					      struct sk_buff_head *head)
{
	if (!skb_queue_empty(list)) {
		__skb_queue_splice(list, head->prev, (struct sk_buff *) head);
		head->qlen += list->qlen;
		__skb_queue_head_init(list);
	}
} /* From include/linux/skbuff.h */

/**
 *	skb_queue_splice_tail - join two skb lists, each list being a queue
 *	@list: the new list to add
 *	@head: the place to add it in the first list
 */
static inline void skb_queue_splice_tail(const struct sk_buff_head *list,
					 struct sk_buff_head *head)
{
	if (!skb_queue_empty(list)) {
		__skb_queue_splice(list, head->prev, (struct sk_buff *) head);
		head->qlen += list->qlen;
	}
}

#define skb_queue_walk_from(queue, skb)						\
		for (; skb != (struct sk_buff *)(queue);			\
		     skb = skb->next)

#ifndef DECLARE_TRACE

#define TP_PROTO(args...)	args
#define TP_ARGS(args...)		args

#define DECLARE_TRACE(name, proto, args)				\
	static inline void _do_trace_##name(struct tracepoint *tp, proto) \
	{ }								\
	static inline void trace_##name(proto)				\
	{ }								\
	static inline int register_trace_##name(void (*probe)(proto))	\
	{								\
		return -ENOSYS;						\
	}								\
	static inline int unregister_trace_##name(void (*probe)(proto))	\
	{								\
		return -ENOSYS;						\
	}

#define EXPORT_TRACEPOINT_SYMBOL_GPL(name)
#define EXPORT_TRACEPOINT_SYMBOL(name)


#endif

#define round_jiffies_up LINUX_BACKPORT(round_jiffies_up)
unsigned long round_jiffies_up(unsigned long j);

#define wake_up_interruptible_poll(x, m)			\
	__wake_up(x, TASK_INTERRUPTIBLE, 1, (void *) (m))

#define n_tty_ioctl_helper LINUX_BACKPORT(n_tty_ioctl_helper)
extern int n_tty_ioctl_helper(struct tty_struct *tty, struct file *file,
		       unsigned int cmd, unsigned long arg);

#define pci_wake_from_d3 LINUX_BACKPORT(pci_wake_from_d3)
int pci_wake_from_d3(struct pci_dev *dev, bool enable);

#define alloc_workqueue(name, flags, max_active) __create_workqueue(name, flags, max_active)

#ifndef pr_fmt
#define pr_fmt(fmt) fmt
#endif

#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,28)) */

#endif /* LINUX_26_28_COMPAT_H */
