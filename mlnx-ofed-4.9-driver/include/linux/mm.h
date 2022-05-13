#ifndef _COMPAT_LINUX_MM_H
#define _COMPAT_LINUX_MM_H

#include "../../compat/config.h"

#include_next <linux/mm.h>
#include <linux/page_ref.h>

#include <linux/overflow.h>

#ifndef HAVE_KVZALLOC
#include <linux/vmalloc.h>
#include <linux/slab.h>

static inline void *kvzalloc(unsigned long size,...) {
	void *rtn;

	rtn = kzalloc(size, GFP_KERNEL | __GFP_NOWARN);
	if (!rtn)
		rtn = vzalloc(size);
	return rtn;
}
#endif

#ifndef HAVE_KVMALLOC_ARRAY
#include <linux/vmalloc.h>
#include <linux/slab.h>

static inline void *kvmalloc_array(size_t n, size_t size,...) {
	void *rtn;

	rtn = kcalloc(n, size, GFP_KERNEL | __GFP_NOWARN);
	if (!rtn)
		rtn = vzalloc(n * size);
	return rtn;
}
#endif

#ifndef HAVE_KVMALLOC_NODE
#include <linux/vmalloc.h>
#include <linux/slab.h>

static inline void *kvmalloc_node(size_t size, gfp_t flags, int node) {
	void *rtn;

	rtn = kmalloc_node(size, GFP_KERNEL | __GFP_NOWARN, node);
	if (!rtn)
		rtn = vmalloc(size);

	if (rtn && flags & __GFP_ZERO)
		memset(rtn, 0, size);

	return rtn;
}
#endif

#ifndef HAVE_KVMALLOC
#include <linux/vmalloc.h>
#include <linux/slab.h>

static inline void *kvmalloc(size_t size, gfp_t flags)
{
        return kvmalloc_node(size, flags, NUMA_NO_NODE);
}

#endif
#ifndef HAVE_KVZALLOC_NODE
#include <linux/vmalloc.h>
#include <linux/slab.h>

static inline void *kvzalloc_node(size_t size, gfp_t flags, int node)
{
	void *p = kvmalloc_node(size, flags, node);
	if (p)
		memset(p, 0, size);
	return p;
}
#endif

#ifndef HAVE_KVCALLOC
#include <linux/vmalloc.h>
#include <linux/slab.h>

static inline void *kvcalloc(size_t n, size_t size, gfp_t flags)
{
	return kvmalloc_array(n, size, flags | __GFP_ZERO);
}
#endif

#ifndef HAVE_IS_PCI_P2PDMA_PAGE
static inline bool is_pci_p2pdma_page(const struct page *page)
{
        return false;
}

#ifndef HAVE_PAGE_IS_PFMEMALLOC
static inline bool page_is_pfmemalloc(struct page *page)
{
#ifdef HAVE_PAGE_PFMEMALLOC
	return page->pfmemalloc;
#else
	return false;
#endif
}
#endif

#endif
#endif /* _COMPAT_LINUX_MM_H */
