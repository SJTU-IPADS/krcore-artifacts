#ifndef LINUX_26_34_COMPAT_H
#define LINUX_26_34_COMPAT_H

#include <linux/version.h>

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,34))

#include <linux/netdevice.h>
#include <linux/usb.h>
#include <linux/bitops.h>

#ifndef for_each_set_bit
#define for_each_set_bit(bit, addr, size) \
	for ((bit) = find_first_bit((addr), (size));		\
	     (bit) < (size);					\
	     (bit) = find_next_bit((addr), (size), (bit) + 1))
#endif

#define netdev_uc_count(dev) ((dev)->uc.count)
#define netdev_uc_empty(dev) ((dev)->uc.count == 0)
#define netdev_for_each_uc_addr(ha, dev) \
        list_for_each_entry(ha, &dev->uc.list, list)

#define netdev_mc_count(dev) ((dev)->mc_count)
#define netdev_mc_empty(dev) (netdev_mc_count(dev) == 0)

#define netdev_for_each_mc_addr(mclist, dev) \
	for (mclist = dev->mc_list; mclist; mclist = mclist->next)
/* source: include/linux/netdevice.h */


/* Logging, debugging and troubleshooting/diagnostic helpers. */

/* netdev_printk helpers, similar to dev_printk */

#ifndef netdev_name
#define netdev_name(__dev) \
	((__dev->reg_state != NETREG_REGISTERED) ? \
		"(unregistered net_device)" : __dev->name)
#endif

#define netdev_printk(level, netdev, format, args...)		\
	dev_printk(level, (netdev)->dev.parent,			\
		   "%s: " format,				\
		   netdev_name(netdev), ##args)

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
/* source: include/linux/netdevice.h */


#define device_lock LINUX_BACKPORT(device_lock)
static inline void device_lock(struct device *dev)
{
#if defined(CONFIG_PREEMPT_RT) || defined(CONFIG_PREEMPT_DESKTOP)
        mutex_lock(&dev->mutex);
#else
	down(&dev->sem);
#endif
}

#define device_trylock LINUX_BACKPORT(device_trylock)
static inline int device_trylock(struct device *dev)
{
#if defined(CONFIG_PREEMPT_RT) || defined(CONFIG_PREEMPT_DESKTOP)
	return mutex_trylock(&dev->mutex);
#else
	return down_trylock(&dev->sem);
#endif
}

#define device_unlock LINUX_BACKPORT(device_unlock)
static inline void device_unlock(struct device *dev)
{
#if defined(CONFIG_PREEMPT_RT) || defined(CONFIG_PREEMPT_DESKTOP)
        mutex_unlock(&dev->mutex);
#else
	up(&dev->sem);
#endif
}

#if defined(CONFIG_PCMCIA) || defined(CONFIG_PCMCIA_MODULE)
#define PCMCIA_DEVICE_PROD_ID3(v3, vh3) { \
	.match_flags = PCMCIA_DEV_ID_MATCH_PROD_ID3, \
	.prod_id = { NULL, NULL, (v3), NULL },  \
	.prod_id_hash = { 0, 0, (vh3), 0 }, }
#endif

#define rcu_dereference_check(p, c) rcu_dereference(p)

#ifndef sysfs_attr_init
/**
 *	sysfs_attr_init - initialize a dynamically allocated sysfs attribute
 *	@attr: struct attribute to initialize
 */
#define sysfs_attr_init(attr) do {} while(0)
#endif /* sysfs_attr_init */

/**
 *	sysfs_bin_attr_init - initialize a dynamically allocated bin_attribute
 *	@attr: struct bin_attribute to initialize
 *
 *	Initialize a dynamically allocated struct bin_attribute so we
 *	can make lockdep happy.  This is a new requirement for
 *	attributes and initially this is only needed when lockdep is
 *	enabled.  Lockdep gives a nice error when your attribute is
 *	added to sysfs if you don't have this.
 */
#ifndef sysfs_bin_attr_init
#define sysfs_bin_attr_init(bin_attr) sysfs_attr_init(&(bin_attr)->attr)
#endif /* sysfs_bin_attr_init */

