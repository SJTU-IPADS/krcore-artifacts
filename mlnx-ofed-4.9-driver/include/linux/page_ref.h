#ifndef _COMPAT_LINUX_PAGE_REF_H
#define _COMPAT_LINUX_PAGE_REF_H

#include "../../compat/config.h"

#ifdef HAVE_PAGE_REF_COUNT_ADD_SUB_INC
#include_next <linux/page_ref.h>
#else
static inline int page_ref_count(struct page *page)
{
	return atomic_read(&page->_count);
}

static inline void page_ref_add(struct page *page, int nr)
{
	atomic_add(nr, &page->_count);
}

static inline void page_ref_sub(struct page *page, int nr)
{
	atomic_sub(nr, &page->_count);
}

static inline void page_ref_inc(struct page *page)
{
	atomic_inc(&page->_count);
}

static inline void page_ref_dec(struct page *page)
{
	atomic_dec(&page->_count);
}
#endif

#endif /* _COMPAT_LINUX_PAGE_REF_H */
