/*
 * Copyright (c) 2018 Yizhou Shan <ys@purdue.edu>. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef _LASTWEEK_USER_COMPILER_H_
#define _LASTWEEK_USER_COMPILER_H_

#include <assert.h>

#define BUG_ON(cond)	assert(!!!(cond))
#define BUG()		BUG_ON(1)

#define likely(x)	__builtin_expect(!!(x), 1)
#define unlikely(x)	__builtin_expect(!!(x), 0)

/*
 * Mark a position in code as unreachable.  This can be used to
 * suppress control flow warnings after asm blocks that transfer
 * control elsewhere.
 *
 * Early snapshots of gcc 4.5 don't support this and we can't detect
 * this in the preprocessor, but we can live with this because they're
 * unreleased.  Really, we need to have autoconf for the kernel.
 */
#define unreachable() __builtin_unreachable()

/* The "volatile" is due to gcc bugs */
#define barrier() __asm__ __volatile__("": : :"memory")

#endif /* _LASTWEEK_USER_COMPILER_H_ */
