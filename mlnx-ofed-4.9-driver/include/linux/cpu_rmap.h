#ifndef _COMPAT_LINUX_CPU_RMAP_H
#define _COMPAT_LINUX_CPU_RMAP_H 1

#include <linux/version.h>

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,39)) || defined (CONFIG_COMPAT_IS_LINUX_CPU_RMAP)
#include_next <linux/cpu_rmap.h>
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,39)) */

#endif	/* _COMPAT_LINUX_CPU_RMAP_H */
