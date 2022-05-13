#ifndef COMPAT_UACCESS_H
#define COMPAT_UACCESS_H

#include "../../compat/config.h"

#include_next <linux/uaccess.h>

#ifndef uaccess_kernel
#define uaccess_kernel() segment_eq(get_fs(), KERNEL_DS)
#endif

#endif /* COMPAT_UACCESS_H */
