#ifndef LINUX_3_16_COMPAT_H
#define LINUX_3_16_COMPAT_H

#include <linux/version.h>

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,16,0))

#include <linux/cpumask.h>

#define cpumask_set_cpu_local_first LINUX_BACKPORT(cpumask_set_cpu_local_first)

#if NR_CPUS == 1
static inline int cpumask_set_cpu_local_first(int i, int numa_node, cpumask_t *dstp)
{
	set_bit(0, cpumask_bits(dstp));

	return 0;
}
#else
int cpumask_set_cpu_local_first(int i, int numa_node, cpumask_t *dstp);
#endif

#include <linux/ktime.h>

#ifndef smp_mb__after_atomic
#define smp_mb__after_atomic()	smp_mb()
#endif

#ifndef smp_mb__before_atomic
#define smp_mb__before_atomic()	smp_mb()
#endif

#define RPC_CWNDSHIFT		(8U)
#define RPC_CWNDSCALE		(1U << RPC_CWNDSHIFT)
#define RPC_INITCWND		RPC_CWNDSCALE
#define RPC_MAXCWND(xprt)	((xprt)->max_reqs << RPC_CWNDSHIFT)
#define RPCXPRT_CONGESTED(xprt) ((xprt)->cong >= (xprt)->cwnd)

#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(3,16,0)) */

#endif /* LINUX_3_16_COMPAT_H */
