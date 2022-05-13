#ifndef _COMPAT_LINUX_RATELIMIT_H
#define _COMPAT_LINUX_RATELIMIT_H 1

#include <linux/version.h>
#include "../../compat/config.h"

#include_next <linux/ratelimit.h>

#ifndef HAVE_PR_DEBUG_RATELIMITED
#define pr_debug_ratelimited printk
#define pr_warn_ratelimited printk
#endif

#endif	/* _COMPAT_LINUX_RATELIMIT_H */
