#ifndef FIT_LIB_H
#define FIT_LIB_H

#define _GNU_SOURCE
#include <unistd.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <malloc.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/syscall.h>
#include <linux/unistd.h>

#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdarg.h>
#include <pthread.h>
#include <time.h>

#include "compiler.h"

/*
 * Grabbed from:
 * arch/x86/include/generated/uapi/asm/unistd_64.h
 */

#define __NR_lite_remote_memset 400
#define __NR_lite_fetch_add 401
#define __NR_lite_rdma_synwrite 402
#define __NR_lite_rdma_read 403
#define __NR_lite_ask_lmr 404
#define __NR_lite_rdma_asywrite 405
#define __NR_lite_dist_barrier 406
#define __NR_lite_add_ask_mr_table 407
#define __NR_lite_compare_swp 408
#define __NR_lite_alloc_remote 409
#define __NR_lite_umap_lmr 410
#define __NR_lite_register_application 411
#define __NR_lite_unregister_application 412
#define __NR_lite_receive_message 413
#define __NR_lite_send_reply_imm 414
#define __NR_lite_reply_message 415
#define __NR_lite_get_node_id 416
#define __NR_lite_query_port 417
#define __NR_lite_alloc_continuous_memory 418
#define __NR_lite_wrap_alloc_for_remote_access 419
#define __NR_lite_create_lock 420
#define __NR_lite_ask_lock 421
#define __NR_lite_lock 422
#define __NR_lite_unlock 423
#define __NR_lite_get_total_node 424
#define __NR_lite_reply_and_receive_message 425
#define __NR_lite_join 426
#define __NR_lite_rdma_read_async 427

#define max(x, y)				\
({						\
	x > y ? x : y;		\
})

#define IMM_SEND_ONLY_FLAG      0xffffffffffffffff

struct lmr_info {
	//struct ib_device	*context;
	//struct ib_pd		*pd;
	void			*addr;
	size_t			length;
	//uint32_t		handle;
	uint32_t		lkey;
	uint32_t		rkey;
	uint32_t		node_id;
};
struct lite_lock_form{
	int lock_num;
	struct lmr_info lock_mr;
	uint64_t ticket_num;
};
typedef struct lite_lock_form remote_spinlock_t;

struct reply_struct{
        void *addr;
        int size;
        uintptr_t descriptor;
};

struct receive_struct{
        unsigned int designed_port;
        void *ret_addr;
        int receive_size;
        void *descriptor;
        int block_call;
};

#define __ACTIVE_NODES	3
#define LIMITATION 1024*1024*4
#define PAGE_SHIFT 12

#define IMM_MAX_PORT 64
#define IMM_MAX_PORT_BIT 6
#define IMM_MAX_PRIORITY 64
#define IMM_MAX_PRIORITY_BIT 6

#define SEND_REPLY_WAIT (-101)

/* Added by Yizhou, to impl async rdma read without adding another syscall */
#define ASYNC_RDMA_READ_IN_PASSWORD	(0x10000000)

#define CHECK_LENGTH 100000

#define USERSPACE_HIGH_PRIORITY 16
#define USERSPACE_LOW_PRIORITY 17
#define NULL_PRIORITY 0

enum permission_mode{
	MR_READ_FLAG=0x01,
	MR_WRITE_FLAG=0x02,
	MR_SHARE_FLAG=0x04,
	MR_ADMIN_FLAG=0x08,
	MR_ATOMIC_FLAG=0x10,
	MR_ASK_SUCCESS=0,
	MR_ASK_REFUSE=1,
	MR_ASK_UNPERMITTED=2,
	MR_ASK_HANDLER_ERROR=3,
	MR_ASK_UNKNOWN=4
};

#define BLOCK_CALL 1

#include <sys/syscall.h>
#include <linux/unistd.h>
#include <asm/unistd.h>

inline int userspace_liteapi_dist_barrier(unsigned int num)
{
    return syscall(__NR_lite_dist_barrier, num);
}

inline int userspace_liteapi_alloc_remote_mem(unsigned int node_id, unsigned int size, bool atomic_flag, int password)
{
    return syscall(__NR_lite_alloc_remote, node_id, size, atomic_flag, password);
}

inline int userspace_liteapi_register_application(unsigned int destined_port, unsigned int max_size_per_message, unsigned int max_user_per_node, char *name, uint64_t name_len)
{
    return syscall(__NR_lite_register_application, destined_port, max_size_per_message, max_user_per_node, name, name_len);
}

