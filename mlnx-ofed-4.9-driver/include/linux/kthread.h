/*
 * Simple work processor based on kthread.
 *
 * This provides easier way to make use of kthreads.  A kthread_work
 * can be queued and flushed using queue/flush_kthread_work()
 * respectively.  Queued kthread_works are processed by a kthread
 * running kthread_worker_fn().
 *
 * A kthread_work can't be freed while it is executing.
 */
#ifndef BACKPORT_LINUX_KTHREAD_H
#define BACKPORT_LINUX_KTHREAD_H

#include <linux/version.h>
#include "../../compat/config.h"

#include_next <linux/kthread.h>

#ifndef HAVE_KTHREAD_WORK

struct kthread_work;
typedef void (*kthread_work_func_t)(struct kthread_work *work);

struct kthread_worker {
	spinlock_t		lock;
	struct list_head	work_list;
	struct task_struct	*task;
};

struct kthread_work {
	struct list_head	node;
	kthread_work_func_t	func;
	wait_queue_head_t	done;
	atomic_t		flushing;
	int			queue_seq;
	int			done_seq;
};

#define KTHREAD_WORKER_INIT(worker)	{				\
	.lock = __SPIN_LOCK_UNLOCKED((worker).lock),			\
	.work_list = LIST_HEAD_INIT((worker).work_list),		\
	}

#define KTHREAD_WORK_INIT(work, fn)	{				\
	.node = LIST_HEAD_INIT((work).node),				\
	.func = (fn),							\
	.done = __WAIT_QUEUE_HEAD_INITIALIZER((work).done),		\
	.flushing = ATOMIC_INIT(0),					\
	}

#define DEFINE_KTHREAD_WORKER(worker)					\
	struct kthread_worker worker = KTHREAD_WORKER_INIT(worker)

#define DEFINE_KTHREAD_WORK(work, fn)					\
	struct kthread_work work = KTHREAD_WORK_INIT(work, fn)

/*
 * kthread_worker.lock and kthread_work.done need their own lockdep class
 * keys if they are defined on stack with lockdep enabled.  Use the
 * following macros when defining them on stack.
 */
#ifdef CONFIG_LOCKDEP
# define KTHREAD_WORKER_INIT_ONSTACK(worker)				\
	({ init_kthread_worker(&worker); worker; })
# define DEFINE_KTHREAD_WORKER_ONSTACK(worker)				\
	struct kthread_worker worker = KTHREAD_WORKER_INIT_ONSTACK(worker)
# define KTHREAD_WORK_INIT_ONSTACK(work, fn)				\
	({ init_kthread_work((&work), fn); work; })
# define DEFINE_KTHREAD_WORK_ONSTACK(work, fn)				\
	struct kthread_work work = KTHREAD_WORK_INIT_ONSTACK(work, fn)
#else
# define DEFINE_KTHREAD_WORKER_ONSTACK(worker) DEFINE_KTHREAD_WORKER(worker)
# define DEFINE_KTHREAD_WORK_ONSTACK(work, fn) DEFINE_KTHREAD_WORK(work, fn)
#endif

#define __init_kthread_worker LINUX_BACKPORT(__init_kthread_worker)
extern void __init_kthread_worker(struct kthread_worker *worker,
			const char *name, struct lock_class_key *key);

#define init_kthread_worker(worker)					\
	do {								\
		static struct lock_class_key __key;			\
		__init_kthread_worker((worker), "("#worker")->lock", &__key); \
	} while (0)

#define init_kthread_work(work, fn)					\
	do {								\
		memset((work), 0, sizeof(struct kthread_work));		\
		INIT_LIST_HEAD(&(work)->node);				\
		(work)->func = (fn);					\
		init_waitqueue_head(&(work)->done);			\
	} while (0)

#define kthread_worker_fn LINUX_BACKPORT(kthread_worker_fn)
int kthread_worker_fn(void *worker_ptr);

#define queue_kthread_work LINUX_BACKPORT(queue_kthread_work)
bool queue_kthread_work(struct kthread_worker *worker,
			struct kthread_work *work);
#define flush_kthread_work LINUX_BACKPORT(flush_kthread_work)
void flush_kthread_work(struct kthread_work *work);
#define flush_kthread_worker LINUX_BACKPORT(flush_kthread_worker)
void flush_kthread_worker(struct kthread_worker *worker);

#endif /* LINUX_VERSION_CODE < 2.6.35 */

#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,38))
/*
 * Kernels between 2.6.36 and 2.6.38 have the above functions but still lack the
 * following.
 */
#define kthread_create_on_node(threadfn, data, node, namefmt, arg...) \
	kthread_create(threadfn, data, namefmt, ##arg)

#endif /* HAVE_KTHREAD_WORK */

#endif /* _LINUX_KTHREAD_H */

