#ifndef LINUX_26_33_COMPAT_H
#define LINUX_26_33_COMPAT_H

#include <linux/version.h>

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,33))

#include <linux/skbuff.h>
#include <linux/pci.h>
#if defined(CONFIG_PCCARD) || defined(CONFIG_PCCARD_MODULE)
#include <pcmcia/cs_types.h>
#include <pcmcia/cistpl.h>
#include <pcmcia/ds.h>
#endif
#include <linux/firmware.h>
#include <linux/input.h>
#include <linux/sched.h>

#if defined(CONFIG_COMPAT_FIRMWARE_CLASS)
#define request_firmware_nowait LINUX_BACKPORT(request_firmware_nowait)
#define request_firmware LINUX_BACKPORT(request_firmware)
#define release_firmware LINUX_BACKPORT(release_firmware)

#if defined(CONFIG_FW_LOADER) || defined(CONFIG_FW_LOADER_MODULE)
int request_firmware(const struct firmware **fw, const char *name,
		     struct device *device);
int request_firmware_nowait(
	struct module *module, int uevent,
	const char *name, struct device *device, gfp_t gfp, void *context,
	void (*cont)(const struct firmware *fw, void *context));

void release_firmware(const struct firmware *fw);
#else
static inline int request_firmware(const struct firmware **fw,
				   const char *name,
				   struct device *device)
{
	return -EINVAL;
}
static inline int request_firmware_nowait(
	struct module *module, int uevent,
	const char *name, struct device *device, gfp_t gfp, void *context,
	void (*cont)(const struct firmware *fw, void *context))
{
	return -EINVAL;
}

static inline void release_firmware(const struct firmware *fw)
{
}
#endif
#endif

/* mask KEY_RFKILL as RHEL6 backports this */
#if !defined(KEY_RFKILL)
#define KEY_RFKILL		247	/* Key that controls all radios */
#endif
/* source: include/linux/if.h */

/* this will never happen on older kernels */
#ifndef NETDEV_POST_INIT
#define NETDEV_POST_INIT 0xffff
#endif /* NETDEV_POST_INIT */

/* mask netdev_alloc_skb_ip_align as debian squeeze also backports this */
#define netdev_alloc_skb_ip_align(a, b) compat_netdev_alloc_skb_ip_align(a, b)

static inline struct sk_buff *netdev_alloc_skb_ip_align(struct net_device *dev,
                unsigned int length)
{
	struct sk_buff *skb = netdev_alloc_skb(dev, length + NET_IP_ALIGN);

	if (NET_IP_ALIGN && skb)
		skb_reserve(skb, NET_IP_ALIGN);
	return skb;
}

/**
 * list_for_each_entry_continue_rcu - continue iteration over list of given type
 * @pos:	the type * to use as a loop cursor.
 * @head:	the head for your list.
 * @member:	the name of the list_struct within the struct.
 *
 * Continue to iterate over list of given type, continuing after
 * the current position.
 */
#define list_for_each_entry_continue_rcu(pos, head, member) 		\
	for (pos = list_entry_rcu(pos->member.next, typeof(*pos), member); \
	     prefetch(pos->member.next), &pos->member != (head);	\
	     pos = list_entry_rcu(pos->member.next, typeof(*pos), member))

#define sock_recv_ts_and_drops(msg, sk, skb) sock_recv_timestamp(msg, sk, skb)

#define pci_pcie_cap LINUX_BACKPORT(pci_pcie_cap)

/**
 * pci_pcie_cap - get the saved PCIe capability offset
 * @dev: PCI device
 *
 * PCIe capability offset is calculated at PCI device initialization
 * time and saved in the data structure. This function returns saved
 * PCIe capability offset. Using this instead of pci_find_capability()
 * reduces unnecessary search in the PCI configuration space. If you
 * need to calculate PCIe capability offset from raw device for some
 * reasons, please use pci_find_capability() instead.
 */
static inline int pci_pcie_cap(struct pci_dev *dev)
{
	return pci_find_capability(dev, PCI_CAP_ID_EXP);
}

#define pci_is_pcie LINUX_BACKPORT(pci_is_pcie)

/**
 * pci_is_pcie - check if the PCI device is PCI Express capable
 * @dev: PCI device
 *
 * Retrun true if the PCI device is PCI Express capable, false otherwise.
 */
static inline bool pci_is_pcie(struct pci_dev *dev)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24))
	return dev->is_pcie;
#else
	return !!pci_pcie_cap(dev);
#endif
}

#ifdef __GNUC__
#define __always_unused			__attribute__((unused))
#else
#define __always_unused			/* unimplemented */
#endif

/* mask IS_ERR_OR_NULL as debian squeeze also backports this */
#define IS_ERR_OR_NULL(a) compat_IS_ERR_OR_NULL(a)

static inline long __must_check IS_ERR_OR_NULL(const void *ptr)
{
	return !ptr || IS_ERR_VALUE((unsigned long)ptr);
}

#define for_each_netdev_rcu(net, d)             \
                list_for_each_entry_rcu(d, &(net)->dev_base_head, dev_list)

#define IPV4_FLOW               0x10    /* hash only */
#define IPV6_FLOW               0x11    /* hash only */

#define tsk_cpus_allowed(tsk) (&(tsk)->cpus_allowed)

#ifndef CONFIG_COMPAT_IS_BITMAP

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

#endif /* CONFIG_COMPAT_IS_BITMAP */

#ifdef CONFIG_PPC
#ifndef NUMA_NO_NODE
#define	NUMA_NO_NODE	(-1)
#endif
#endif /* CONFIG_PPC */

#define strim LINUX_BACKPORT(strim)
extern char *strim(char *);

#define skip_spaces LINUX_BACKPORT(skip_spaces)
extern char * __must_check skip_spaces(const char *);

#ifndef PORT_DA
#define PORT_DA 0x05
#endif

#ifndef PORT_NONE
#define PORT_NONE 0xef
#endif

#ifndef VLAN_PRIO_SHIFT
#define VLAN_PRIO_SHIFT		13
#endif /* VLAN_PRIO_SHIFT */

#ifndef __sockaddr_check_size
#define __sockaddr_check_size(size)    \
	BUILD_BUG_ON(((size) > sizeof(struct __kernel_sockaddr_storage)))
#endif

#ifndef DECLARE_SOCKADDR
#define DECLARE_SOCKADDR(type, dst, src)       \
	type dst = ({ __sockaddr_check_size(sizeof(*dst)); (type) src; })
#endif

#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,33)) */

#endif /* LINUX_26_33_COMPAT_H */
