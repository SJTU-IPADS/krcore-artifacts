#ifndef _COMPAT_LINUX_SDT_H_
#define _COMPAT_LINUX_SDT_H_

#include_next <linux/sdt.h>

#ifdef DTRACE_PROBE
#undef DTRACE_PROBE
#endif

#define DTRACE_PROBE(name, ...)                                         \
        __DTRACE_DOUBLE_APPLY_NOCOMMA(__DTRACE_NONE, __DTRACE_NONE, ## __VA_ARGS__)     \
        do { } while (0)

#endif /* _COMPAT_LINUX_SDT_H_ */
