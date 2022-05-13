#ifndef _COMPAT_LINUX_TIMECOUNTER_H
#define _COMPAT_LINUX_TIMECOUNTER_H 1

#include "../../compat/config.h"

#ifdef HAVE_TIMECOUNTER_H
#include_next <linux/timecounter.h>
#endif

#endif	/* _COMPAT_LINUX_TIMECOUNTER_H */