inline int userspace_liteapi_receive_message(unsigned int port, void *ret_addr, int receive_size, uintptr_t *descriptor, int block_call)
{
    int ret;
    //ret = syscall(__NR_lite_receive_message, receive_size*IMM_MAX_PORT+port, ret_addr, descriptor, 0, block_call, NULL_PRIORITY);
    ret = syscall(__NR_lite_receive_message, (receive_size<<IMM_MAX_PORT_BIT)+port, ret_addr, descriptor, 0, block_call, NULL_PRIORITY);
    return ret;
}

inline int userspace_liteapi_receive_message_high(unsigned int port, void *ret_addr, int receive_size, uintptr_t *descriptor, int block_call)
{
    int ret;
    //ret = syscall(__NR_lite_receive_message, receive_size*IMM_MAX_PORT+port, ret_addr, descriptor, 0, block_call, USERSPACE_HIGH_PRIORITY);
    ret = syscall(__NR_lite_receive_message, (receive_size<<IMM_MAX_PORT_BIT)+port, ret_addr, descriptor, 0, block_call, USERSPACE_HIGH_PRIORITY);
    return ret;
}
inline int userspace_liteapi_receive_message_low(unsigned int port, void *ret_addr, int receive_size, uintptr_t *descriptor, int block_call)
{
    int ret;
    //ret = syscall(__NR_lite_receive_message, receive_size*IMM_MAX_PORT+port, ret_addr, descriptor, 0, block_call, USERSPACE_LOW_PRIORITY);
    ret = syscall(__NR_lite_receive_message, (receive_size<<IMM_MAX_PORT_BIT)+port, ret_addr, descriptor, 0, block_call, USERSPACE_LOW_PRIORITY);
    return ret;
}

int userspace_liteapi_receive_message_fast(unsigned int port, void *ret_addr, int receive_size,
                                           uintptr_t *descriptor, int *ret_length, int block_call)
{
    int ret;
    int i=0;

    *descriptor = 0;

    ret = syscall(__NR_lite_receive_message, (receive_size<<IMM_MAX_PORT_BIT)+port,
                  ret_addr, descriptor, ret_length, block_call, NULL_PRIORITY);

    while(*(descriptor)==0 && ++i<CHECK_LENGTH);

    while(*(descriptor)==0)
    {
        usleep(1);
    };

    if(*(descriptor) == IMM_SEND_ONLY_FLAG)
        *(descriptor) = 0;

    if(ret<0)
        return *ret_length;
    return ret;
}
int record_flag = 0;
inline double userspace_liteapi_receive_message_fast_record(unsigned int port, void *ret_addr, int receive_size, uintptr_t *descriptor, int *ret_length, int block_call)
{
    int ret;
    userspace_liteapi_receive_message_fast(port, ret_addr, receive_size, descriptor, ret_length, block_call);
    return ret;
}

int
userspace_liteapi_send(int target_node, unsigned int port, void *addr, int size)
{
    if(size >= LIMITATION)
    {
        printf("%s: size %d too big\n", __func__, size);
        return -1;
    }
    return syscall(__NR_lite_send_reply_imm, target_node, (size<<IMM_MAX_PORT_BIT)+port, addr, 0, 0, 0);
}

int
userspace_liteapi_send_reply_imm(int target_node, unsigned int port, void *addr,
                                 int size, void *ret_addr, int max_ret_size)
{
    if(size >= LIMITATION || max_ret_size >= LIMITATION)
    {
        printf("%s: size %d max_ret_size %d too big\n", __func__, size, max_ret_size);
        return -1;
    }
    return syscall(__NR_lite_send_reply_imm, target_node, (size<<IMM_MAX_PORT_BIT)+port,
                   addr, ret_addr, 0, max_ret_size);
}

int
userspace_liteapi_send_reply_imm_high(int target_node, unsigned int port, void *addr,
                                      int size, void *ret_addr, int max_ret_size)
{
    if(size >= LIMITATION || max_ret_size >= LIMITATION)
    {
        printf("%s: size %d max_ret_size %d too big\n", __func__, size, max_ret_size);
        return -1;
    }
    return syscall(__NR_lite_send_reply_imm, target_node, (size<<IMM_MAX_PORT_BIT)+port,
                   addr, ret_addr, 0, (max_ret_size<<IMM_MAX_PRIORITY_BIT)+USERSPACE_HIGH_PRIORITY);
}

