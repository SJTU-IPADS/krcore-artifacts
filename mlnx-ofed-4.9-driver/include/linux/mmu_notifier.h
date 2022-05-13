#ifndef _MLNX_LINUX_MMU_NOTIFIER_H
#define _MLNX_LINUX_MMU_NOTIFIER_H

#include "../../compat/config.h"

#include_next <linux/mmu_notifier.h>

#ifndef HAVE_MMU_NOTIFIER_CALL_SRCU
#define mmu_notifier_call_srcu LINUX_BACKPORT(mmu_notifier_call_srcu)
extern void mmu_notifier_call_srcu(struct rcu_head *rcu, void (*func)(struct rcu_head *rcu));
#endif
#ifndef HAVE_MMU_NOTIFIER_UNREGISTER_NO_RELEASE
#define mmu_notifier_unregister_no_release LINUX_BACKPORT(mmu_notifier_unregister_no_release)
extern void mmu_notifier_unregister_no_release(struct mmu_notifier *mn, struct mm_struct *mm);
#endif

#endif /* _MLNX_LINUX_MMU_NOTIFIER_H */
