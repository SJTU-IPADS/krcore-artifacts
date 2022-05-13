#ifndef LINUX_26_25_COMPAT_H
#define LINUX_26_25_COMPAT_H

#include <linux/version.h>

/* Compat work for 2.6.24 */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,25))

#include <linux/types.h>
#include <linux/io.h>
#include <linux/hw_random.h>
#include <linux/leds.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/pm.h>
#include <asm-generic/bug.h>
#include <linux/pci.h>
#include <linux/in.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/pci.h>

/* Backports b718989da7 */
#define pci_enable_device_mem LINUX_BACKPORT(pci_enable_device_mem)
int __must_check pci_enable_device_mem(struct pci_dev *dev);

/*
 * Backports 312b1485fb509c9bc32eda28ad29537896658cb8
 * Author: Sam Ravnborg <sam@ravnborg.org>
 * Date:   Mon Jan 28 20:21:15 2008 +0100
 * 
 * Introduce new section reference annotations tags: __ref, __refdata, __refconst
 */
#define __ref		__init_refok
#define __refdata	__initdata_refok

/*
 * backports 2658fa803111dae1353602e7f586de8e537803e2
 */

static inline bool ipv4_is_loopback(__be32 addr)
{
	return (addr & htonl(0xff000000)) == htonl(0x7f000000);
}

static inline bool ipv4_is_multicast(__be32 addr)
{
	return (addr & htonl(0xf0000000)) == htonl(0xe0000000);
}

static inline bool ipv4_is_local_multicast(__be32 addr)
{
	return (addr & htonl(0xffffff00)) == htonl(0xe0000000);
}

static inline bool ipv4_is_lbcast(__be32 addr)
{
	/* limited broadcast */
	return addr == htonl(INADDR_BROADCAST);
}

static inline bool ipv4_is_zeronet(__be32 addr)
{
	return (addr & htonl(0xff000000)) == htonl(0x00000000);
}

/* Special-Use IPv4 Addresses (RFC3330) */

static inline bool ipv4_is_private_10(__be32 addr)
{
	return (addr & htonl(0xff000000)) == htonl(0x0a000000);
}

static inline bool ipv4_is_private_172(__be32 addr)
{
	return (addr & htonl(0xfff00000)) == htonl(0xac100000);
}

static inline bool ipv4_is_private_192(__be32 addr)
{
	return (addr & htonl(0xffff0000)) == htonl(0xc0a80000);
}

static inline bool ipv4_is_linklocal_169(__be32 addr)
{
	return (addr & htonl(0xffff0000)) == htonl(0xa9fe0000);
}

static inline bool ipv4_is_anycast_6to4(__be32 addr)
{
	return (addr & htonl(0xffffff00)) == htonl(0xc0586300);
}

static inline bool ipv4_is_test_192(__be32 addr)
{
	return (addr & htonl(0xffffff00)) == htonl(0xc0000200);
}

static inline bool ipv4_is_test_198(__be32 addr)
{
	return (addr & htonl(0xfffe0000)) == htonl(0xc6120000);
}

/*
 * phys_addr_t was added as a generic arch typedef on 2.6.28,
 * that backport is dealt with in compat-2.6.28.h
 */
#if defined(CONFIG_X86) || defined(CONFIG_X86_64)

#if defined(CONFIG_64BIT) || defined(CONFIG_X86_PAE) || defined(CONFIG_PHYS_64BIT)
typedef u64 phys_addr_t;
#else
typedef u32 phys_addr_t;
#endif

#endif /* x86 */

/* The macro below uses a const upstream, this differs */

/**
 * DEFINE_PCI_DEVICE_TABLE - macro used to describe a pci device table
 * @_table: device table name
 *
 * This macro is used to create a struct pci_device_id array (a device table)
 * in a generic manner.
 */
#define DEFINE_PCI_DEVICE_TABLE(_table) \
	const struct pci_device_id _table[] __devinitdata

/*
 * 2.6.25 adds PM_EVENT_HIBERNATE as well here but
 * we don't have this on <= 2.6.23)
 */
