#ifndef _MLNX_LINUX_RADIX_TREE_H
#define _MLNX_LINUX_RADIX_TREE_H

#include "../../compat/config.h"

#include_next <linux/radix-tree.h>

#ifndef HAVE_RADIX_TREE_NODE

#ifdef __KERNEL__
#define RADIX_TREE_MAP_SHIFT	(CONFIG_BASE_SMALL ? 4 : 6)
#else
#define RADIX_TREE_MAP_SHIFT	3	/* For more stressful testing */
#endif
#define RADIX_TREE_MAP_SIZE	(1UL << RADIX_TREE_MAP_SHIFT)
#define RADIX_TREE_MAP_MASK	(RADIX_TREE_MAP_SIZE-1)

#define RADIX_TREE_TAG_LONGS	\
	((RADIX_TREE_MAP_SIZE + BITS_PER_LONG - 1) / BITS_PER_LONG)

#ifdef HAVE_RADIX_TREE_NEXT_CHUNK
struct radix_tree_node {
	unsigned int	path;	/* Offset in parent & height from the bottom */
	unsigned int	count;
	union {
		struct {
			/* Used when ascending tree */
			struct radix_tree_node *parent;
			/* For tree user */
			void *private_data;
		};
		/* Used when freeing node */
		struct rcu_head	rcu_head;
	};
	/* For tree user */
	struct list_head private_list;
	void __rcu	*slots[RADIX_TREE_MAP_SIZE];
	unsigned long	tags[RADIX_TREE_MAX_TAGS][RADIX_TREE_TAG_LONGS];
};
#endif/*HAVE_RADIX_TREE_NEXT_CHUNK*/

#endif /* HAVE_RADIX_TREE_NODE */
#ifndef HAVE_IDR_PRELOAD_EXPORTED
#define idr_preload LINUX_BACKPORT(idr_preload)
extern void idr_preload(gfp_t gfp_mask);
#endif

#ifndef HAVE_RADIX_TREE_NEXT_CHUNK
struct radix_tree_iter {
	unsigned long	index;
	unsigned long	next_index;
	unsigned long	tags;
};

#define RADIX_TREE_ITER_TAG_MASK	0x00FF	/* tag index in lower byte */
#define RADIX_TREE_ITER_TAGGED		0x0100	/* lookup tagged slots */
#define RADIX_TREE_ITER_CONTIG		0x0200	/* stop at first hole */

static __always_inline void **
radix_tree_iter_init(struct radix_tree_iter *iter, unsigned long start)
{
	iter->index = 0;
	iter->next_index = start;
	return NULL;
}

#define radix_tree_next_chunk LINUX_BACKPORT(radix_tree_next_chunk)
void **radix_tree_next_chunk(struct radix_tree_root *root,
			     struct radix_tree_iter *iter, unsigned flags);


static __always_inline unsigned
radix_tree_chunk_size(struct radix_tree_iter *iter)
{
	return iter->next_index - iter->index;
}


static __always_inline void **
radix_tree_next_slot(void **slot, struct radix_tree_iter *iter, unsigned flags)
{
	if (flags & RADIX_TREE_ITER_TAGGED) {
		iter->tags >>= 1;
		if (likely(iter->tags & 1ul)) {
			iter->index++;
			return slot + 1;
		}
		if (!(flags & RADIX_TREE_ITER_CONTIG) && likely(iter->tags)) {
			unsigned offset = __ffs(iter->tags);

			iter->tags >>= offset;
			iter->index += offset + 1;
			return slot + offset + 1;
		}
	} else {
		unsigned size = radix_tree_chunk_size(iter) - 1;

		while (size--) {
			slot++;
			iter->index++;
			if (likely(*slot))
				return slot;
			if (flags & RADIX_TREE_ITER_CONTIG)
				break;
		}
	}
	return NULL;
}

#define radix_tree_for_each_chunk(slot, root, iter, start, flags)	\
	for (slot = radix_tree_iter_init(iter, start) ;			\
	      (slot = radix_tree_next_chunk(root, iter, flags)) ;)

#define radix_tree_for_each_chunk_slot(slot, iter, flags)		\
	for (; slot ; slot = radix_tree_next_slot(slot, iter, flags))

#define radix_tree_for_each_slot(slot, root, iter, start)		\
	for (slot = radix_tree_iter_init(iter, start) ;			\
	     slot || (slot = radix_tree_next_chunk(root, iter, 0)) ;	\
	     slot = radix_tree_next_slot(slot, iter, 0))

#define radix_tree_for_each_contig(slot, root, iter, start)		\
	for (slot = radix_tree_iter_init(iter, start) ;			\
	     slot || (slot = radix_tree_next_chunk(root, iter,		\
				RADIX_TREE_ITER_CONTIG)) ;		\
	     slot = radix_tree_next_slot(slot, iter,			\
				RADIX_TREE_ITER_CONTIG))

#define radix_tree_for_each_tagged(slot, root, iter, start, tag)	\
	for (slot = radix_tree_iter_init(iter, start) ;			\
	     slot || (slot = radix_tree_next_chunk(root, iter,		\
			      RADIX_TREE_ITER_TAGGED | tag)) ;		\
	     slot = radix_tree_next_slot(slot, iter,			\
				RADIX_TREE_ITER_TAGGED))
#endif /* HAVE_RADIX_TREE_NEXT_CHUNK */

#ifndef HAVE_RADIX_TREE_IS_INTERNAL
#define RADIX_TREE_ENTRY_MASK           3UL
#define RADIX_TREE_INTERNAL_NODE        2UL
static inline bool radix_tree_is_internal_node(void *ptr)
{
	return ((unsigned long)ptr & RADIX_TREE_ENTRY_MASK) ==
		RADIX_TREE_INTERNAL_NODE;
}
#endif
#endif /* _MLNX_LINUX_RADIX_TREE_H */
