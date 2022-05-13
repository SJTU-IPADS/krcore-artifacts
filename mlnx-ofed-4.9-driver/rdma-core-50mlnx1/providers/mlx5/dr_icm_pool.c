/*
 * Copyright (c) 2019, Mellanox Technologies. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *	Redistribution and use in source and binary forms, with or
 *	without modification, are permitted provided that the following
 *	conditions are met:
 *
 *	- Redistributions of source code must retain the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer.
 *
 *	- Redistributions in binary form must reproduce the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer in the documentation and/or other materials
 *	  provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdlib.h>
#include <string.h>
#include <ccan/minmax.h>
#include "mlx5dv_dr.h"

#define DR_ICM_MODIFY_HDR_ALIGN_BASE	64
#define DR_ICM_ACTION_SYNC_THRESHOLD (1024 * 1024)
#define DR_ICM_SYNC_THRESHOLD (64 * 1024 * 1024)

struct dr_icm_pool {
	enum dr_icm_type	icm_type;
	enum dr_icm_chunk_size	max_log_chunk_sz;
	struct mlx5dv_dr_domain	*dmn;
	uint64_t                sync_threshold;
	/* memory manangment */
	struct list_head	buddy_mem_list;
	pthread_mutex_t		mutex;
};

struct dr_icm_mr {
	struct ibv_mr		*mr;
	struct ibv_dm		*dm;
	uint64_t		icm_start_addr;
};

struct dr_icm_buddy_mem {
	unsigned long		**bits;
	unsigned int		*num_free;
	unsigned long		**set_bit;
	uint32_t		max_order;
	struct list_node	list_node;
	struct dr_icm_mr	*icm_mr;
	struct dr_icm_pool	*pool;

	/* This is the list of used chunks. HW may be accessing this memory */
	struct list_head	used_list;

	/* hardware may be accessing this memory but at some future,
	 * undetermined time, it might cease to do so.
	 * sync_ste command sets them free.
	 */
	struct list_head	hot_list;
	/* indicates the byte size of hot mem */
	unsigned int		hot_memory;
};

static inline unsigned long dr_ffs(uint64_t l)
{
	unsigned long res = __builtin_ffsl(l);

	return res ? res - 1 : 0;
}

static inline void dr_set_bit(unsigned int nr, unsigned long *addr)
{
	addr[(nr / BITS_PER_LONG)] |= (1UL << (nr % BITS_PER_LONG));
}

static inline void dr_clear_bit(unsigned int nr,  unsigned long *addr)
{
	addr[(nr / BITS_PER_LONG)] &= ~(1UL << (nr % BITS_PER_LONG));
}

static inline int dr_test_bit(unsigned int nr, const unsigned long *addr)
{
	return !!(addr[(nr / BITS_PER_LONG)] & (1UL <<  (nr % BITS_PER_LONG)));
}

static int dr_find_first_bit(const unsigned long *set_addr,
			     const unsigned long *addr,
			     unsigned int size)
{
	unsigned int set_size = (size - 1) / BITS_PER_LONG + 1;
	unsigned long set_idx, idx;

	for (idx = 0; idx * BITS_PER_LONG < set_size; idx++) {
		if (set_addr[idx]) {
			set_idx = min((unsigned int)(idx * BITS_PER_LONG +
						     dr_ffs(set_addr[idx])), set_size);
			return min((unsigned int)(((unsigned int)set_idx) * BITS_PER_LONG +
						  dr_ffs(addr[set_idx])), size);
		}
	}

	return size;
}

static enum dr_icm_type
get_chunk_icm_type(struct dr_icm_chunk *chunk)
{
	return chunk->buddy_mem->pool->icm_type;
}

