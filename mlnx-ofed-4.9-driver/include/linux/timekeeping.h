#ifndef COMPAT_LINUX_TIMEKEEPING_H
#define COMPAT_LINUX_TIMEKEEPING_H

#include "../../compat/config.h"

#include_next <linux/timekeeping.h>

#ifndef HAVE_KTIME_GET_NS
static inline u64 ktime_get_ns(void)
{
	return ktime_to_ns(ktime_get());
}
#endif /* HAVE_KTIME_TO_NS */

#endif /* COMPAT_LINUX_TIMEKEEPING_H */
