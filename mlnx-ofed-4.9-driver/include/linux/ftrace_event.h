#ifndef _COMPAT_LINUX_FTRACE_EVENT_H
#define _COMPAT_LINUX_FTRACE_EVENT_H

#include "../../compat/config.h"

/* ugly W/A for old kernels redifinig is_signed_type since we will
 * have it already defined in include/linux/overflow.h
 */
#ifdef is_signed_type
#undef is_signed_type
#endif

#include_next <linux/ftrace_event.h>

#endif /* _COMPAT_LINUX_TRACE_EVENTS_H */