static int dr_buddy_init(struct dr_icm_buddy_mem *buddy, int max_order)
{
	int i, s;

	buddy->max_order = max_order;

	list_node_init(&buddy->list_node);
	list_head_init(&buddy->used_list);
	list_head_init(&buddy->hot_list);

	buddy->bits = calloc(buddy->max_order + 1, sizeof(long *));
	if (!buddy->bits) {
		errno = ENOMEM;
		return ENOMEM;
	}

	buddy->num_free = calloc(buddy->max_order + 1, sizeof(*buddy->num_free));
	if (!buddy->num_free)
		goto err_out_free_bits;

	buddy->set_bit = calloc(buddy->max_order + 1, sizeof(long *));
	if (!buddy->set_bit)
		goto err_out_free_num_free;

	for (i = 0; i <= buddy->max_order; ++i) {
		s = BITS_TO_LONGS(1 << (buddy->max_order - i));
		buddy->bits[i] = calloc(s, sizeof(long));
		if (!buddy->bits[i])
			goto err_out_free;
	}

	for (i = 0; i <= buddy->max_order; ++i) {
		s = BITS_TO_LONGS(1 << (buddy->max_order - i));
		buddy->set_bit[i] = calloc(BITS_TO_LONGS(s), sizeof(long));
		if (!buddy->set_bit[i])
			goto err_out_free_set;
	}

	dr_set_bit(0, buddy->bits[buddy->max_order]);
	dr_set_bit(0, buddy->set_bit[buddy->max_order]);
	buddy->num_free[buddy->max_order] = 1;

	return 0;

err_out_free_set:
	for (i = 0; i <= buddy->max_order; ++i)
		free(buddy->set_bit[i]);

err_out_free:
	free(buddy->set_bit);

	for (i = 0; i <= buddy->max_order; ++i)
		free(buddy->bits[i]);

err_out_free_num_free:
	free(buddy->num_free);

err_out_free_bits:
	free(buddy->bits);
	errno = ENOMEM;
	return ENOMEM;
}

static void dr_buddy_cleanup(struct dr_icm_buddy_mem *buddy)
{
	int i;

	list_del(&buddy->list_node);

	for (i = 0; i <= buddy->max_order; ++i) {
		free(buddy->bits[i]);
		free(buddy->set_bit[i]);
	}

	free(buddy->set_bit);
	free(buddy->num_free);
	free(buddy->bits);
}

static uint32_t dr_buddy_alloc_mem(struct dr_icm_buddy_mem *buddy, int order)
{
	uint32_t seg;
	int o, m;

	for (o = order; o <= buddy->max_order; ++o)
		if (buddy->num_free[o]) {
			m = 1 << (buddy->max_order - o);
			seg = dr_find_first_bit(buddy->set_bit[o], buddy->bits[o], m);
			if (seg < m)
				goto found;
		}

	return -1;

found:
	dr_clear_bit(seg, buddy->bits[o]);
	if (buddy->bits[o][seg / BITS_PER_LONG] == 0)
		dr_clear_bit(seg / BITS_PER_LONG, buddy->set_bit[o]);
	--buddy->num_free[o];
	while (o > order) {
		--o;
		seg <<= 1;
		dr_set_bit(seg ^ 1, buddy->bits[o]);
		dr_set_bit((seg ^ 1) / BITS_PER_LONG, buddy->set_bit[o]);

		++buddy->num_free[o];
	}

	seg <<= order;

	return seg;
}

static void
dr_buddy_free_mem(struct dr_icm_buddy_mem *buddy, uint32_t seg, int order)
{
	seg >>= order;

	while (dr_test_bit(seg ^ 1, buddy->bits[order])) {
		dr_clear_bit(seg ^ 1, buddy->bits[order]);
		if (buddy->bits[order][(seg ^ 1) / BITS_PER_LONG] == 0)
			dr_clear_bit((seg ^ 1) / BITS_PER_LONG, buddy->set_bit[order]);
		--buddy->num_free[order];
		seg >>= 1;
		++order;
	}
	dr_set_bit(seg, buddy->bits[order]);
	dr_set_bit(seg / BITS_PER_LONG, buddy->set_bit[order]);

	++buddy->num_free[order];
}

#define MAX_RETRY_DM_ALIGN_ALLOC 2

static int
dr_icm_allocate_aligned_dm(struct dr_icm_pool *pool,
			   struct dr_icm_mr *icm_mr,
			   enum mlx5_ib_uapi_dm_type dm_type,
			   struct ibv_alloc_dm_attr *dm_attr,
			   size_t align_base)
{
	struct mlx5dv_alloc_dm_attr mlx5_dm_attr = {};
	struct mlx5_dm *dm;
	size_t align_diff;
	u8 align_factor;
	u8 retry;

	mlx5_dm_attr.type = dm_type;

	align_factor = 1;
	retry = MAX_RETRY_DM_ALIGN_ALLOC; /* only 2 tries to allocate */

	do {
		dm_attr->length =
			dr_icm_pool_chunk_size_to_byte(pool->max_log_chunk_sz,
						       pool->icm_type) * align_factor;
		dm_attr->log_align_req = pool->max_log_chunk_sz +
			(align_factor - 1);

		if (icm_mr->dm)
			mlx5_free_dm(icm_mr->dm);

		align_factor++; /* allocate double size */

		retry--;

		icm_mr->dm = mlx5dv_alloc_dm(pool->dmn->ctx, dm_attr, &mlx5_dm_attr);
		if (!icm_mr->dm) {
			dr_dbg(pool->dmn, "Failed allocating DM\n");
			return ENOMEM;
		}

		dm = to_mdm(icm_mr->dm);
		icm_mr->icm_start_addr = dm->remote_va;

		align_diff = icm_mr->icm_start_addr % align_base;
		if (align_diff) {
			dr_dbg(pool->dmn, "Got not aligned memory: %zu try: %d\n",
			       align_diff, retry);
			icm_mr->icm_start_addr = dm->remote_va +
				(align_base - align_diff);
		}

	} while (retry && align_diff);

	return 0;

}

