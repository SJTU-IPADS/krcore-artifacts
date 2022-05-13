/*
 * Copyright (c) 2018 Yizhou Shan <ys@purdue.edu>. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#define _GNU_SOURCE
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <pthread.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/syscall.h>
#include <stdbool.h>
#include <malloc.h>
#include <getopt.h>
#include <sched.h>
#include "lite-lib.h"

#define MAX_BUF_SIZE	(1024 * 1024 * 4)

/*
 * lite rpc max port: 64
 * each thread pair need 2 ports
 */
struct thread_info {
	/*
	 * inbound_port is local's property, which is used by local to receive.
	 * outbound_port is remote's property, which is used by remote to receive
	 */
	int inbound_port;
	int outbound_port;

	int tid;
	int remote_nid;

	int nr_async_recorded;
	double total_async_lat;
};

static bool record_async_latency = false;
static unsigned int nr_threads = 1;
static double *per_rps;
static pthread_barrier_t thread_barrier;

#define NSEC_PER_SEC	(1000*1000*1000)
static inline long timespec_diff_ns(struct timespec end, struct timespec start)
{
	long e, s;

	e = end.tv_sec * NSEC_PER_SEC + end.tv_nsec;
	s = start.tv_sec * NSEC_PER_SEC + start.tv_nsec;
	return e - s;
}

static int bind_thread(int cpu_id)
{
	cpu_set_t cpu_set;

	CPU_ZERO(&cpu_set);
	CPU_SET(cpu_id, &cpu_set);

	return sched_setaffinity(0, sizeof (cpu_set), &cpu_set);
}

/*
 * ret_buf is _not_ guranteed to be ready upon return.
 * Caller is responsible for polling *ret_length for completion.
 *
 * When the underlying syscall returned, it only guranteed that
 * the send WQE has been posted to senq queue. It does not gurantee
 * the buffer has been set out by NIC.
 *
 * That means, caller can not free, nor reuse @buf. Otherwise, the
 * data that will be sent out by NIC, might be corrupted.
 *
 * You are safe to reuse @buf after async_rpc_completed() returns true.
 */
int async_rpc(int dst_nid, int dst_port, void *buf, int buf_size,
	      void *ret_buf, int *ret_size_ptr, int max_ret_size)
{
	int ret;

	if (buf_size >= MAX_BUF_SIZE || max_ret_size >= MAX_BUF_SIZE) {
		fprintf(stderr, "%s: buf_size %d max_ret_size %d too big\n",
			__func__, buf_size, max_ret_size);
		return -EINVAL;
	}

	ret = syscall(__NR_lite_send_reply_imm,
			dst_nid,
			(buf_size << IMM_MAX_PORT_BIT) + dst_port,
			buf, ret_buf, ret_size_ptr,
			(max_ret_size << IMM_MAX_PRIORITY_BIT) + NULL_PRIORITY);
	if (ret < 0)
		perror("lite_send_reply syscall failed");
	return 0;
}

/*
 * Return true if the @poll point indicate the
 * async RPC has completed.
 */
static inline bool async_rpc_completed(int *poll)
{
	if (*poll == SEND_REPLY_WAIT)
		return false;
	return true;
}

#define WAIT_COMPLETION_TIMEOUT_S	(5)

static inline void wait_for_completion(int *poll)
{
	time_t start;
	bool saved_ts = false;

	while (!async_rpc_completed(poll)) {
		time_t now;

		if (!saved_ts) {
			start = time(NULL);
			saved_ts = true;
		}

		now = time(NULL);
		if (now - start > WAIT_COMPLETION_TIMEOUT_S) {
			printf("%s: timeout (%d s) at poll %p\n",
				__func__, WAIT_COMPLETION_TIMEOUT_S, poll);
		}
	}
}

/*
 * @NR_OUTSTANDING_ASYNC_RPC is the maximum outstanding a-sync rpc requests.
 * Every these number of a-sync request, we need to check poll.
 */
#define ASYNC_BATCH_LIMIT	(1024)
static unsigned int async_batch_size = 32;

