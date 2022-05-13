#ifndef _COMPAT_ASM_GENERIC_SCATTERLIST_H
#define _COMPAT_ASM_GENERIC_SCATTERLIST_H

#include "../../compat/config.h"
#include <linux/version.h>
#include_next <asm-generic/scatterlist.h>

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 35)) && \
    !defined(CONFIG_NEED_SG_DMA_LENGTH) && (__BITS_PER_LONG == 64)
#define CONFIG_NEED_SG_DMA_LENGTH
#endif

#endif /* _COMPAT_ASM_GENERIC_SCATTERLIST_H */