static struct dr_icm_mr *
dr_icm_pool_mr_create(struct dr_icm_pool *pool,
		      enum mlx5_ib_uapi_dm_type dm_type,
		      size_t align_base)
{
	struct ibv_alloc_dm_attr dm_attr = {};
	struct dr_icm_mr *icm_mr;

	icm_mr = calloc(1, sizeof(struct dr_icm_mr));
	if (!icm_mr) {
		errno = ENOMEM;
		return NULL;
	}

	if (dr_icm_allocate_aligned_dm(pool,icm_mr, dm_type, &dm_attr, align_base))
		goto free_icm_mr;

	/* Register device memory */
	icm_mr->mr = ibv_reg_dm_mr(pool->dmn->pd, icm_mr->dm, 0,
				   dm_attr.length,
				   IBV_ACCESS_ZERO_BASED |
				   IBV_ACCESS_REMOTE_WRITE |
				   IBV_ACCESS_LOCAL_WRITE |
				   IBV_ACCESS_REMOTE_READ);
	if (!icm_mr->mr) {
		dr_dbg(pool->dmn, "Failed DM registration\n");
		goto free_dm;
	}

	return icm_mr;

free_dm:
	mlx5_free_dm(icm_mr->dm);
free_icm_mr:
	free(icm_mr);
	return NULL;
}

static  void dr_icm_pool_mr_destroy(struct dr_icm_mr *icm_mr)
{
	ibv_dereg_mr(icm_mr->mr);
	mlx5_free_dm(icm_mr->dm);
	free(icm_mr);
}

static int dr_icm_chunk_ste_init(struct dr_icm_chunk *chunk)
{
	chunk->ste_arr = calloc(chunk->num_of_entries, sizeof(struct dr_ste));
	if (!chunk->ste_arr) {
		errno = ENOMEM;
		return errno;
	}

	chunk->hw_ste_arr = calloc(chunk->num_of_entries, DR_STE_SIZE_REDUCED);
	if (!chunk->hw_ste_arr) {
		errno = ENOMEM;
		goto out_free_ste_arr;
	}

	chunk->miss_list = malloc(chunk->num_of_entries *
				  sizeof(struct list_head));
	if (!chunk->miss_list) {
		errno = ENOMEM;
		goto out_free_hw_ste_arr;
	}

	return 0;

out_free_hw_ste_arr:
	free(chunk->hw_ste_arr);
out_free_ste_arr:
	free(chunk->ste_arr);
	return errno;
}

static void dr_icm_chunk_ste_cleanup(struct dr_icm_chunk *chunk)
{
	free(chunk->miss_list);
	free(chunk->hw_ste_arr);
	free(chunk->ste_arr);
}

static void dr_icm_chunk_destroy(struct dr_icm_chunk *chunk)
{
	enum dr_icm_type icm_type = get_chunk_icm_type(chunk);

	list_del(&chunk->chunk_list);

	if (icm_type == DR_ICM_TYPE_STE)
		dr_icm_chunk_ste_cleanup(chunk);

	free(chunk);
}

static int dr_icm_buddy_create(struct dr_icm_pool *pool)
{
	enum mlx5_ib_uapi_dm_type dm_type;
	struct dr_icm_buddy_mem *buddy;
	struct dr_icm_mr *icm_mr;
	size_t align_base;
	size_t mr_size;

	/* create dm/mr for this pool */
	mr_size = dr_icm_pool_chunk_size_to_byte(pool->max_log_chunk_sz,
						 pool->icm_type);

	if (pool->icm_type == DR_ICM_TYPE_STE) {
		dm_type = MLX5_IB_UAPI_DM_TYPE_STEERING_SW_ICM;
		/* Align base is the biggest chunk size */
		align_base = mr_size;
	} else if (pool->icm_type == DR_ICM_TYPE_MODIFY_ACTION) {
		dm_type = MLX5_IB_UAPI_DM_TYPE_HEADER_MODIFY_SW_ICM;
		/* Align base is 64B */
		align_base = DR_ICM_MODIFY_HDR_ALIGN_BASE;
	}

	icm_mr = dr_icm_pool_mr_create(pool, dm_type, align_base);
	if (!icm_mr) {
		errno = ENOMEM;
		return ENOMEM;
	}

	buddy = calloc(1, sizeof(*buddy));
	if (!buddy) {
		errno = ENOMEM;
		goto free_mr;
	}

	if (dr_buddy_init(buddy, pool->max_log_chunk_sz))
		goto err_free_buddy;

	buddy->icm_mr = icm_mr;
	buddy->pool = pool;

	/* add it to the -start- of the list in order to search in it first */
	list_add(&pool->buddy_mem_list, &buddy->list_node);

	return 0;

err_free_buddy:
	free(buddy);
free_mr:
	free(icm_mr);
	errno = EINVAL;
	return EINVAL;
}

