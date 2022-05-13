#ifndef _COMPAT_LINUX_NUMA_H
#define _COMPAT_LINUX_NUMA_H

#include "../../compat/config.h"
#include <linux/version.h>

#include_next <linux/numa.h>

#ifndef NUMA_NO_NODE
#define NUMA_NO_NODE	(-1)
#endif

#endif /* _COMPAT_LINUX_NUMA_H */