#define usb_alloc_coherent(dev, size, mem_flags, dma) usb_buffer_alloc(dev, size, mem_flags, dma)
#define usb_free_coherent(dev, size, addr, dma) usb_buffer_free(dev, size, addr, dma)

/* only include this if DEFINE_DMA_UNMAP_ADDR is not set as debian squeeze also backports this  */
#ifndef DEFINE_DMA_UNMAP_ADDR
#ifdef CONFIG_NEED_DMA_MAP_STATE
#define DEFINE_DMA_UNMAP_ADDR(ADDR_NAME)        dma_addr_t ADDR_NAME
#define DEFINE_DMA_UNMAP_LEN(LEN_NAME)          __u32 LEN_NAME
#define dma_unmap_addr(PTR, ADDR_NAME)           ((PTR)->ADDR_NAME)
#define dma_unmap_addr_set(PTR, ADDR_NAME, VAL)  (((PTR)->ADDR_NAME) = (VAL))
#define dma_unmap_len(PTR, LEN_NAME)             ((PTR)->LEN_NAME)
#define dma_unmap_len_set(PTR, LEN_NAME, VAL)    (((PTR)->LEN_NAME) = (VAL))
#else
#define DEFINE_DMA_UNMAP_ADDR(ADDR_NAME)
#define DEFINE_DMA_UNMAP_LEN(LEN_NAME)
#define dma_unmap_addr(PTR, ADDR_NAME)           (0)
#define dma_unmap_addr_set(PTR, ADDR_NAME, VAL)  do { } while (0)
#define dma_unmap_len(PTR, LEN_NAME)             (0)
#define dma_unmap_len_set(PTR, LEN_NAME, VAL)    do { } while (0)
#endif
#endif

/* mask dma_set_coherent_mask as debian squeeze also backports this */
#define dma_set_coherent_mask(a, b) compat_dma_set_coherent_mask(a, b)

static inline int dma_set_coherent_mask(struct device *dev, u64 mask)
{
	if (!dma_supported(dev, mask))
		return -EIO;
	dev->coherent_dma_mask = mask;
	return 0;
}

/* USB autosuspend and autoresume */
static inline int usb_enable_autosuspend(struct usb_device *udev)
{ return 0; }
static inline int usb_disable_autosuspend(struct usb_device *udev)
{ return 0; }

#ifndef rcu_dereference_protected
#define rcu_dereference_protected(p, c) (p)
#endif

#define rcu_access_pointer(p)   ACCESS_ONCE(p)

#define rcu_dereference_raw(p)	rcu_dereference(p)

#define KEY_WPS_BUTTON		0x211	/* WiFi Protected Setup key */

/*
 * This looks more complex than it should be. But we need to
 * get the type for the ~ right in round_down (it needs to be
 * as wide as the result!), and we want to evaluate the macro
 * arguments just once each.
 */
#define __round_mask(x, y) ((__typeof__(x))((y)-1))
#define round_up(x, y) ((((x)-1) | __round_mask(x, y))+1)
#define round_down(x, y) ((x) & ~__round_mask(x, y))

#ifndef CONFIG_PROVE_LOCKING
/*
 * Starting with the 3.13 kernel, include/linux/rtnetlink.h declares
 * static inline int lockdep_rtnl_is_held(void) if CONFIG_PROVE_LOCKING
 * is not defined.
 *
 * Here is the original comment:
 *
 * Obviously, this is wrong.  But the base kernel will have rtnl_mutex
 * declared static, with no way to access it.  I think this is the best
 * we can do...
 */
static inline int lockdep_rtnl_is_held(void)
{
        return 1;
}
#endif /* #ifndef CONFIG_PROVE_LOCKING */

#ifndef NETIF_F_NTUPLE
#define NETIF_F_NTUPLE		(1 << 27) /* N-tuple filters supported */
#endif


#ifndef PCI_VPD_LRDT
#define PCI_VPD_LRDT			0x80	/* Large Resource Data Type */
#define PCI_VPD_LRDT_ID(x)		(x | PCI_VPD_LRDT)

