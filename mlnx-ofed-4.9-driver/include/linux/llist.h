#ifndef COMPAT_LINUX_LLIST_H
#define COMPAT_LINUX_LLIST_H

#include <linux/version.h>

#ifndef member_address_is_nonnull
#define member_address_is_nonnull(ptr, member)  \
	((uintptr_t)(ptr) + offsetof(typeof(*(ptr)), member) != 0)
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,2,0))
#include_next <linux/llist.h>

#ifndef llist_for_each_entry_safe
#define llist_for_each_entry_safe(pos, n, node, member)				\
	for (pos = llist_entry((node), typeof(*pos), member);			\
		member_address_is_nonnull(pos, member) &&			\
		(n = llist_entry(pos->member.next, typeof(*n), member), true);	\
		 pos = n)
#endif
#else

#if (LINUX_VERSION_CODE > KERNEL_VERSION(3,1,0)) || (defined(CONFIG_SUSE_KERNEL) && defined(CONFIG_COMPAT_SLES_11_2))
#include_next <linux/llist.h>
#define llist_add_batch LINUX_BACKPORT(llist_add_batch)
extern bool llist_add_batch(struct llist_node *new_first,
			    struct llist_node *new_last,
			    struct llist_head *head);
#define llist_del_first LINUX_BACKPORT(llist_del_first)
extern struct llist_node *llist_del_first(struct llist_head *head);

#ifndef llist_for_each_entry_safe
#define llist_for_each_entry_safe(pos, n, node, member)				\
	for (pos = llist_entry((node), typeof(*pos), member);			\
		member_address_is_nonnull(pos, member) &&			\
		(n = llist_entry(pos->member.next, typeof(*n), member), true);	\
		 pos = n)
#endif
#else
#ifndef LLIST_H
#define LLIST_H
/*
 * Lock-less NULL terminated single linked list
 *
 * If there are multiple producers and multiple consumers, llist_add
 * can be used in producers and llist_del_all can be used in
 * consumers.  They can work simultaneously without lock.  But
 * llist_del_first can not be used here.  Because llist_del_first
 * depends on list->first->next does not changed if list->first is not
 * changed during its operation, but llist_del_first, llist_add,
 * llist_add (or llist_del_all, llist_add, llist_add) sequence in
 * another consumer may violate that.
 *
 * If there are multiple producers and one consumer, llist_add can be
 * used in producers and llist_del_all or llist_del_first can be used
 * in the consumer.
 *
 * This can be summarized as follow:
 *
 *           |   add    | del_first |  del_all
 * add       |    -     |     -     |     -
 * del_first |          |     L     |     L
 * del_all   |          |           |     -
 *
 * Where "-" stands for no lock is needed, while "L" stands for lock
 * is needed.
 *
 * The list entries deleted via llist_del_all can be traversed with
 * traversing function such as llist_for_each etc.  But the list
 * entries can not be traversed safely before deleted from the list.
 * The order of deleted entries is from the newest to the oldest added
 * one.  If you want to traverse from the oldest to the newest, you
 * must reverse the order by yourself before traversing.
 *
 * The basic atomic operation of this list is cmpxchg on long.  On
 * architectures that don't have NMI-safe cmpxchg implementation, the
 * list can NOT be used in NMI handlers.  So code that uses the list in
 * an NMI handler should depend on CONFIG_ARCH_HAVE_NMI_SAFE_CMPXCHG.
 *
 * Copyright 2010,2011 Intel Corp.
 *   Author: Huang Ying <ying.huang@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/kernel.h>
#include <asm/system.h>
#include <asm/processor.h>

struct llist_head {
	struct llist_node *first;
};

struct llist_node {
	struct llist_node *next;
};

#ifndef LLIST_HEAD_INIT
#define LLIST_HEAD_INIT(name)	{ NULL }
#endif
#ifndef LLIST_HEAD
#define LLIST_HEAD(name)	struct llist_head name = LLIST_HEAD_INIT(name)
#endif

/**
 * init_llist_head - initialize lock-less list head
 * @head:	the head for your lock-less list
 */
#define init_llist_head LINUX_BACKPORT(init_llist_head)
static inline void init_llist_head(struct llist_head *list)
{
	list->first = NULL;
}

/**
 * llist_entry - get the struct of this entry
 * @ptr:	the &struct llist_node pointer.
 * @type:	the type of the struct this is embedded in.
 * @member:	the name of the llist_node within the struct.
 */
