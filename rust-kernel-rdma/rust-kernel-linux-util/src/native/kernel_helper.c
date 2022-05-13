#include "kernel_helper.h"

u64
bd_virt_to_phys(void* kaddr)
{
    return virt_to_phys(kaddr);
}

u64
bd_phys_to_virt(void* kaddr)
{
    return (u64)phys_to_virt((phys_addr_t)kaddr);
}

u64
bd_cpu_to_be64(u64 v)
{
    return cpu_to_be64(v);
}

struct task_struct*
bd_kthread_run(int (*threadfn)(void* data), void* data, const char* namefmt)
{
    return kthread_run(threadfn, data, namefmt);
}

struct task_struct *
bd_kthread_create(int (*threadfn)(void *data), void *data, const char *namefmt)
{
    return kthread_create(threadfn, data, namefmt);
}

int
bd_wake_up_process(struct task_struct *t)
{
    return wake_up_process(t);
}

int
bd_kthread_stop(struct task_struct *k)
{
    return kthread_stop(k);
}

bool
bd_kthread_should_stop(void)
{
    return kthread_should_stop();
}

void
bd_kthread_bind(struct task_struct *p, unsigned int cpu)
{
    kthread_bind(p, cpu);
}

void
bd_schedule(void)
{
    schedule();
}

u64
get_hz(void)
{
    return HZ;
}

void
bd_init_completion(struct completion* x)
{
    init_completion(x);
}

u64
bd__builtin_bswap64(u64 v)
{
    return __builtin_bswap64(v);
}

void
bd_ssleep(unsigned int seconds)
{
    ssleep(seconds);
}

void
bd_rwlock_init(rwlock_t* lock)
{
  rwlock_init(lock);
}

void
bd_read_lock(rwlock_t* lock)
{
  read_lock(lock);
}

void
bd_read_unlock(rwlock_t* lock)
{
  read_unlock(lock);
}

void
bd_write_lock(rwlock_t* lock)
{
  write_lock(lock);
}

void
bd_write_unlock(rwlock_t* lock)
{
  write_unlock(lock);
}

int
bd_get_cpu(void)
{
    return get_cpu();
}

unsigned int
bd_get_cpu_id(void)
{
    return smp_processor_id();
}