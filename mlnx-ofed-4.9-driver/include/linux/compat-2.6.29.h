#ifndef LINUX_26_29_COMPAT_H
#define LINUX_26_29_COMPAT_H

#include <linux/version.h>

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,29))
#include <linux/netdevice.h>
#include <linux/if_link.h>

#include <linux/skbuff.h>
#include <linux/types.h>

#if \
	defined(CONFIG_ALPHA) || defined(CONFIG_AVR32) || \
	defined(CONFIG_BLACKFIN) || defined(CONFIG_CRIS) || \
	defined(CONFIG_H8300) || defined(CONFIG_IA64) || \
	defined(CONFIG_M68K) ||  defined(CONFIG_MIPS) || \
	defined(CONFIG_PARISC) || defined(CONFIG_S390) || \
	defined(CONFIG_PPC64) || defined(CONFIG_PPC32) || \
	defined(CONFIG_SUPERH) || defined(CONFIG_SPARC) || \
	defined(CONFIG_FRV) || defined(CONFIG_X86) || \
	defined(CONFIG_M32R) || defined(CONFIG_M68K) || \
	defined(CONFIG_MN10300) || defined(CONFIG_XTENSA)
#include <asm/atomic.h>
#else
typedef struct {
	volatile int counter;
} atomic_t;

#ifdef CONFIG_64BIT
typedef struct {
	volatile long counter;
} atomic64_t;
#endif /* CONFIG_64BIT */

#endif

static inline struct net_device_stats *dev_get_stats(struct net_device *dev)
{
	return dev->get_stats(dev);
}

#define DIV_ROUND_CLOSEST(x, divisor)(			\
{							\
	typeof(divisor) __divisor = divisor;		\
	(((x) + ((__divisor) / 2)) / (__divisor));	\
}							\
)

#define eth_mac_addr LINUX_BACKPORT(eth_mac_addr)
extern int eth_mac_addr(struct net_device *dev, void *p);
#define eth_change_mtu LINUX_BACKPORT(eth_change_mtu)
extern int eth_change_mtu(struct net_device *dev, int new_mtu);
#define eth_validate_addr LINUX_BACKPORT(eth_validate_addr)
extern int eth_validate_addr(struct net_device *dev);

#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,29)) */

#endif /*  LINUX_26_29_COMPAT_H */