#ifndef llist_entry
#define llist_entry(ptr, type, member)		\
	container_of(ptr, type, member)
#endif

/**
 * llist_for_each - iterate over some deleted entries of a lock-less list
 * @pos:	the &struct llist_node to use as a loop cursor
 * @node:	the first entry of deleted list entries
 *
 * In general, some entries of the lock-less list can be traversed
 * safely only after being deleted from list, so start with an entry
 * instead of list head.
 *
 * If being used on entries deleted from lock-less list directly, the
 * traverse order is from the newest to the oldest added entry.  If
 * you want to traverse from the oldest to the newest, you must
 * reverse the order by yourself before traversing.
 */
#ifndef llist_for_each
#define llist_for_each(pos, node)			\
	for ((pos) = (node); pos; (pos) = (pos)->next)
#endif

/**
 * llist_for_each_entry - iterate over some deleted entries of lock-less list of given type
 * @pos:	the type * to use as a loop cursor.
 * @node:	the fist entry of deleted list entries.
 * @member:	the name of the llist_node with the struct.
 *
 * In general, some entries of the lock-less list can be traversed
 * safely only after being removed from list, so start with an entry
 * instead of list head.
 *
 * If being used on entries deleted from lock-less list directly, the
 * traverse order is from the newest to the oldest added entry.  If
 * you want to traverse from the oldest to the newest, you must
 * reverse the order by yourself before traversing.
 */
#ifndef llist_for_each_entry
#define llist_for_each_entry(pos, node, member)				\
	for ((pos) = llist_entry((node), typeof(*(pos)), member);	\
	     &(pos)->member != NULL;					\
	     (pos) = llist_entry((pos)->member.next, typeof(*(pos)), member))
#endif

#ifndef llist_for_each_entry_safe
#define llist_for_each_entry_safe(pos, n, node, member)				\
	for (pos = llist_entry((node), typeof(*pos), member);			\
		member_address_is_nonnull(pos, member) &&			\
		(n = llist_entry(pos->member.next, typeof(*n), member), true);	\
		 pos = n)
#endif
/**
 * llist_empty - tests whether a lock-less list is empty
 * @head:	the list to test
 *
 * Not guaranteed to be accurate or up to date.  Just a quick way to
 * test whether the list is empty without deleting something from the
 * list.
 */
#define llist_empty LINUX_BACKPORT(llist_empty)
static inline bool llist_empty(const struct llist_head *head)
{
	return ACCESS_ONCE(head->first) == NULL;
}

#define llist_next LINUX_BACKPORT(llist_next)
static inline struct llist_node *llist_next(struct llist_node *node)
{
	return node->next;
}

/**
 * llist_add - add a new entry
 * @new:	new entry to be added
 * @head:	the head for your lock-less list
 *
 * Returns true if the list was empty prior to adding this entry.
 */
#define llist_add LINUX_BACKPORT(llist_add)
static inline bool llist_add(struct llist_node *new, struct llist_head *head)
{
	struct llist_node *entry, *old_entry;

	entry = head->first;
	for (;;) {
		old_entry = entry;
		new->next = entry;
		entry = cmpxchg(&head->first, old_entry, new);
		if (entry == old_entry)
			break;
	}

	return old_entry == NULL;
}

/**
 * llist_del_all - delete all entries from lock-less list
 * @head:	the head of lock-less list to delete all entries
 *
 * If list is empty, return NULL, otherwise, delete all entries and
 * return the pointer to the first entry.  The order of entries
 * deleted is from the newest to the oldest added one.
 */
#define llist_del_all LINUX_BACKPORT(llist_del_all)
static inline struct llist_node *llist_del_all(struct llist_head *head)
{
	return xchg(&head->first, NULL);
}

#define llist_add_batch LINUX_BACKPORT(llist_add_batch)
extern bool llist_add_batch(struct llist_node *new_first,
			    struct llist_node *new_last,
			    struct llist_head *head);

#define llist_del_first LINUX_BACKPORT(llist_del_first)
extern struct llist_node *llist_del_first(struct llist_head *head);

#endif /* (LINUX_VERSION_CODE > KERNEL_VERSION(3,1,0)) || (defined(CONFIG_SUSE_KERNEL) && defined(CONFIG_COMPAT_SLES_11_2)) */
#endif /* LLIST_H */
#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(3,2,0) */

#endif /* COMPAT_LINUX_LLIST_H */