#ifndef PM_EVENT_SLEEP /* some distribution have mucked with their own headers to add this.. */
#define PM_EVENT_SLEEP  (PM_EVENT_SUSPEND)
#endif

/* Although we don't care about wimax this is needed for rfkill input stuff */
#define KEY_WIMAX		246

#define __WARN(foo) dump_stack()

#define dev_emerg(dev, format, arg...)          \
	dev_printk(KERN_EMERG , dev , format , ## arg)
#define dev_alert(dev, format, arg...)          \
	dev_printk(KERN_ALERT , dev , format , ## arg)
#define dev_crit(dev, format, arg...)           \
	dev_printk(KERN_CRIT , dev , format , ## arg)

#define __dev_addr_sync LINUX_BACKPORT(__dev_addr_sync)
extern int		__dev_addr_sync(struct dev_addr_list **to, int *to_count, struct dev_addr_list **from, int *from_count);
#define __dev_addr_unsync LINUX_BACKPORT(__dev_addr_unsync)
extern void		__dev_addr_unsync(struct dev_addr_list **to, int *to_count, struct dev_addr_list **from, int *from_count);

#define seq_file_net &init_net;

enum nf_inet_hooks {
	NF_INET_PRE_ROUTING = 0,
	NF_INET_LOCAL_IN = 1,
	NF_INET_FORWARD = 2,
	NF_INET_LOCAL_OUT = 3,
	NF_INET_POST_ROUTING = 4,
	NF_INET_NUMHOOKS = 5
};

/* The patch:
 * commit 8b5f6883683c91ad7e1af32b7ceeb604d68e2865
 * Author: Marcin Slusarz <marcin.slusarz@gmail.com>
 * Date:   Fri Feb 8 04:20:12 2008 -0800
 *
 *     byteorder: move le32_add_cpu & friends from OCFS2 to core
 *
 * moves le*_add_cpu and be*_add_cpu functions from OCFS2 to core
 * header (1st) and converted some existing code to it. We port
 * it here as later kernels will most likely use it.
 */
static inline void le16_add_cpu(__le16 *var, u16 val)
{
	*var = cpu_to_le16(le16_to_cpu(*var) + val);
}

static inline void le32_add_cpu(__le32 *var, u32 val)
{
	*var = cpu_to_le32(le32_to_cpu(*var) + val);
}

static inline void le64_add_cpu(__le64 *var, u64 val)
{
	*var = cpu_to_le64(le64_to_cpu(*var) + val);
}

static inline void be16_add_cpu(__be16 *var, u16 val)
{
	u16 v = be16_to_cpu(*var);
	*var = cpu_to_be16(v + val);
}

static inline void be32_add_cpu(__be32 *var, u32 val)
{
	u32 v = be32_to_cpu(*var);
	*var = cpu_to_be32(v + val);
}

static inline void be64_add_cpu(__be64 *var, u64 val)
{
	u64 v = be64_to_cpu(*var);
	*var = cpu_to_be64(v + val);
}

/* 2.6.25 changes hwrng_unregister()'s behaviour by supporting
 * suspend of its parent device (the misc device, which is itself the
 * hardware random number generator). It does this by passing a parameter to
 * unregister_miscdev() which is not supported in older kernels. The suspend
 * parameter allows us to enable access to the device's hardware
 * number generator during suspend. As far as wireless is concerned this means
 * if a driver goes to suspend it you won't have the HNR available in
 * older kernels. */
static inline void __hwrng_unregister(struct hwrng *rng, bool suspended)
{
	hwrng_unregister(rng);
}

static inline void led_classdev_unregister_suspended(struct led_classdev *lcd)
{
	led_classdev_unregister(lcd);
}

/**
 * The following things are out of ./include/linux/kernel.h
 * The new iwlwifi driver is using them.
 */
#define strict_strtoul LINUX_BACKPORT(strict_strtoul)
extern int strict_strtoul(const char *, unsigned int, unsigned long *);
#define strict_strtol LINUX_BACKPORT(strict_strtol)
extern int strict_strtol(const char *, unsigned int, long *);

#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,25)) */

#endif /* LINUX_26_25_COMPAT_H */
