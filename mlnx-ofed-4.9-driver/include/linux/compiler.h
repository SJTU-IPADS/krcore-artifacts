#ifndef _COMPAT_COMPILER_H
#define _COMPAT_COMPILER_H

#include_next <linux/compiler.h>
#include <linux/types.h>
#include "../../compat/config.h"

#ifndef HAVE_CONST_READ_ONCE_SIZE
#define __read_once_size LINUX_BACKPORT(__read_once_size)
static __always_inline void __read_once_size(const volatile void *p, void *res, int size)
{
	switch (size) {
	case 1: *(__u8 *)res = *(volatile __u8 *)p; break;
	case 2: *(__u16 *)res = *(volatile __u16 *)p; break;
	case 4: *(__u32 *)res = *(volatile __u32 *)p; break;
#ifdef CONFIG_64BIT
	case 8: *(__u64 *)res = *(volatile __u64 *)p; break;
#endif
	default:
		barrier();
		__builtin_memcpy((void *)res, (const void *)p, size);
		barrier();
	}
}
#endif

#ifndef __percpu
#define __percpu
#endif

#ifndef __aligned
#define __aligned(x)		__attribute__((aligned(x)))
#endif

#ifndef READ_ONCE
#define READ_ONCE(val)		ACCESS_ONCE(val)
#elif !defined (HAVE_CONST_READ_ONCE_SIZE)
#undef READ_ONCE
#define READ_ONCE(x) \
	({ union { typeof(x) __val; char __c[1]; } __u; __read_once_size(&(x), __u.__c, sizeof(x)); __u.__val; })
#endif

#ifndef WRITE_ONCE
#define WRITE_ONCE(var, val)	{ ACCESS_ONCE(var) = val; }
#endif

#endif /* _COMPAT_COMPILER_H */
