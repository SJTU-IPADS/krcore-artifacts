#ifndef _COMPAT_LINUX_TOPOLOGY_H
#define _COMPAT_LINUX_TOPOLOGY_H 1

#include_next <linux/topology.h>

#ifndef topology_thread_cpumask
#define topology_thread_cpumask topology_sibling_cpumask
#endif

#endif	/* _COMPAT_LINUX_TOPOLOGY_H */