void test_sync_rpc_send(struct thread_info *info, int NR_SYNC_RPC)
{
	int *poll_array;
	int i, ret;
	char *read, *write;
	struct timespec start, end;
	long diff_ns;
	double rps;

	read = memalign(sysconf(_SC_PAGESIZE), 4096 * 2);
	write = memalign(sysconf(_SC_PAGESIZE), 4096 * 2);
	poll_array = memalign(sysconf(_SC_PAGESIZE), sizeof(int));
	memset(poll_array, 0, sizeof(int));

	mlock(read, 4096);
	mlock(write, 4096);
	mlock(poll_array, sizeof(int));

	clock_gettime(CLOCK_MONOTONIC, &start);
	for (i = 0; i < NR_SYNC_RPC; i++) {
		/*
		 * send 4 bytes 
		 */
		userspace_liteapi_send_reply_imm_fast(info->remote_nid,
			info->outbound_port, write, 4, read, poll_array, 4096);
	}
	clock_gettime(CLOCK_MONOTONIC, &end);

	diff_ns = timespec_diff_ns(end, start);

	rps = (double)NR_SYNC_RPC / ((double)diff_ns / (double)NSEC_PER_SEC);
	printf("[tid: %d] Performed #%10d sync_rpc. Total %13ld ns, per RPC: %ld ns. RPC/s: %10lf\n",
		info->tid, NR_SYNC_RPC, diff_ns, diff_ns/NR_SYNC_RPC, rps);

	/* save to global array */
	per_rps[info->tid] = rps;
}

void test_sync_rpc_recv(struct thread_info *info, int NR_SYNC_RPC)
{
	int i, ret, ret_length;
	uintptr_t descriptor;
	char *read, *write;

	read = memalign(sysconf(_SC_PAGESIZE), 4096 * 2);
	write = memalign(sysconf(_SC_PAGESIZE), 4096 * 2);
	mlock(read, 4096);
	mlock(write, 4096);

	for (i = 0; i < NR_SYNC_RPC; i++) {
		ret = userspace_liteapi_receive_message_fast(info->inbound_port,
			read, 4096, &descriptor, &ret_length, BLOCK_CALL);
        	userspace_liteapi_reply_message(write, 4, descriptor);
	}
}

/*
 * Can we reuse the send buffer? Depends on when the syscall returned.
 * If it is returned after send has completed, then we can.
 * If it is returned before send has completed, then we can not.
 */
void test_async_rpc_send(struct thread_info *info, int NR_ASYNC_RPC)
{
	int *poll_array;
	int i, base, ret;
	char *read, *write;
	struct timespec start, end;
	struct timespec async_start_prev, async_start, async_end;
	long diff_ns, async_diff_ns;
	double rps;

	read = memalign(sysconf(_SC_PAGESIZE), 4096 * 2);
	write = memalign(sysconf(_SC_PAGESIZE), 4096 * 2);
	poll_array = memalign(sysconf(_SC_PAGESIZE),
		sizeof(int) * async_batch_size);
	memset(poll_array, 0, sizeof(int) * async_batch_size);
	mlock(read, 4096);
	mlock(write, 4096);
	mlock(poll_array, sizeof(int) * NR_ASYNC_RPC);

	info->nr_async_recorded = 0;
	info->total_async_lat = 0;
	memset(&async_start_prev, 0, sizeof(struct timespec));
	memset(&async_start, 0, sizeof(struct timespec));

	clock_gettime(CLOCK_MONOTONIC, &start);
	for (i = 0; i < NR_ASYNC_RPC; i++) {
		bool record_async;

		/*
		 * Only record the async time at each polling batch boundary.
		 * Cause otherwise all the clock_gettime latency will be included.
		 */
		if (record_async_latency && (i % async_batch_size == 0))
			record_async = true;
		else
			record_async = false;

		if (record_async) {
			async_start_prev = async_start;
			clock_gettime(CLOCK_MONOTONIC, &async_start);
		}

		/*
		 * send 4 bytes 
		 * And only use [0, async_batch_size) portion of poll array.
		 */
		async_rpc(info->remote_nid, info->outbound_port,
			write, 4, read, &poll_array[i % async_batch_size], 4096);

		/* Perform batch completion check */
		if ((i % async_batch_size == 0) && i != 0) {
			int k;

			/* This means we only record the first rpc in a batch */
			if (record_async) {
				clock_gettime(CLOCK_MONOTONIC, &async_end);
				async_diff_ns = timespec_diff_ns(async_end,
								 async_start_prev);
				if (async_diff_ns > 0) {
					info->nr_async_recorded++;
					info->total_async_lat += async_diff_ns;
				} else
					BUG();
			}

			base = i - async_batch_size;
			for (k = 0; k < async_batch_size; k++)
				wait_for_completion(&poll_array[(base + k) & async_batch_size]);
		}
	}
	clock_gettime(CLOCK_MONOTONIC, &end);

	diff_ns = timespec_diff_ns(end, start);

	rps = (double)NR_ASYNC_RPC / ((double)diff_ns / (double)NSEC_PER_SEC);

	/* save to global array */
	per_rps[info->tid] = rps;

	printf("[tid: %d] Performed #%10d async_rpc (batch_size: %u). "
	       "Total %13ld ns. RPC/s: %10lf\n",
	        info->tid,
		NR_ASYNC_RPC, async_batch_size, diff_ns, rps);

	if (record_async_latency) {
		printf("  ..\033[32m [tid: %d] async_rpc latency numbers: total = %12lf ns, nr = %d, avg = %12lf ns \033[0m\n",
			info->tid,
			info->total_async_lat, info->nr_async_recorded,
			info->total_async_lat / info->nr_async_recorded);
	}
}

