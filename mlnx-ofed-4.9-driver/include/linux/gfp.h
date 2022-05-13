#ifndef _COMPAT_LINUX_GFP_H
#define _COMPAT_LINUX_GFP_H

#include "../../compat/config.h"
#include <linux/version.h>

#include_next <linux/gfp.h>
#ifndef __GFP_ACCOUNT
#define ___GFP_ACCOUNT          0x100000u
#define __GFP_ACCOUNT   ((__force gfp_t)___GFP_ACCOUNT)
#endif
#ifndef __GFP_MEMALLOC
#define __GFP_MEMALLOC	0
#endif


#ifndef HAS_GFP_DIRECT_RECLAIM
#define ___GFP_DIRECT_RECLAIM	0x400u
#define __GFP_DIRECT_RECLAIM	((__force gfp_t)___GFP_DIRECT_RECLAIM) /* Caller can reclaim */
#endif
#ifndef HAS_GFPFLAGES_ALLOW_BLOCKING
static inline bool gfpflags_allow_blocking(const gfp_t gfp_flags)
{
	return !!(gfp_flags & __GFP_DIRECT_RECLAIM);
}
#endif
#endif /* _COMPAT_LINUX_GFP_H */
