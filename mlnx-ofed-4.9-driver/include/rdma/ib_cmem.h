#ifndef IB_CMEM_H
#define IB_CMEM_H

#include <rdma/ib_umem.h>
#include <rdma/ib_verbs.h>

/* contiguous memory structure */
struct ib_cmem {
	struct ib_ucontext     *context;
	size_t			length;
	/* Link list of contiguous blocks being part of that cmem  */
	struct list_head ib_cmem_block;

	/* Order of cmem block,  2^ block_order will equal number
	  * of physical pages per block
	  */
	unsigned long    block_order;
	/* Refernce counter for that memory area
	  * When value became 0 pages will be returned to the kernel.
	  */
	struct kref refcount;
};

struct ib_cmem_block {
	struct list_head	list;
	/* page will point to the page struct of the head page
	  * in the current compound page.
	  * block order is saved once as part of ib_cmem.
	  */
	struct page            *page;
};

int ib_cmem_map_contiguous_pages_to_vma(struct ib_cmem *ib_cmem,
					struct vm_area_struct *vma);
struct ib_cmem *ib_cmem_alloc_contiguous_pages(struct ib_ucontext *context,
					       unsigned long total_size,
					       unsigned long page_size_order,
					       int numa_node);
void ib_cmem_release_contiguous_pages(struct ib_cmem *cmem);

#endif
