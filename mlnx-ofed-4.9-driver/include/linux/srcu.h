#ifndef _COMPAT_LINUX_SRCU_H
#define _COMPAT_LINUX_SRCU_H

#include "../../compat/config.h"

#include_next <linux/srcu.h>

#ifndef srcu_dereference
#define srcu_dereference(p, sp) p
#endif

#endif /* _COMPAT_LINUX_SRCU_H */
