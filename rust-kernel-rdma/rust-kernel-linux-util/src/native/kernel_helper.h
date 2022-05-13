#ifndef KBUILD_MODNAME
#define KBUILD_MODNAME "rust-kernel-rdma-base"
#endif

// helper headers
#include <linux/bug.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/gfp.h>
#include <linux/moduleparam.h>
#include <linux/printk.h>
#include <linux/scatterlist.h>
#include <linux/uaccess.h>
#include <linux/utsname.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/syscalls.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/kthread.h>
#include <linux/vmalloc.h>

u64
bd_virt_to_phys(void *addr);

u64
bd_phys_to_virt(void* kaddr);

u64
bd_cpu_to_be64(u64 v);

struct task_struct *
bd_kthread_run(int (*threadfn)(void *data), void *data, const char *namefmt);

struct task_struct *
bd_kthread_create(int (*threadfn)(void *data), void *data, const char *namefmt);

int
bd_wake_up_process(struct task_struct *t);

int
bd_kthread_stop(struct task_struct *k);

bool
bd_kthread_should_stop(void);

void
bd_kthread_bind(struct task_struct *p, unsigned int cpu);

void
bd_schedule(void);

u64
get_hz(void);

void
bd_init_completion(struct completion *x);


u64
bd__builtin_bswap64(u64 v);

void
bd_ssleep(unsigned int seconds);

void
bd_rwlock_init(rwlock_t* lock);

void
bd_read_lock(rwlock_t* lock);

void
bd_read_unlock(rwlock_t* lock);

void
bd_write_lock(rwlock_t* lock);

void
bd_write_unlock(rwlock_t* lock);

int
bd_get_cpu(void);

unsigned int
bd_get_cpu_id(void);