static void dr_icm_buddy_destroy(struct dr_icm_buddy_mem *buddy)
{
	struct dr_icm_chunk *chunk, *next;
	struct list_head tmp_list;

	list_head_init(&tmp_list);

	list_append_list(&tmp_list, &buddy->hot_list);
	list_append_list(&tmp_list, &buddy->used_list);

	list_for_each_safe(&tmp_list, chunk, next, chunk_list)
		dr_icm_chunk_destroy(chunk);

	dr_icm_pool_mr_destroy(buddy->icm_mr);

	dr_buddy_cleanup(buddy);

	free(buddy);
}

static struct dr_icm_chunk *
dr_icm_chunk_create(struct dr_icm_pool *pool,
		    enum dr_icm_chunk_size chunk_size,
		    struct dr_icm_buddy_mem *buddy_mem_pool,
		    int seg)
{
	struct dr_icm_chunk *chunk;
	int offset;

	chunk = calloc(1, sizeof(struct dr_icm_chunk));
	if (!chunk) {
		errno = ENOMEM;
		return NULL;
	}

	offset = dr_icm_pool_dm_type_to_entry_size(pool->icm_type) * seg;

	chunk->rkey = buddy_mem_pool->icm_mr->mr->rkey;
	chunk->mr_addr = (uintptr_t)buddy_mem_pool->icm_mr->mr->addr + offset;
	chunk->icm_addr = (uintptr_t)buddy_mem_pool->icm_mr->icm_start_addr + offset;
	chunk->num_of_entries = dr_icm_pool_chunk_size_to_entries(chunk_size);
	chunk->byte_size = dr_icm_pool_chunk_size_to_byte(chunk_size, pool->icm_type);
	chunk->seg = seg;

	if (pool->icm_type == DR_ICM_TYPE_STE)
		if (dr_icm_chunk_ste_init(chunk)) {
			dr_dbg(pool->dmn, "failed init ste arrays: order: %d\n",
			       chunk_size)
			goto out_free_chunk;
		}

	chunk->buddy_mem = buddy_mem_pool;
	list_node_init(&chunk->chunk_list);

	/* chunk now is part of the used_list */
	list_add_tail(&buddy_mem_pool->used_list, &chunk->chunk_list);

	return chunk;

out_free_chunk:
	free(chunk);
	return NULL;
}

static bool dr_icm_pool_is_sync_required(struct dr_icm_pool *pool)
{
	uint64_t allow_hot_size, all_hot_mem = 0;
	struct dr_icm_buddy_mem *buddy;

	list_for_each(&pool->buddy_mem_list, buddy, list_node) {
		allow_hot_size = dr_icm_pool_chunk_size_to_byte((buddy->max_order - 2),
								pool->icm_type);
		all_hot_mem += buddy->hot_memory;

		if ((buddy->hot_memory > allow_hot_size) ||
		    (all_hot_mem > pool->sync_threshold))
			return true;
	}

	return false;
}

static int dr_icm_pool_sync_all_buddy_pools(struct dr_icm_pool *pool)
{
	struct dr_icm_buddy_mem *buddy, *tmp_buddy;
	int err;

	err = dr_devx_sync_steering(pool->dmn->ctx);
	if (err) {
		dr_dbg(pool->dmn, "failed devx sync hw\n");
		return err;
	}

	list_for_each_safe(&pool->buddy_mem_list, buddy, tmp_buddy, list_node) {
		struct dr_icm_chunk *chunk, *tmp_chunk;

		list_for_each_safe(&buddy->hot_list, chunk, tmp_chunk, chunk_list) {
			dr_buddy_free_mem(buddy, chunk->seg,
					  ilog32(chunk->num_of_entries - 1));
			buddy->hot_memory -= chunk->byte_size;
			dr_icm_chunk_destroy(chunk);
		}
	}

	return 0;
}

