/*
 * Copyright (c) 2007 Cisco Systems.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef IB_UMEM_H
#define IB_UMEM_H

#include <linux/list.h>
#include <linux/scatterlist.h>
#include <linux/workqueue.h>
#include <rdma/ib_verbs.h>
#include <rdma/ib_peer_mem.h>

struct ib_ucontext;
struct ib_umem_odp;
struct ib_umem;

typedef void (*umem_invalidate_func_t)(void *invalidation_cookie,
		struct ib_umem *umem,
		unsigned long addr, size_t size);

struct invalidation_ctx {
	struct ib_umem *umem;
	u64 context_ticket;
	umem_invalidate_func_t func;
	void *cookie;
	int peer_callback;
	int inflight_invalidation;
	int peer_invalidated;
	struct completion comp;
};

struct ib_umem {
	struct ib_ucontext     *context;
	struct mm_struct       *owning_mm;
	size_t			length;
	unsigned long		address;
	int			page_shift;
	u32 writable : 1;
	u32 hugetlb : 1;
	u32 is_odp : 1;
	struct work_struct	work;
	struct sg_table sg_head;
	int             nmap;
	unsigned int    sg_nents;
	/* peer memory that manages this umem */
	struct ib_peer_memory_client *ib_peer_mem;
	struct invalidation_ctx *invalidation_ctx;
	/* peer memory private context */
	void *peer_mem_client_context;
};

/* Returns the offset of the umem start relative to the first page. */
static inline int ib_umem_offset(struct ib_umem *umem)
{
	return umem->address & (BIT(umem->page_shift) - 1);
}

/* Returns the first page of an ODP umem. */
static inline unsigned long ib_umem_start(struct ib_umem *umem)
{
	return umem->address - ib_umem_offset(umem);
}

/* Returns the address of the page after the last one of an ODP umem. */
static inline unsigned long ib_umem_end(struct ib_umem *umem)
{
	return ALIGN(umem->address + umem->length, BIT(umem->page_shift));
}

static inline size_t ib_umem_num_pages(struct ib_umem *umem)
{
	return (ib_umem_end(umem) - ib_umem_start(umem)) >> umem->page_shift;
}

#ifdef CONFIG_INFINIBAND_USER_MEM

struct ib_umem *ib_umem_get(struct ib_udata *udata, unsigned long addr,
			    size_t size, int access, int dmasync,
			    unsigned long peer_mem_flags);
void ib_umem_release(struct ib_umem *umem);
int ib_umem_page_count(struct ib_umem *umem);
int ib_umem_copy_from(void *dst, struct ib_umem *umem, size_t offset,
		      size_t length);
unsigned long ib_umem_find_best_pgsz(struct ib_umem *umem,
				     unsigned long pgsz_bitmap,
				     unsigned long virt);
int  ib_umem_activate_invalidation_notifier(struct ib_umem *umem,
						   umem_invalidate_func_t func,
						   void *cookie);

#else /* CONFIG_INFINIBAND_USER_MEM */

#include <linux/err.h>

static inline struct ib_umem *ib_umem_get(struct ib_udata *udata,
					     unsigned long addr, size_t size,
					     int access, int dmasync,
					     unsigned long peer_mem_flags) {
{
	return ERR_PTR(-EINVAL);
}
static inline void ib_umem_release(struct ib_umem *umem) { }
static inline int ib_umem_page_count(struct ib_umem *umem) { return 0; }
static inline int  ib_umem_activate_invalidation_notifier(struct ib_umem *umem,
						   umem_invalidate_func_t func,
						   void *cookie) { return 0; }
static inline int ib_umem_copy_from(void *dst, struct ib_umem *umem, size_t offset,
		      		    size_t length) {
	return -EINVAL;
}
static inline int ib_umem_find_best_pgsz(struct ib_umem *umem,
					 unsigned long pgsz_bitmap,
					 unsigned long virt) {
	return -EINVAL;
}

#endif /* CONFIG_INFINIBAND_USER_MEM */

#endif /* IB_UMEM_H */
