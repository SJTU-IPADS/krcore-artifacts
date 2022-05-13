#ifndef _COMPAT_LINUX_COMPILER_INTEL_H
#define _COMPAT_LINUX_COMPILER_INTEL_H

#include "../../compat/config.h"

#include_next <linux/compiler-intel.h>

#ifndef HAVE_LINUX_OVERFLOW_H

/*
 * icc defines __GNUC__, but does not implement the builtin overflow checkers.
 */
#undef COMPILER_HAS_GENERIC_BUILTIN_OVERFLOW

#endif /* HAVE_LINUX_OVERFLOW_H */

#endif /* _COMPAT_LINUX_COMPILER_INTEL_H */
