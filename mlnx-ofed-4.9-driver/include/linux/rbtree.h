#ifndef _MLNX_LINUX_RBTREE_H
#define _MLNX_LINUX_RBTREE_H

#include "../../compat/config.h"

#include_next <linux/rbtree.h>

#ifndef HAVE_RB_ROOT_CACHED
#define rb_root_cached rb_root
#define RB_ROOT_CACHED RB_ROOT
#endif

#ifndef HAVE_RB_FIRST_POSTORDER
#define rb_first_postorder LINUX_BACKPORT(rb_first_postorder)
extern struct rb_node *rb_first_postorder(const struct rb_root *);
#define rb_next_postorder LINUX_BACKPORT(rb_next_postorder)
extern struct rb_node *rb_next_postorder(const struct rb_node *);
#endif

#ifndef rb_entry_safe
#define rb_entry_safe(ptr, type, member) \
	({ typeof(ptr) ____ptr = (ptr); \
	   ____ptr ? rb_entry(____ptr, type, member) : NULL; \
	})
#endif

#ifndef rbtree_postorder_for_each_entry_safe
#define rbtree_postorder_for_each_entry_safe(pos, n, root, field) \
	for (pos = rb_entry_safe(rb_first_postorder(root), typeof(*pos), field); \
	     pos && ({ n = rb_entry_safe(rb_next_postorder(&pos->field), \
			typeof(*pos), field); 1; }); \
	     pos = n)
#endif

#endif /* _MLNX_LINUX_RBTREE_H */
