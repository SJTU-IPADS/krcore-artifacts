#ifndef COMPAT_LINUX_SCHED_SIGNAL_H
#define COMPAT_LINUX_SCHED_SIGNAL_H

#include "../../../compat/config.h"

#ifdef HAVE_SCHED_SIGNAL_H
#include_next <linux/sched/signal.h>
#endif

#endif /* COMPAT_LINUX_SCHED_SIGNAL_H */