/* Large Resource Data Type Tag Item Names */
#define PCI_VPD_LTIN_ID_STRING		0x02	/* Identifier String */
#define PCI_VPD_LTIN_RO_DATA		0x10	/* Read-Only Data */
#define PCI_VPD_LTIN_RW_DATA		0x11	/* Read-Write Data */

#define PCI_VPD_LRDT_ID_STRING		PCI_VPD_LRDT_ID(PCI_VPD_LTIN_ID_STRING)
#define PCI_VPD_LRDT_RO_DATA		PCI_VPD_LRDT_ID(PCI_VPD_LTIN_RO_DATA)
#define PCI_VPD_LRDT_RW_DATA		PCI_VPD_LRDT_ID(PCI_VPD_LTIN_RW_DATA)
#define PCI_VPD_LRDT_TAG_SIZE		3
#define PCI_VPD_SRDT_TAG_SIZE		1

#define PCI_VPD_INFO_FLD_HDR_SIZE	3

/* Small Resource Data Type Tag Item Names */
#define PCI_VPD_STIN_END		0x78	/* End */

#define PCI_VPD_SRDT_END		PCI_VPD_STIN_END

#define PCI_VPD_SRDT_TIN_MASK		0x78
#define PCI_VPD_SRDT_LEN_MASK		0x07
#endif /* PCI_VPD_LRDT */

/**
 * pci_vpd_info_field_size - Extracts the information field length
 * @lrdt: Pointer to the beginning of an information field header
 *
 * Returns the extracted information field length.
 */
#define pci_vpd_info_field_size LINUX_BACKPORT(pci_vpd_info_field_size)
static inline u8 pci_vpd_info_field_size(const u8 *info_field)
{
	return info_field[2];
}

/**
 * pci_vpd_lrdt_size - Extracts the Large Resource Data Type length
 * @lrdt: Pointer to the beginning of the Large Resource Data Type tag
 *
 * Returns the extracted Large Resource Data Type length.
 */
#define pci_vpd_lrdt_size LINUX_BACKPORT(pci_vpd_lrdt_size)
static inline u16 pci_vpd_lrdt_size(const u8 *lrdt)
{
	return (u16)lrdt[1] + ((u16)lrdt[2] << 8);
}

/**
 * pci_vpd_find_info_keyword - Locates an information field keyword in the VPD
 * @buf: Pointer to buffered vpd data
 * @off: The offset into the buffer at which to begin the search
 * @len: The length of the buffer area, relative to off, in which to search
 * @kw: The keyword to search for
 *
 * Returns the index where the information field keyword was found or
 * -ENOENT otherwise.
 */
#define pci_vpd_find_info_keyword LINUX_BACKPORT(pci_vpd_find_info_keyword)
int pci_vpd_find_info_keyword(const u8 *buf, unsigned int off,
			      unsigned int len, const char *kw);

/**
 * pci_vpd_find_tag - Locates the Resource Data Type tag provided
 * @buf: Pointer to buffered vpd data
 * @off: The offset into the buffer at which to begin the search
 * @len: The length of the vpd buffer
 * @rdt: The Resource Data Type to search for
 *
 * Returns the index where the Resource Data Type was found or
 * -ENOENT otherwise.
 */
#define pci_vpd_find_tag LINUX_BACKPORT(pci_vpd_find_tag)
int pci_vpd_find_tag(const u8 *buf, unsigned int off, unsigned int len, u8 rdt);

/**
 * pci_vpd_srdt_size - Extracts the Small Resource Data Type length
 * @lrdt: Pointer to the beginning of the Small Resource Data Type tag
 *
 * Returns the extracted Small Resource Data Type length.
 */
#define pci_vpd_srdt_size LINUX_BACKPORT(pci_vpd_srdt_size)
static inline u8 pci_vpd_srdt_size(const u8 *srdt)
{
	return (*srdt) & PCI_VPD_SRDT_LEN_MASK;
}

#ifndef this_cpu_inc
#define this_cpu_inc(x) ((x)++)
#endif

#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,34)) */


#endif /* LINUX_26_34_COMPAT_H */
