#ifndef _MLNX_LINUX_REFCOUNT_H
#define _MLNX_LINUX_REFCOUNT_H

#include "../../compat/config.h"

#ifdef HAVE_REFCOUNT
#include_next <linux/refcount.h>
#else /* HAVE_REFCOUNT */

/* simply map back to atomic interface */

#include <linux/atomic.h>

#define refcount_t		atomic_t
#define refcount_set		atomic_set
#define refcount_inc		atomic_inc
#define refcount_dec		atomic_dec
#define refcount_read		atomic_read
#define refcount_inc_not_zero	atomic_inc_not_zero
#define refcount_dec_and_test	atomic_dec_and_test

static inline bool
refcount_dec_and_mutex_lock(refcount_t *r, struct mutex *lock)
{
	mutex_lock(lock);
	if (!refcount_dec_and_test(r)) {
		mutex_unlock(lock);
		return false;
	}

	return true;
}

#endif /* HAVE_REFCOUNT */


#endif /* _MLNX_LINUX_REFCOUNT_H */
