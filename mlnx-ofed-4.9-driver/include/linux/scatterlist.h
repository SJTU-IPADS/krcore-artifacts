#ifndef _COMPAT_LINUX_SCATTERLIST_H
#define _COMPAT_LINUX_SCATTERLIST_H

#include "../../compat/config.h"
#include <linux/version.h>

#include_next <linux/scatterlist.h>

#if (KERNEL_VERSION(4, 8, 0) <= LINUX_VERSION_CODE) || \
	(defined(RHEL_MAJOR) && RHEL_MAJOR -0 == 7 && RHEL_MINOR -0 >= 4)
#ifndef HAVE_SG_ZERO_BUFFER
/**
 * sg_zero_buffer - Zero-out a part of a SG list
 * @sgl:		 The SG list
 * @nents:		 Number of SG entries
 * @buflen:		 The number of bytes to zero out
 * @skip:		 Number of bytes to skip before zeroing
 *
 * Returns the number of bytes zeroed.
 **/
static inline size_t sg_zero_buffer(struct scatterlist *sgl, unsigned int nents,
		       size_t buflen, off_t skip)
{
	unsigned int offset = 0;
	struct sg_mapping_iter miter;
	unsigned int sg_flags = SG_MITER_ATOMIC | SG_MITER_TO_SG;

	sg_miter_start(&miter, sgl, nents, sg_flags);

	if (!sg_miter_skip(&miter, skip))
		return false;

	while (offset < buflen && sg_miter_next(&miter)) {
		unsigned int len;

		len = min(miter.length, buflen - offset);
		memset(miter.addr, 0, len);

		offset += len;
	}

	sg_miter_stop(&miter);
	return offset;
}
#endif /* HAVE_SG_ZERO_BUFFER */

#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0) */

#if defined(RHEL_MAJOR) && RHEL_MAJOR -0 == 7 && RHEL_MINOR -0 >= 2
#if !defined(HAVE_SG_ALLOC_TABLE_CHAINED_4_PARAMS) && \
    !defined(HAVE_SG_ALLOC_TABLE_CHAINED_3_PARAMS)

#include <scsi/scsi.h>
#include <linux/mempool.h>

struct sg_pool {
	size_t		size;
	char		*name;
	struct kmem_cache	*slab;
	mempool_t	*pool;
};

#define SP(x) { .size = x, "sgpool-" __stringify(x) }
#if (SCSI_MAX_SG_SEGMENTS < 32)
#error SCSI_MAX_SG_SEGMENTS is too small (must be 32 or greater)
#endif
static struct sg_pool sg_pools[] = {
	SP(8),
	SP(16),
#if (SCSI_MAX_SG_SEGMENTS > 32)
	SP(32),
#if (SCSI_MAX_SG_SEGMENTS > 64)
	SP(64),
#if (SCSI_MAX_SG_SEGMENTS > 128)
	SP(128),
#if (SCSI_MAX_SG_SEGMENTS > 256)
#error SCSI_MAX_SG_SEGMENTS is too large (256 MAX)
#endif
#endif
#endif
#endif
	SP(SCSI_MAX_SG_SEGMENTS)
};
#undef SP

static inline unsigned int sg_pool_index(unsigned short nents)
{
	unsigned int index;

	BUG_ON(nents > SCSI_MAX_SG_SEGMENTS);

	if (nents <= 8)
		index = 0;
	else
		index = get_count_order(nents) - 3;

	return index;
}

static inline void sg_pool_free(struct scatterlist *sgl, unsigned int nents)
{
	struct sg_pool *sgp;

	sgp = sg_pools + sg_pool_index(nents);
	mempool_free(sgl, sgp->pool);
}

static inline struct scatterlist *sg_pool_alloc(unsigned int nents, gfp_t gfp_mask)
{
	struct sg_pool *sgp;

	sgp = sg_pools + sg_pool_index(nents);
	return mempool_alloc(sgp->pool, gfp_mask);
}

static inline void sg_free_table_chained(struct sg_table *table, bool first_chunk)
{
	if (first_chunk && table->orig_nents <= SCSI_MAX_SG_SEGMENTS)
		return;
	__sg_free_table(table, SCSI_MAX_SG_SEGMENTS, first_chunk, sg_pool_free);
}

static inline int sg_alloc_table_chained(struct sg_table *table, int nents,
		struct scatterlist *first_chunk)
{
	int ret;

	BUG_ON(!nents);

	if (first_chunk) {
		if (nents <= SCSI_MAX_SG_SEGMENTS) {
			table->nents = table->orig_nents = nents;
			sg_init_table(table->sgl, nents);
			return 0;
		}
	}

	ret = __sg_alloc_table(table, nents, SCSI_MAX_SG_SEGMENTS,
			       first_chunk, GFP_ATOMIC, sg_pool_alloc);
	if (unlikely(ret))
		sg_free_table_chained(table, (bool)first_chunk);
	return ret;
}
#endif
#endif /* defined(RHEL_MAJOR) && RHEL_MAJOR -0 == 7 && RHEL_MINOR -0 >= 2 */

#ifndef HAVE_SG_PAGE_ITER
#define sg_page_iter LINUX_BACKPORT(sg_page_iter)

/*
 * sg page iterator
 *
 * Iterates over sg entries page-by-page.  On each successful iteration,
 * you can call sg_page_iter_page(@piter) and sg_page_iter_dma_address(@piter)
 * to get the current page and its dma address. @piter->sg will point to the
 * sg holding this page and @piter->sg_pgoffset to the page's page offset
 * within the sg. The iteration will stop either when a maximum number of sg
 * entries was reached or a terminating sg (sg_last(sg) == true) was reached.
 */
struct sg_page_iter {
	struct scatterlist	*sg;		/* sg holding the page */
	unsigned int		sg_pgoffset;	/* page offset within the sg */

	/* these are internal states, keep away */
	unsigned int		__nents;	/* remaining sg entries */
	int			__pg_advance;	/* nr pages to advance at */
						/* the next step */
};


#define __sg_page_iter_next LINUX_BACKPORT(__sg_page_iter_next)
#define __sg_page_iter_start LINUX_BACKPORT(__sg_page_iter_start)
#define sg_page_iter_page LINUX_BACKPORT(sg_page_iter_page)

bool __sg_page_iter_next(struct sg_page_iter *piter);
void __sg_page_iter_start(struct sg_page_iter *piter,
			  struct scatterlist *sglist, unsigned int nents,
			  unsigned long pgoffset);

static inline struct page *sg_page_iter_page(struct sg_page_iter *piter)
{
	return nth_page(sg_page(piter->sg), piter->sg_pgoffset);
}

/**
 * for_each_sg_page - iterate over the pages of the given sg list
 * @sglist:	sglist to iterate over
 * @piter:	page iterator to hold current page, sg, sg_pgoffset
 * @nents:	maximum number of sg entries to iterate over
 * @pgoffset:	starting page offset
 *
 * Callers may use sg_page_iter_page() to get each page pointer.
 */
#define for_each_sg_page(sglist, piter, nents, pgoffset)		   \
	for (__sg_page_iter_start((piter), (sglist), (nents), (pgoffset)); \
	     __sg_page_iter_next(piter);)


#endif
#endif /* _COMPAT_LINUX_SCATTERLIST_H */
