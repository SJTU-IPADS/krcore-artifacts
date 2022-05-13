#ifndef LINUX_3_15_COMPAT_H
#define LINUX_3_15_COMPAT_H

#include <linux/version.h>

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,15,0))

#define kvfree LINUX_BACKPORT(kvfree)
extern void kvfree(const void *addr);

#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(3,15,0)) */

#endif /* LINUX_3_15_COMPAT_H */
