#ifndef _COMPAT_LINUX_NODEMASK_H
#define _COMPAT_LINUX_NODEMASK_H

#include "../../compat/config.h"

#include_next <linux/nodemask.h>

#ifndef first_memory_node
#if MAX_NUMNODES > 1
#ifdef HAVE_N_MEMORY
#define first_memory_node	first_node(node_states[N_MEMORY])
#else
#define first_memory_node	first_node(node_states[N_NORMAL_MEMORY])
#endif
#else
#define first_memory_node	0
#endif
#endif

#endif /* _COMPAT_LINUX_NODEMASK_H */
