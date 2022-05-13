#ifndef _COMPAT_LINUX_BITMAP_H
#define _COMPAT_LINUX_BITMAP_H

#include "../../compat/config.h"

#include_next <linux/bitmap.h>


#ifndef HAVE_BITMAP_KZALLOC
#define bitmap_alloc LINUX_BACKPORT(bitmap_alloc)
extern unsigned long *bitmap_alloc(unsigned int nbits, gfp_t flags);
#define bitmap_zalloc LINUX_BACKPORT(bitmap_zalloc)
extern unsigned long *bitmap_zalloc(unsigned int nbits, gfp_t flags);
#endif

#ifndef HAVE_BITMAP_FREE
#define bitmap_free LINUX_BACKPORT(bitmap_free)
extern void bitmap_free(const unsigned long *bitmap);
#endif

#ifndef HAVE_BITMAP_FROM_ARR32
#if BITS_PER_LONG == 64
extern void bitmap_from_arr32(unsigned long *bitmap, const u32 *buf,
			      unsigned int nbits);
#else

static inline void bitmap_copy_clear_tail(unsigned long *dst,
                const unsigned long *src, unsigned int nbits)
{
        bitmap_copy(dst, src, nbits);
        if (nbits % BITS_PER_LONG)
                dst[nbits / BITS_PER_LONG] &= BITMAP_LAST_WORD_MASK(nbits);
}

#define bitmap_from_arr32(bitmap, buf, nbits)                   \
        bitmap_copy_clear_tail((unsigned long *) (bitmap),      \
                        (const unsigned long *) (buf), (nbits))
#endif
#endif

#endif /* _COMPAT_LINUX_BITMAP_H */
