#ifndef LINUX_4_0_COMPAT_H
#define LINUX_4_0_COMPAT_H

#include <linux/version.h>

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4,0,0))
#include <linux/string.h>

#define kstrdup_const LINUX_BACKPORT(kstrdup_const)
static inline const char *kstrdup_const(const char *s, gfp_t gfp)
{
	return kstrdup(s, gfp);
}

#ifndef HAVE_KFREE_CONST
#include <linux/slab.h>
#include <linux/uaccess.h>

static inline void kfree_const(const void *x)
{
	kfree(x);
}
#endif
#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(4,0,0)) */

#endif /* LINUX_4_0_COMPAT_H */
