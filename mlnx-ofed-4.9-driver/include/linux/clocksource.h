#ifndef LINUX_CLOCKSOURCE_H
#define LINUX_CLOCKSOURCE_H

#include <linux/version.h>
#include "../../compat/config.h"

#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 16)
#include_next <linux/clocksource.h>
#endif

#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 18)
#ifndef HAVE_TIMECOUNTER_ADJTIME
/**
* timecounter_adjtime - Shifts the time of the clock.
* @delta:     Desired change in nanoseconds.
*/
static inline void timecounter_adjtime(struct timecounter *tc, s64 delta)
{
	tc->nsec += delta;
}
#endif /* HAVE_TIMECOUNTER_H */
#endif

#endif /* LINUX_CLOCKSOURCE_H */
