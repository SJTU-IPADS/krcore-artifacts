#ifndef _COMPAT_LINUX_SCHED_H
#define _COMPAT_LINUX_SCHED_H

#include "../../compat/config.h"

#include_next <linux/sched.h>

#if !defined(HAVE___GET_TASK_COMM_EXPORTED) && !defined(HAVE_GET_TASK_COMM_EXPORTED)

#define __get_task_comm LINUX_BACKPORT(__get_task_comm)
extern char *__get_task_comm(char *to, size_t len, struct task_struct *tsk);
#define backport_get_task_comm(buf, tsk) ({			\
	BUILD_BUG_ON(sizeof(buf) != TASK_COMM_LEN);	\
	__get_task_comm(buf, sizeof(buf), tsk);		\
})
#ifdef get_task_comm
#undef get_task_comm
#endif
#define get_task_comm backport_get_task_comm

#endif

#endif /* _COMPAT_LINUX_SCHED_H */
