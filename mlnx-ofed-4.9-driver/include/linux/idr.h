#ifndef _COMPAT_LINUX_IDR_H
#define _COMPAT_LINUX_IDR_H

#include "../../compat/config.h"

#include_next <linux/idr.h>

#ifndef HAVE_IDR_GET_NEXT_EXPORTED
#define idr_get_next LINUX_BACKPORT(idr_get_next)
void *idr_get_next(struct idr *idr, int *nextid);
#endif

#ifndef HAVE_IDR_PRELOAD_END
static inline void idr_preload_end(void)
{
	        preempt_enable();
}
#endif
#ifndef HAVE_IDR_FOR_EACH_ENTRY
#define compat_idr_for_each_entry(idr, entry, id)                      \
		for (id = 0; ((entry) = idr_get_next(idr, &(id))) != NULL; ++id)
#else
#define compat_idr_for_each_entry(idr, entry, id)          \
		idr_for_each_entry(idr, entry, id)
#endif

#ifndef HAVE_IDA_SIMPLE_GET
#define ida_simple_remove LINUX_BACKPORT(ida_simple_remove)
void ida_simple_remove(struct ida *ida, unsigned int id);

#define ida_simple_get LINUX_BACKPORT(ida_simple_get)
int ida_simple_get(struct ida *ida, unsigned int start, unsigned int end,
		   gfp_t gfp_mask);

#endif /* HAVE_IDA_SIMPLE_GET */
#ifndef HAVE_IDR_GET_NEXT_UL_EXPORTED
#define idr_get_next_ul LINUX_BACKPORT(idr_get_next_ul)
void *idr_get_next_ul(struct idr *idr, unsigned long *nextid);


#define idr_alloc_u32 LINUX_BACKPORT(idr_alloc_u32)
int idr_alloc_u32(struct idr *idr, void *ptr, u32 *nextid,
		unsigned long max, gfp_t gfp);
#endif
#endif /* _COMPAT_LINUX_IDR_H */
