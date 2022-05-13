#ifndef _COMPAT_LINUX_IRQ_POLL_H
#define _COMPAT_LINUX_IRQ_POLL_H 1

#include "../../compat/config.h"

#ifdef HAVE_IRQ_POLL_H
#include_next <linux/irq_poll.h>
#endif

#endif	/* _COMPAT_LINUX_IRQ_POLL_H */