int
userspace_liteapi_send_reply_imm_low(int target_node, unsigned int port, void *addr,
                                     int size, void *ret_addr, int max_ret_size)
{
    if(size >= LIMITATION || max_ret_size >= LIMITATION)
    {
        printf("%s: size %d max_ret_size %d too big\n", __func__, size, max_ret_size);
        return -1;
    }
    return syscall(__NR_lite_send_reply_imm, target_node, (size<<IMM_MAX_PORT_BIT)+port,
                   addr, ret_addr, 0, (max_ret_size<<IMM_MAX_PRIORITY_BIT)+USERSPACE_LOW_PRIORITY);
}

/*
 * This is the version with RPC syscall optimizations.
 */
int
userspace_liteapi_send_reply_imm_fast(int target_node, unsigned int port, void *addr,
                                      int size, void *ret_addr, int *ret_length, int max_ret_size)
{
    int ret;

    if(size >= LIMITATION || max_ret_size >= LIMITATION) {
        printf("%s: size %d max_ret_size %d too big\n", __func__, size, max_ret_size);
        return -1;
    }

    ret = syscall(__NR_lite_send_reply_imm, target_node, (size<<IMM_MAX_PORT_BIT)+port,
                  addr, ret_addr, ret_length, (max_ret_size<<IMM_MAX_PRIORITY_BIT)+NULL_PRIORITY);
    if (ret < 0)
        printf("[significant error] error in fast send setup %d\n", ret);

    /*
     * This is where userspace poll when a RPC finished.
     */
    while (*ret_length == SEND_REPLY_WAIT) {
        ;
    }

    if (*ret_length < 0)
        printf("[significant error] error in fast send %d\n", *ret_length);
    return *ret_length;
}

int userspace_liteapi_reply_message(void *addr, int size, uintptr_t descriptor)
{
    if(size >= LIMITATION)
    {
        printf("%s: size %d too big\n", __func__, size);
        return -1;
    }
    return syscall(__NR_lite_reply_message, addr, size, descriptor, NULL_PRIORITY);
}

int userspace_liteapi_reply_message_high(void *addr, int size, uintptr_t descriptor)
{
    if(size >= LIMITATION) {
        printf("%s: size %d too big\n", __func__, size);
        return -1;
    }
    return syscall(__NR_lite_reply_message, addr, size, descriptor, USERSPACE_HIGH_PRIORITY);
}

int userspace_liteapi_reply_message_low(void *addr, int size, uintptr_t descriptor)
{
    if(size >= LIMITATION)
    {
        printf("%s: size %d too big\n", __func__, size);
        return -1;
    }
    return syscall(__NR_lite_reply_message, addr, size, descriptor, USERSPACE_LOW_PRIORITY);
}

inline int userspace_liteapi_query_port(int target_node, int designed_port)
{
    return syscall(__NR_lite_query_port, target_node, designed_port, 0);
}

inline int userspace_liteapi_ask_lmr(int memory_node, uint64_t identifier, uint64_t permission, int password)
{
    return syscall(__NR_lite_ask_lmr, memory_node, identifier, permission, password);
}

int userspace_liteapi_get_node_id(void)
{
    return syscall(__NR_lite_get_node_id);
}

inline int userspace_liteapi_get_total_node(void)
{
    return syscall(__NR_lite_get_total_node);
}

inline int userspace_liteapi_rdma_write(unsigned lite_handler, void *local_addr, unsigned int size, unsigned int offset, int password)
{
    return syscall(__NR_lite_rdma_synwrite, lite_handler, local_addr, size, NULL_PRIORITY, offset, password);
}

inline int userspace_liteapi_rdma_write_high(unsigned lite_handler, void *local_addr, unsigned int size, unsigned int offset, int password)
{
    return syscall(__NR_lite_rdma_synwrite, lite_handler, local_addr, size, USERSPACE_HIGH_PRIORITY, offset, password);
}

inline int userspace_liteapi_rdma_write_low(unsigned lite_handler, void *local_addr, unsigned int size, unsigned int offset, int password)
{
    return syscall(__NR_lite_rdma_synwrite, lite_handler, local_addr, size, USERSPACE_LOW_PRIORITY, offset, password);
}

inline int userspace_liteapi_rdma_read(unsigned lite_handler, void *local_addr, unsigned int size, unsigned int offset, int password)
{
    return syscall(__NR_lite_rdma_read, lite_handler, local_addr, size, NULL_PRIORITY, offset, password);
}

