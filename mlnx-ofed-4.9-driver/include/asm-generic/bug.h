#ifndef _COMPAT_ASM_GENERIC_BUG_H
#define _COMPAT_ASM_GENERIC_BUG_H

#include "../../compat/config.h"

#include_next <asm-generic/bug.h>

#ifndef CUT_HERE
#define CUT_HERE		"------------[ cut here ]------------\n"
#endif

#endif /* _COMPAT_ASM_GENERIC_BUG_H */
