#ifndef COMPAT_LINUX_KREF_H
#define COMPAT_LINUX_KREF_H

#include "../../compat/config.h"

#include_next <linux/kref.h>

#ifndef HAVE_KREF_GET_UNLESS_ZERO
static inline int __must_check kref_get_unless_zero(struct kref *kref)
{
	return atomic_add_unless(&kref->refcount, 1, 0);
}
#endif

#ifndef HAVE_KREF_READ

static inline int kref_read(struct kref *kref)
{
	return atomic_read(&kref->refcount);
}
#endif

#endif /* COMPAT_LINUX_KREF_H */
