#ifndef _COMPAT_LINUX_COMPILER_CLANG_H
#define _COMPAT_LINUX_COMPILER_CLANG_H

#include "../../compat/config.h"

#include_next <linux/compiler-clang.h>

#ifndef HAVE_LINUX_OVERFLOW_H

/*
   + * Not all versions of clang implement the the type-generic versions
   + * of the builtin overflow checkers. Fortunately, clang implements
   + * __has_builtin allowing us to avoid awkward version
   + * checks. Unfortunately, we don't know which version of gcc clang
   + * pretends to be, so the macro may or may not be defined.
   + */
#undef COMPILER_HAS_GENERIC_BUILTIN_OVERFLOW
#if __has_builtin(__builtin_mul_overflow) && \
    __has_builtin(__builtin_add_overflow) && \
    __has_builtin(__builtin_sub_overflow)
#define COMPILER_HAS_GENERIC_BUILTIN_OVERFLOW 1
#endif

#endif /* HAVE_LINUX_OVERFLOW_H */

#endif /* _COMPAT_LINUX_COMPILER_CLANG_H */