void test_async_rpc_recv(struct thread_info *info, int NR_ASYNC_RPC)
{
	int i, ret, ret_length;
	uintptr_t descriptor;
	char *read, *write;

	read = memalign(sysconf(_SC_PAGESIZE), 4096 * 2);
	write = memalign(sysconf(_SC_PAGESIZE), 4096 * 2);
	mlock(read, 4096);
	mlock(write, 4096);

	for (i = 0; i < NR_ASYNC_RPC; i++) {
		ret = userspace_liteapi_receive_message_fast(info->inbound_port,
			read, 4096, &descriptor, &ret_length, BLOCK_CALL);
        	userspace_liteapi_reply_message(write, 4, descriptor);
	}
}

int testsize[7]={8,8,64,512,1024,2048,4096};
int run_times = 10;
int base_port = 1;

/*
 * HACK!!!
 * Nothing is reliable.
 * Match the number of send and recv on both ends.
 */

static void print_rps(int nr_rpc, const char *who)
{
	int i;
	double total = 0;

	for (i = 0; i < nr_threads; i++) {
		printf("   .. %10lf\n", per_rps[i]);
		total += per_rps[i];
	}

	printf("\033[31m  nr_threads: %d, each thread performed #%d %s RPC, average throughput: %10lf\033[0m\n",
		nr_threads, nr_rpc, who, total/nr_threads);
}

void *thread_send_lat(void *_info)
{
	struct thread_info *info = _info;
	int i, b, nr_rpc;

	/*
	 * XXX
	 * Hard-coded distribution
	 */
	bind_thread(55 - info->tid);
	printf("  thread[%d] running on CPU%2d\n",
		info->tid, sched_getcpu());

	nr_rpc = 1000 * 1000 * 1;

	test_async_rpc_send(info, nr_rpc);

	b = pthread_barrier_wait(&thread_barrier);
	if (b == PTHREAD_BARRIER_SERIAL_THREAD)
		print_rps(nr_rpc, "async");

	test_sync_rpc_send(info, nr_rpc);

	b = pthread_barrier_wait(&thread_barrier);
	if (b == PTHREAD_BARRIER_SERIAL_THREAD)
		print_rps(nr_rpc, "sync");
}

void *thread_recv(void *_info)
{
	struct thread_info *info = _info;
	int i, nr_rpc;

	nr_rpc = 1000 * 1000 * 1;

	test_async_rpc_recv(info, nr_rpc);
	test_sync_rpc_recv(info, nr_rpc);
}

void run(bool server_mode, int remote_node)
{
	int i;
	char name[32] = {'\0'};
	pthread_t *threads;
	struct thread_info *info;

	threads = malloc(sizeof(*threads) * nr_threads);
	per_rps = malloc(sizeof(*per_rps) * nr_threads);
	info = malloc(sizeof(*info) * nr_threads);
	memset(info, 0, sizeof(*info) * nr_threads);

	pthread_barrier_init(&thread_barrier, NULL, nr_threads);

	sprintf(name, "test.1");

	/*
	 * Okay, symmetric RPC.
	 * Server use (base_port) to receive client's RPC request
	 * Client use (base_port + 1) to receive server's RPC request
	 *
	 * By doing this, server/client can both send RPC to each other.
	 */
	if (server_mode) {
		for (i = 0; i < nr_threads; i++) {
			info[i].inbound_port = base_port;
			info[i].outbound_port = base_port + 1;
			info[i].tid = i;

			/* XXX: hardcoded */
			info[i].remote_nid = 1;
		}

       		userspace_liteapi_register_application(info[0].inbound_port,
			4096, 16, name, strlen(name));

                userspace_liteapi_dist_barrier(2);
		printf("Pass dist barrier..\n");

		/*
		 * Server should query client's inboud port info,
		 * which is base_port + 1
		 */
		userspace_liteapi_query_port(info[0].remote_nid,
					     info[0].outbound_port);

		for (i = 0; i < nr_threads; i++)
			pthread_create(&threads[i], NULL, thread_recv, info + i);
		for (i = 0; i < nr_threads; i++)
			pthread_join(threads[i], NULL);
	} else {
		for (i = 0; i < nr_threads; i++) {
			info[i].inbound_port = base_port + 1;
			info[i].outbound_port = base_port;
			info[i].tid = i;
			info[i].remote_nid = remote_node;
		}

       		userspace_liteapi_register_application(info[0].inbound_port,	
			4096, 16, name, strlen(name));

                userspace_liteapi_dist_barrier(2);
		printf("Pass dist barrier..\n");

		/*
		 * Client should query server's inboud port info,
		 * which is base_port
		 */
		userspace_liteapi_query_port(info[0].remote_nid,
					     info[0].outbound_port);

		for (i = 0; i < nr_threads; i++)
                	pthread_create(&threads[i], NULL, thread_send_lat, info + i);
		for (i = 0; i < nr_threads; i++)
			pthread_join(threads[i], NULL);
	}
}