static int dr_icm_handle_buddies_get_mem(struct dr_icm_pool *pool,
					 enum dr_icm_chunk_size chunk_size,
					 struct dr_icm_buddy_mem **buddy,
					 int *seg)
{
	struct dr_icm_buddy_mem *buddy_mem_pool;
	bool new_mem = false;
	int err;

	/* Check if we have chunks that are waiting for sync-ste */
	if (dr_icm_pool_is_sync_required(pool))
		dr_icm_pool_sync_all_buddy_pools(pool);

	*seg = -1;

	/* find the next free place from the buddy list */
	while (*seg == -1) {
		list_for_each(&pool->buddy_mem_list, buddy_mem_pool, list_node) {
			*seg = dr_buddy_alloc_mem(buddy_mem_pool, chunk_size);
			if (*seg != -1)
				goto found;

			if (new_mem) {
				/*
				 * We have new memory pool, first in the list,
				 * so why no mem for me..
				 */
				dr_dbg(pool->dmn, "no memory for order: %d\n",
				       chunk_size);
				errno = ENOMEM;
				return ENOMEM;
			}
		}
		/* no more available allocators in that pool, create new */
		err = dr_icm_buddy_create(pool);
		if (err)
			return err;
		/* mark we have new memory, first in list */
		new_mem = true;
	}

found:
	*buddy = buddy_mem_pool;
	return 0;
}

/* Allocate an ICM chunk, each chunk holds a piece of ICM memory and
 * also memory used for HW STE management for optimisations.
 */
struct dr_icm_chunk *dr_icm_alloc_chunk(struct dr_icm_pool *pool,
					enum dr_icm_chunk_size chunk_size)
{
	struct dr_icm_buddy_mem *buddy;
	struct dr_icm_chunk *chunk;
	int ret;
	int seg;

	if (chunk_size > pool->max_log_chunk_sz) {
		errno = EINVAL;
		return NULL;
	}

	pthread_mutex_lock(&pool->mutex);
	/* find mem, get back the relevant buddy pool and seg in that mem */
	ret = dr_icm_handle_buddies_get_mem(pool, chunk_size, &buddy, &seg);
	if (ret)
		goto out;

	chunk = dr_icm_chunk_create(pool, chunk_size, buddy, seg);
	if (!chunk)
		goto out_err;
	pthread_mutex_unlock(&pool->mutex);

	return chunk;

out_err:
	dr_buddy_free_mem(buddy, seg, chunk_size);
out:
	pthread_mutex_unlock(&pool->mutex);
	return NULL;
}

void dr_icm_free_chunk(struct dr_icm_chunk *chunk)
{
	struct dr_icm_buddy_mem *buddy = chunk->buddy_mem;

	pthread_mutex_lock(&buddy->pool->mutex);
	/* move the memory to the waiting list AKA "hot" */
	list_del_init(&chunk->chunk_list);
	list_add_tail(&buddy->hot_list, &chunk->chunk_list);
	buddy->hot_memory += chunk->byte_size;
	pthread_mutex_unlock(&buddy->pool->mutex);
}

struct dr_icm_pool *dr_icm_pool_create(struct mlx5dv_dr_domain *dmn,
				       enum dr_icm_type icm_type)
{
	enum dr_icm_chunk_size max_log_chunk_sz;
	struct dr_icm_pool *pool;

	if (icm_type == DR_ICM_TYPE_STE)
		max_log_chunk_sz = dmn->info.max_log_sw_icm_sz;
	else
		max_log_chunk_sz = dmn->info.max_log_action_icm_sz;

	pool = calloc(1, sizeof(struct dr_icm_pool));
	if (!pool) {
		errno = ENOMEM;
		return NULL;
	}

	if (icm_type == DR_ICM_TYPE_STE)
		pool->sync_threshold = DR_ICM_SYNC_THRESHOLD;
	else
		pool->sync_threshold = DR_ICM_ACTION_SYNC_THRESHOLD;

	pool->dmn = dmn;
	pool->icm_type = icm_type;
	pool->max_log_chunk_sz = max_log_chunk_sz;

	list_head_init(&pool->buddy_mem_list);

	pthread_mutex_init(&pool->mutex, NULL);

	return pool;
}

void dr_icm_pool_destroy(struct dr_icm_pool *pool)
{
	struct dr_icm_buddy_mem *buddy, *tmp_buddy;

	list_for_each_safe(&pool->buddy_mem_list, buddy, tmp_buddy, list_node)
		dr_icm_buddy_destroy(buddy);

	pthread_mutex_destroy(&pool->mutex);

	free(pool);
}
