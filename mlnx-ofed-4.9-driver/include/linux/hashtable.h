#ifndef COMPAT_LINUX_HASHTABLE_H
#define COMPAT_LINUX_HASHTABLE_H

#include "../../compat/config.h"

#ifdef HAVE_LINUX_HASHTABLE_H
#include_next <linux/hashtable.h>
#endif

#ifndef DECLARE_HASHTABLE
#include <linux/types.h>
#define DECLARE_HASHTABLE(name, bits)                                   	\
	struct hlist_head name[1 << (bits)]
#endif

#ifndef hash_init
#include <linux/types.h>
#include <linux/list.h>
#include <linux/hash.h>

#define HASH_SIZE(name) (ARRAY_SIZE(name))
static inline void __hash_init(struct hlist_head *ht, unsigned int sz)
{
	unsigned int i;

	for (i = 0; i < sz; i++)
		INIT_HLIST_HEAD(&ht[i]);
}

/**
 * hash_init - initialize a hash table
 * @hashtable: hashtable to be initialized
 *
 * Calculates the size of the hashtable from the given parameter, otherwise
 * same as hash_init_size.
 *
 * This has to be a macro since HASH_BITS() will not work on pointers since
 * it calculates the size during preprocessing.
 */
#define hash_init(hashtable) __hash_init(hashtable, HASH_SIZE(hashtable))

#define HASH_SIZE(name) (ARRAY_SIZE(name))
#define HASH_BITS(name) ilog2(HASH_SIZE(name))
/* Use hash_32 when possible to allow for fast 32bit hashing in 64bit kernels. */
#define hash_min(val, bits)							\
	(sizeof(val) <= 4 ? hash_32(val, bits) : hash_long(val, bits))

#define hash_for_each_safe(name, bkt, node, tmp, obj, member)                   \
	for ((bkt) = 0, node = NULL; node == NULL && (bkt) < HASH_SIZE(name); (bkt)++)\
		hlist_for_each_entry_safe(obj, node, tmp, &name[bkt], member)

#define hash_for_each_possible(name, obj, node, member, key)                   \
	hlist_for_each_entry(obj, node, &name[hash_min(key, HASH_BITS(name))], member)

/**
 * hash_add - add an object to a hashtable
 * @hashtable: hashtable to add to
 * @node: the &struct hlist_node of the object to be added
 * @key: the key of the object to be added
 */
#define hash_add(hashtable, node, key)						\
	hlist_add_head(node, &hashtable[hash_min(key, HASH_BITS(hashtable))])

static inline bool __hash_empty(struct hlist_head *ht, unsigned int sz)
{
	unsigned int i;

	for (i = 0; i < sz; i++)
		if (!hlist_empty(&ht[i]))
			return false;

	return true;
}

/**
 * hash_empty - check whether a hashtable is empty
 * @hashtable: hashtable to check
 *
 * This has to be a macro since HASH_BITS() will not work on pointers since
 * it calculates the size during preprocessing.
 */
#define hash_empty(hashtable) __hash_empty(hashtable, HASH_SIZE(hashtable))

/**
 * hash_del - remove an object from a hashtable
 * @node: &struct hlist_node of the object to remove
 */
static inline void hash_del(struct hlist_node *node)
{
	hlist_del_init(node);
}

static inline void hash_del_rcu(struct hlist_node *node)
{
	        hlist_del_init_rcu(node);
}


static inline bool hash_hashed(struct hlist_node *node)
{
	        return !hlist_unhashed(node);
}

#define hash_add_rcu(hashtable, node, key)                                      \
        hlist_add_head_rcu(node, &hashtable[hash_min(key, HASH_BITS(hashtable))])

#define compat_hash_for_each(name, bkt, obj, member)				\
	for ((bkt) = 0, obj = NULL; obj == NULL && (bkt) < HASH_SIZE(name);\
			(bkt)++)\
		compat_hlist_for_each_entry(obj, &name[bkt], member)

#define compat_hash_for_each_possible_rcu(name, obj, member, key)		\
 	compat_hlist_for_each_entry_rcu(obj, &name[hash_min(key, HASH_BITS(name))], member)

#else /* hash_init */

#ifndef HAVE_HLIST_FOR_EACH_ENTRY_3_PARAMS
#define compat_hash_for_each(hash, bkt, e, node) \
	hash_for_each(hash, bkt, hlnode, e, node)
#define compat_hash_for_each_possible_rcu(hash, bkt, e, node) \
	hash_for_each_possible_rcu(hash, bkt, hlnode, e, node)
#else
#define compat_hash_for_each hash_for_each
#define compat_hash_for_each_possible_rcu hash_for_each_possible_rcu
#endif

#endif /* hash_init */

#endif /* COMPAT_LINUX_HASHTABLE_H */
