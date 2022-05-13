#ifndef _COMPAT_LINUX_VMALLOC_H
#define _COMPAT_LINUX_VMALLOC_H

#include "../../compat/config.h"

#include <linux/version.h>
#include_next <linux/vmalloc.h>
#include <linux/overflow.h>

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,37))
#define vzalloc LINUX_BACKPORT(vzalloc)
#define vzalloc_node LINUX_BACKPORT(vzalloc_node)

extern void *vzalloc(unsigned long size);
extern void *vzalloc_node(unsigned long size, int node);
#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,37)) */

#endif /* _COMPAT_LINUX_VMALLOC_H */
