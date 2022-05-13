#ifndef COMPAT_LINUX_SCHED_MM_H
#define COMPAT_LINUX_SCHED_MM_H

#include "../../../compat/config.h"

#ifdef HAVE_SCHED_MM_H
#include_next <linux/sched/mm.h>
#endif

#ifdef HAVE_SCHED_H
#include_next <linux/sched.h>
#endif

#ifndef HAVE_MMGET_NOT_ZERO
static inline bool mmget_not_zero(struct mm_struct *mm)
{
	return atomic_inc_not_zero(&mm->mm_users);
}
#endif

#ifndef HAVE_MMGRAB
static inline void mmgrab(struct mm_struct *mm)
{
	atomic_inc(&mm->mm_count);
}
#endif

#ifndef HAVE_MMGET
static inline void mmget(struct mm_struct *mm)
{
	atomic_inc(&mm->mm_users);
}

#endif
#if !defined (HAVE_MMGET_STILL_VALID) && !defined(HAVE_MMGET_STILL_VALID_IN_SCHED_H) && !defined(HAVE_MMGET_STILL_VALID_IN_MM_H)
/*
 * This has to be called after a get_task_mm()/mmget_not_zero()
 * followed by taking the mmap_sem for writing before modifying the
 * vmas or anything the coredump pretends not to change from under it.
 *
 * NOTE: find_extend_vma() called from GUP context is the only place
 * that can modify the "mm" (notably the vm_start/end) under mmap_sem
 * for reading and outside the context of the process, so it is also
 * the only case that holds the mmap_sem for reading that must call
 * this function. Generally if the mmap_sem is hold for reading
 * there's no need of this check after get_task_mm()/mmget_not_zero().
 *
 * This function can be obsoleted and the check can be removed, after
 * the coredump code will hold the mmap_sem for writing before
 * invoking the ->core_dump methods.
 */
static inline bool mmget_still_valid(struct mm_struct *mm)
{
       return likely(!mm->core_state);
}
#endif
#endif /* COMPAT_LINUX_SCHED_MM_H */
