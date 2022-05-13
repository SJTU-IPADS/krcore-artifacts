#ifndef LINUX_3_17_COMPAT_H
#define LINUX_3_17_COMPAT_H

#include <linux/version.h>
#include "../../compat/config.h"

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,17,0))

#ifndef HAVE_KTIME_GET_REAL_NS
#include <linux/hrtimer.h>
#include <linux/ktime.h>
static inline u64 ktime_get_real_ns(void) {
	return ktime_to_ns(ktime_get_real());
}
#endif /* HAVE_KTIME_GET_REAL_NS */

#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(3,17,0)) */

#endif /* LINUX_3_17_COMPAT_H */