inline int userspace_liteapi_rdma_read_high(unsigned lite_handler, void *local_addr, unsigned int size, unsigned int offset, int password)
{
    return syscall(__NR_lite_rdma_read, lite_handler, local_addr, size, USERSPACE_HIGH_PRIORITY, offset, password);
}

inline int userspace_liteapi_rdma_read_low(unsigned lite_handler, void *local_addr, unsigned int size, unsigned int offset, int password)
{
    return syscall(__NR_lite_rdma_read, lite_handler, local_addr, size, USERSPACE_LOW_PRIORITY, offset, password);
}

int async_rdma_read(unsigned lite_handler, void *local_addr, unsigned int size,
                    unsigned int offset, int *poll)
{
    return syscall(__NR_lite_rdma_read_async, lite_handler, local_addr, size, NULL_PRIORITY, offset, poll);
}

void* userspace_liteapi_alloc_memory(unsigned long size)
{
    unsigned long roundup_size = (((1<<PAGE_SHIFT) + size - 1)>>PAGE_SHIFT)<<PAGE_SHIFT;

    int fd;
    char *addr;
    fd = open("/dev/lite_mmap",O_RDONLY);

    addr = (char *)mmap(NULL,roundup_size,PROT_READ,MAP_PRIVATE,fd,0);
    if (addr==MAP_FAILED) {
        perror("mmaptest user ");
        return 0;
    };
    //
    //char *addr=malloc(roundup_size);
    //syscall(__NR_lite_alloc_memory, addr, size);
    return addr;
}

inline int userspace_liteapi_create_lock(int target_node, remote_spinlock_t *input)
{
    int ret;
    ret = syscall(__NR_lite_create_lock, target_node, input);
    if(ret>=0)
        return ret;
    else
        printf("create lock error\n");
    return 0;
}

inline int userspace_liteapi_ask_lock(int target_node, int target_idx, remote_spinlock_t *input)
{
    int ret;
    ret = syscall(__NR_lite_ask_lock, target_node, target_idx, input);
    if(ret>=0)
        return ret;
    else
        printf("ask lock error\n");
    return 0;
}

inline int userspace_liteapi_lock(remote_spinlock_t *input)
{
    return syscall(__NR_lite_lock, input);
}

inline int userspace_liteapi_unlock(remote_spinlock_t *input)
{
    return syscall(__NR_lite_unlock, input);
}

inline int userspace_liteapi_remote_memset(unsigned lite_handler, int offset, int size)
{
    return syscall(__NR_lite_remote_memset, lite_handler, offset, size);
}

inline int userspace_liteapi_add_ask_mr_table(uint64_t identifier, uint64_t lmr, uint64_t permission, int password)
{
    return syscall(__NR_lite_add_ask_mr_table, identifier, lmr, permission, password);
}

inline int userspace_liteapi_compare_swp(unsigned long lite_handler, void *local_addr, unsigned long long guess_value, unsigned long long set_value)
{
    return syscall(__NR_lite_compare_swp, lite_handler, local_addr, guess_value, set_value, 0);
}

inline int userspace_liteapi_fetch_add(unsigned long lite_handler, void *local_addr, unsigned long long input_value)
{
    return syscall(__NR_lite_fetch_add, lite_handler, local_addr, input_value, 0);
}

//int userspace_liteapi_reply_and_receive_message(void *addr, int size, uintptr_t descriptor, unsigned int port, void *ret_addr, int receive_size, uintptr_t *receive_descriptor, int block_call)
inline int userspace_liteapi_reply_and_receive_message(void *addr, int size, uintptr_t descriptor, unsigned int port, void *ret_addr, int receive_size, uintptr_t *receive_descriptor)
{
    return syscall(__NR_lite_reply_and_receive_message, addr, size*IMM_MAX_PORT+port, descriptor, ret_addr, receive_size, receive_descriptor);
}

#if 0
inline int userspace_syscall_test(void)
{
        return syscall(__NR_lite_umap_testsyscall, 0);
}
#endif

int userspace_liteapi_join(char *input_str, int eth_port, int ib_port)
{
    char ipstr[32];
    memset(ipstr, 0, 32);
    strcpy(ipstr, input_str);
    return syscall(__NR_lite_join, ipstr, eth_port, ib_port);
}

int stick_this_thread_to_core(int core_id)
{
    int num_cores = sysconf(_SC_NPROCESSORS_ONLN);
    if (core_id < 0 || core_id >= num_cores)
        return EINVAL;

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);

    pthread_t current_thread = pthread_self();
    return pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset);
}

#endif /* FIT_LIB_H */
