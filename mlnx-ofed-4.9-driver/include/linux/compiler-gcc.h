#ifndef _COMPAT_LINUX_COMPILER_GCC_H
#define _COMPAT_LINUX_COMPILER_GCC_H

#include "../../compat/config.h"

#include_next <linux/compiler-gcc.h>

#ifndef fallthrough
# define fallthrough                    do {} while (0)  /* fallthrough */
#endif

#ifndef GCC_VERSION
#define GCC_VERSION (__GNUC__ * 10000		\
		     + __GNUC_MINOR__ * 100	\
		     + __GNUC_PATCHLEVEL__)
#endif

#ifndef HAVE_LINUX_OVERFLOW_H

#if GCC_VERSION >= 50100
#define COMPILER_HAS_GENERIC_BUILTIN_OVERFLOW 1
#endif

#endif /* HAVE_LINUX_OVERFLOW_H */

#endif /* _COMPAT_LINUX_COMPILER_GCC_H */