static void usage(const char *argv0)
{
	printf("Usage:\n");
	printf("  %s -s                    start a server and wait for connection\n", argv0);
	printf("  %s -c -n <nid>           start a client and connect to server at <nid>\n", argv0);
	printf("  %s -c -n <nid> -b 128\n", argv0);
	printf("  %s -c -n <nid> --thread=16\n", argv0);
	printf("  %s -c -n <nid> -b 128 --thread=16\n", argv0);
	printf("  %s -c -n <nid> -b 128 --thread=16 --async-latency\n", argv0);
	printf("\n");
	printf("Options:\n");
	printf("  -c, --client              start a client\n");
	printf("  -s, --server              start a server\n");
	printf("  -n, --remote_nid=<nid>    remote server_id\n");
	printf("  --thread=<nr>             number of threads, default 1\n");
	printf("  --async-latency           calculate async RPC end-to-end latency\n");
	printf("  -b, --async-batch=<nr>\n"
	       "                            async rpc batch size.\n"
	       "                            Default is 32. Limit is %d\n"
	       "                            Large batch size will have high per-async RPC latency.\n", ASYNC_BATCH_LIMIT);
}

static struct option long_options[] = {
	{ .name = "server",		.has_arg = 0, .val = 's' },
	{ .name = "client",		.has_arg = 0, .val = 'c' },
	{ .name = "remote_nid",		.has_arg = 1, .val = 'n' },
	{ .name = "thread",		.has_arg = 1, .val = 't' },
	{ .name = "async-latency",	.has_arg = 0, .val = 'a' },
	{ .name = "async-batch",	.has_arg = 1, .val = 'b' },
	{}
};

int main(int argc, char *argv[])
{
	bool server_mode = false, client_mode = false;
	unsigned int remote_nid = -1;

	while (1) {
		int c;

		c = getopt_long(argc, argv, "b:t:n:sca",
				long_options, NULL);

		if (c == -1)
			break;

		switch (c) {
		case 's':
			server_mode = true;
			break;
		case 'c':
			client_mode = true;
			break;
		case 'a':
			record_async_latency = true;
			break;
		case 'b':
			async_batch_size = strtoul(optarg, NULL, 0);
			if (async_batch_size > ASYNC_BATCH_LIMIT) {
				usage(argv[0]);
				return -1;
			}
			break;
		case 'n':
			remote_nid = strtoul(optarg, NULL, 0);
			if (remote_nid > 16) {
				usage(argv[0]);
				return -1;
			}
			break;
		case 't':
			nr_threads = strtoul(optarg, NULL, 0);
			if (nr_threads > 56) {
				printf("Too many threads: %d\n", nr_threads);
				return -1;
			}
			break;
		default:
			usage(argv[0]);
			return -1;
		};
	}

	if (!server_mode && !client_mode) {
		usage(argv[0]);
		return -1;
	} else if (client_mode && (remote_nid == -1)) {
		usage(argv[0]);
		return -1;
	} else if (server_mode && (remote_nid != -1)) {
		usage(argv[0]);
		return -1;
	}

	if (server_mode)
		printf("RPC server, waiting for connection nr_threads=%d\n", nr_threads);
	else {
		printf("RPC client, connect to server at %d nr_threads=%d\n",
			remote_nid, nr_threads);
	}

	run(server_mode, remote_nid);

	return 0;
}
