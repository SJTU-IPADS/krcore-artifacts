// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2019 Mellanox Technologies. */

#include "dr_types.h"

#define DR_ICM_MODIFY_HDR_ALIGN_BASE 64
#define DR_ICM_SYNC_THRESHOLD (64 * 1024 * 1024)

struct mlx5dr_icm_pool {
	enum mlx5dr_icm_type icm_type;
	enum mlx5dr_icm_chunk_size max_log_chunk_sz;
	struct mlx5dr_domain *dmn;
	/* memory management */
	struct mutex mutex; /* protect the ICM pool */
	struct list_head buddy_mem_list;
};

struct mlx5dr_icm_dm {
	u32 obj_id;
	enum mlx5_sw_icm_type type;
	phys_addr_t addr;
	size_t length;
};

struct mlx5dr_icm_mr {
	struct mlx5_core_mkey mkey;
	struct mlx5dr_icm_dm dm;
	struct mlx5dr_domain *dmn;
	size_t length;
	u64 icm_start_addr;
};

struct mlx5dr_icm_buddy_mem {
	unsigned long		**bits;
	unsigned int		*num_free;
	unsigned long		**set_bit;
	u32			max_order;
	struct list_head	list_node;
	struct mlx5dr_icm_mr	*icm_mr;
	struct mlx5dr_icm_pool	*pool;

	/* This is the list of used chunks. HW may be accessing this memory */
	struct list_head	used_list;

	/* hardware may be accessing this memory but at some future,
	 * undetermined time, it might cease to do so.
	 * sync_ste command sets them free.
	 */
	struct list_head	hot_list;
	/* indicates the byte size of hot mem */
	unsigned int		hot_memory_size;
	unsigned int		used_memory;
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

static enum mlx5dr_icm_type
get_chunk_icm_type(struct mlx5dr_icm_chunk *chunk)
{
	return chunk->buddy_mem->pool->icm_type;
}

static int dr_buddy_init(struct mlx5dr_icm_buddy_mem *buddy, u32 max_order)
{
	int i, s;

	buddy->max_order = max_order;

	INIT_LIST_HEAD(&buddy->list_node);
	INIT_LIST_HEAD(&buddy->used_list);
	INIT_LIST_HEAD(&buddy->hot_list);

	buddy->bits = kcalloc(buddy->max_order + 1,
			      sizeof(*buddy->bits),
			      GFP_KERNEL);
	if (!buddy->bits)
		return -ENOMEM;

	buddy->num_free = kcalloc(buddy->max_order + 1,
				  sizeof(*buddy->num_free),
				  GFP_KERNEL);
	if (!buddy->num_free)
		goto err_out_free_bits;

	buddy->set_bit = kcalloc(buddy->max_order + 1,
				 sizeof(*buddy->set_bit),
				 GFP_KERNEL);
	if (!buddy->set_bit)
		goto err_out_free_num_free;

	for (i = 0; i <= buddy->max_order; i++) {
		s = BITS_TO_LONGS(1 << (buddy->max_order - i));
		buddy->bits[i] = kcalloc(s, sizeof(long), GFP_KERNEL);
		if (!buddy->bits[i])
			goto err_out_free_each_bit_per_order;
	}

	for (i = 0; i <= buddy->max_order; ++i) {
		s = BITS_TO_LONGS(1 << (buddy->max_order - i));
		buddy->set_bit[i] = kcalloc(BITS_TO_LONGS(s), sizeof(long), GFP_KERNEL);
		if (!buddy->set_bit[i])
			goto err_out_free_set;
	}

	dr_set_bit(0, buddy->bits[buddy->max_order]);
	dr_set_bit(0, buddy->set_bit[buddy->max_order]);
	buddy->num_free[buddy->max_order] = 1;

	return 0;

err_out_free_set:
	for (i = 0; i <= buddy->max_order; ++i)
		kfree(buddy->set_bit[i]);

err_out_free_each_bit_per_order:
	kfree(buddy->set_bit);

	for (i = 0; i <= buddy->max_order; ++i)
		kfree(buddy->bits[i]);

err_out_free_num_free:
	kfree(buddy->num_free);

err_out_free_bits:
	kfree(buddy->bits);
	return -ENOMEM;
}

static void dr_buddy_cleanup(struct mlx5dr_icm_buddy_mem *buddy)
{
	int i;

	list_del(&buddy->list_node);

	for (i = 0; i <= buddy->max_order; ++i) {
		kfree(buddy->bits[i]);
		kfree(buddy->set_bit[i]);
	}

	kfree(buddy->set_bit);
	kfree(buddy->num_free);
	kfree(buddy->bits);
}

/* This function finds the first area of the managed memory by the buddy.
 * It uses the data structures of the buddy-system in order to find the first
 * area of free place, starting from the current order till the maximum order
 * in the system.
 */
static int dr_buddy_alloc_mem(struct mlx5dr_icm_buddy_mem *buddy, int order)
{
	u32 seg;
	int o, m;

	for (o = order; o <= buddy->max_order; ++o)
		if (buddy->num_free[o]) {
			m = 1 << (buddy->max_order - o);
			seg = dr_find_first_bit(buddy->set_bit[o],
						buddy->bits[o], m);
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
dr_buddy_free_mem(struct mlx5dr_icm_buddy_mem *buddy, u32 seg, int order)
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

static int dr_icm_create_dm_mkey(struct mlx5_core_dev *mdev,
				 u32 pd, u64 length, u64 start_addr, int mode,
				 struct mlx5_core_mkey *mkey)
{
	u32 inlen = MLX5_ST_SZ_BYTES(create_mkey_in);
	u32 in[MLX5_ST_SZ_DW(create_mkey_in)] = {};
	void *mkc;

	mkc = MLX5_ADDR_OF(create_mkey_in, in, memory_key_mkey_entry);

	MLX5_SET(mkc, mkc, access_mode_1_0, mode);
	MLX5_SET(mkc, mkc, access_mode_4_2, (mode >> 2) & 0x7);
	MLX5_SET(mkc, mkc, lw, 1);
	MLX5_SET(mkc, mkc, lr, 1);
	if (mode == MLX5_MKC_ACCESS_MODE_SW_ICM) {
		MLX5_SET(mkc, mkc, rw, 1);
		MLX5_SET(mkc, mkc, rr, 1);
	}

	MLX5_SET64(mkc, mkc, len, length);
	MLX5_SET(mkc, mkc, pd, pd);
	MLX5_SET(mkc, mkc, qpn, 0xffffff);
	MLX5_SET64(mkc, mkc, start_addr, start_addr);

	return mlx5_core_create_mkey(mdev, mkey, in, inlen);
}

static struct mlx5dr_icm_mr *
dr_icm_pool_mr_create(struct mlx5dr_icm_pool *pool)
{
	struct mlx5_core_dev *mdev = pool->dmn->mdev;
	enum mlx5_sw_icm_type dm_type;
	struct mlx5dr_icm_mr *icm_mr;
	size_t log_align_base;
	int err;

	icm_mr = kvzalloc(sizeof(*icm_mr), GFP_KERNEL);
	if (!icm_mr)
		return NULL;

	icm_mr->dmn = pool->dmn;

	/* 2^log_biggest_table * entry-size */
	icm_mr->dm.length = mlx5dr_icm_pool_chunk_size_to_byte(pool->max_log_chunk_sz,
							       pool->icm_type);

	if (pool->icm_type == DR_ICM_TYPE_STE) {
		dm_type = MLX5_SW_ICM_TYPE_STEERING;
		log_align_base = ilog2(icm_mr->dm.length);
	}
	else {
		dm_type = MLX5_SW_ICM_TYPE_HEADER_MODIFY;
		/* Align base is 64B */
		log_align_base = ilog2(DR_ICM_MODIFY_HDR_ALIGN_BASE);
	}
	icm_mr->dm.type = dm_type;

	err = mlx5_dm_sw_icm_alloc(mdev, icm_mr->dm.type, icm_mr->dm.length,
				   log_align_base, 0, &icm_mr->dm.addr,
				   &icm_mr->dm.obj_id);
	if (err) {
		mlx5dr_err(pool->dmn, "Failed to allocate SW ICM memory, err (%d)\n", err);
		goto free_icm_mr;
	}

	/* Register device memory */
	err = dr_icm_create_dm_mkey(mdev, pool->dmn->pdn,
				    icm_mr->dm.length,
				    icm_mr->dm.addr,
				    MLX5_MKC_ACCESS_MODE_SW_ICM,
				    &icm_mr->mkey);
	if (err) {
		mlx5dr_err(pool->dmn, "Failed to create SW ICM MKEY, err (%d)\n", err);
		goto free_dm;
	}

	icm_mr->icm_start_addr = icm_mr->dm.addr;

	if (icm_mr->icm_start_addr & (BIT(log_align_base) - 1)) {
		mlx5dr_err(pool->dmn, "Failed to get aligned ICM memory\n");
		goto free_mkey;
	}

	return icm_mr;

free_mkey:
	mlx5_core_destroy_mkey(mdev, &icm_mr->mkey);
free_dm:
	mlx5_dm_sw_icm_dealloc(mdev, icm_mr->dm.type, icm_mr->dm.length, 0,
			       icm_mr->dm.addr, icm_mr->dm.obj_id);
free_icm_mr:
	kvfree(icm_mr);
	return NULL;
}

static void dr_icm_pool_mr_destroy(struct mlx5dr_icm_mr *icm_mr)
{
	struct mlx5_core_dev *mdev = icm_mr->dmn->mdev;
	struct mlx5dr_icm_dm *dm = &icm_mr->dm;

	mlx5_core_destroy_mkey(mdev, &icm_mr->mkey);
	mlx5_dm_sw_icm_dealloc(mdev, dm->type, dm->length, 0,
			       dm->addr, dm->obj_id);
	kvfree(icm_mr);
}

static int dr_icm_chunk_ste_init(struct mlx5dr_icm_chunk *chunk)
{
	chunk->ste_arr = kvzalloc(chunk->num_of_entries *
				  sizeof(chunk->ste_arr[0]), GFP_KERNEL);
	if (!chunk->ste_arr)
		return -ENOMEM;

	chunk->hw_ste_arr = kvzalloc(chunk->num_of_entries *
				     DR_STE_SIZE_REDUCED, GFP_KERNEL);
	if (!chunk->hw_ste_arr)
		goto out_free_ste_arr;

	chunk->miss_list = kvmalloc(chunk->num_of_entries *
				    sizeof(chunk->miss_list[0]), GFP_KERNEL);
	if (!chunk->miss_list)
		goto out_free_hw_ste_arr;

	return 0;

out_free_hw_ste_arr:
	kvfree(chunk->hw_ste_arr);
out_free_ste_arr:
	kvfree(chunk->ste_arr);
	return -ENOMEM;
}

static void dr_icm_chunk_ste_cleanup(struct mlx5dr_icm_chunk *chunk)
{
	kvfree(chunk->miss_list);
	kvfree(chunk->hw_ste_arr);
	kvfree(chunk->ste_arr);
}

static void dr_icm_chunk_destroy(struct mlx5dr_icm_chunk *chunk)
{
	enum mlx5dr_icm_type icm_type = get_chunk_icm_type(chunk);

	list_del(&chunk->chunk_list);

	if (icm_type == DR_ICM_TYPE_STE)
		dr_icm_chunk_ste_cleanup(chunk);

	kvfree(chunk);
}

static int dr_icm_buddy_create(struct mlx5dr_icm_pool *pool)
{
	struct mlx5dr_icm_buddy_mem *buddy;
	struct mlx5dr_icm_mr *icm_mr;

	icm_mr = dr_icm_pool_mr_create(pool);
	if (!icm_mr)
		return -ENOMEM;

	buddy = kvzalloc(sizeof(*buddy), GFP_KERNEL);
	if (!buddy)
		goto free_mr;

	if (dr_buddy_init(buddy, pool->max_log_chunk_sz))
		goto err_free_buddy;

	buddy->icm_mr = icm_mr;
	buddy->pool = pool;

	/* add it to the -start- of the list in order to search in it first */
	list_add(&buddy->list_node, &pool->buddy_mem_list);

	return 0;

err_free_buddy:
	kvfree(buddy);
free_mr:
	dr_icm_pool_mr_destroy(icm_mr);
	return -ENOMEM;
}

static void dr_icm_buddy_destroy(struct mlx5dr_icm_buddy_mem *buddy)
{
	struct mlx5dr_icm_chunk *chunk, *next;
	struct list_head tmp_list;

	INIT_LIST_HEAD(&tmp_list);

	list_splice_tail_init(&buddy->hot_list, &tmp_list);
	list_splice_tail_init(&buddy->used_list, &tmp_list);

	list_for_each_entry_safe(chunk, next, &tmp_list, chunk_list)
		dr_icm_chunk_destroy(chunk);

	dr_icm_pool_mr_destroy(buddy->icm_mr);

	dr_buddy_cleanup(buddy);

	kvfree(buddy);
}

static struct mlx5dr_icm_chunk *
dr_icm_chunk_create(struct mlx5dr_icm_pool *pool,
		    enum mlx5dr_icm_chunk_size chunk_size,
		    struct mlx5dr_icm_buddy_mem *buddy_mem_pool,
		    int seg)
{
	struct mlx5dr_icm_chunk *chunk;
	int offset;

	chunk = kvzalloc(sizeof(struct mlx5dr_icm_chunk), GFP_KERNEL);
	if (!chunk)
		return NULL;

	offset = mlx5dr_icm_pool_dm_type_to_entry_size(pool->icm_type) * seg;

	chunk->rkey = buddy_mem_pool->icm_mr->mkey.key;
	chunk->mr_addr = offset;
	chunk->icm_addr =
		(uintptr_t)buddy_mem_pool->icm_mr->icm_start_addr + offset;
	chunk->num_of_entries =
		mlx5dr_icm_pool_chunk_size_to_entries(chunk_size);
	chunk->byte_size =
		mlx5dr_icm_pool_chunk_size_to_byte(chunk_size, pool->icm_type);
	chunk->seg = seg;

	if (pool->icm_type == DR_ICM_TYPE_STE)
		if (dr_icm_chunk_ste_init(chunk)) {
			mlx5dr_err(pool->dmn,
				   "Failed to init ste arrays (order: %d)\n",
				   chunk_size);
			goto out_free_chunk;
		}

	chunk->buddy_mem = buddy_mem_pool;
	INIT_LIST_HEAD(&chunk->chunk_list);

	/* chunk now is part of the used_list */
	list_add_tail(&chunk->chunk_list, &buddy_mem_pool->used_list);

	buddy_mem_pool->used_memory += chunk->byte_size;

	return chunk;

out_free_chunk:
	kvfree(chunk);
	return NULL;
}

static bool dr_icm_pool_is_sync_required(struct mlx5dr_icm_pool *pool)
{
	u64 allow_hot_size, all_hot_mem = 0;
	struct mlx5dr_icm_buddy_mem *buddy;

	list_for_each_entry(buddy, &pool->buddy_mem_list, list_node) {
		allow_hot_size =
			mlx5dr_icm_pool_chunk_size_to_byte((buddy->max_order - 2),
							   pool->icm_type);
		all_hot_mem += buddy->hot_memory_size;

		if ((buddy->hot_memory_size > allow_hot_size) ||
		    (all_hot_mem > DR_ICM_SYNC_THRESHOLD))
			return true;
	}

	return false;
}

static int dr_icm_pool_sync_all_buddy_pools(struct mlx5dr_icm_pool *pool)
{
	struct mlx5dr_icm_buddy_mem *buddy, *tmp_buddy;
	int err;

	err = mlx5dr_cmd_sync_steering(pool->dmn->mdev);
	if (err) {
		mlx5dr_err(pool->dmn, "Failed to sync to HW (err: %d)\n", err);
		return err;
	}

	list_for_each_entry_safe(buddy, tmp_buddy, &pool->buddy_mem_list, list_node) {
		struct mlx5dr_icm_chunk *chunk, *tmp_chunk;

		list_for_each_entry_safe(chunk, tmp_chunk, &buddy->hot_list, chunk_list) {
			dr_buddy_free_mem(buddy, chunk->seg,
					  ilog2(chunk->num_of_entries));
			buddy->hot_memory_size -= chunk->byte_size;
			buddy->used_memory -= chunk->byte_size;
			dr_icm_chunk_destroy(chunk);
		}
		if (!buddy->used_memory)
			dr_icm_buddy_destroy(buddy);
	}

	return 0;
}

static int dr_icm_handle_buddies_get_mem(struct mlx5dr_icm_pool *pool,
					 enum mlx5dr_icm_chunk_size chunk_size,
					 struct mlx5dr_icm_buddy_mem **buddy,
					 int *seg)
{
	struct mlx5dr_icm_buddy_mem *buddy_mem_pool;
	bool new_mem = false;
	int err = 0;

	*seg = -1;

	/* find the next free place from the buddy list */
	while (*seg == -1) {
		list_for_each_entry(buddy_mem_pool, &pool->buddy_mem_list, list_node) {
			*seg = dr_buddy_alloc_mem(buddy_mem_pool, chunk_size);
			if (*seg != -1)
				goto found;

			if (new_mem) {
				/* We have new memory pool, first in the list */
				mlx5dr_err(pool->dmn,
					   "No memory for order: %d\n",
					   chunk_size);
				err = -ENOMEM;
				goto out;
			}
		}
		/* no more available allocators in that pool, create new */
		err = dr_icm_buddy_create(pool);
		if (err) {
			mlx5dr_err(pool->dmn,
				   "Failed creating buddy for order %d\n",
				   chunk_size);
			goto out;
		}
		/* mark we have new memory, first in list */
		new_mem = true;
	}

found:
	*buddy = buddy_mem_pool;
out:
	return err;
}

/* Allocate an ICM chunk, each chunk holds a piece of ICM memory and
 * also memory used for HW STE management for optimizations.
 */
struct mlx5dr_icm_chunk *
mlx5dr_icm_alloc_chunk(struct mlx5dr_icm_pool *pool,
		       enum mlx5dr_icm_chunk_size chunk_size)
{
	struct mlx5dr_icm_buddy_mem *buddy;
	struct mlx5dr_icm_chunk *chunk = NULL; /* Fix compilation warning */
	int ret;
	int seg;

	if (chunk_size > pool->max_log_chunk_sz)
		return NULL;

	mutex_lock(&pool->mutex);
	/* find mem, get back the relevant buddy pool and seg in that mem */
	ret = dr_icm_handle_buddies_get_mem(pool, chunk_size, &buddy, &seg);
	if (ret)
		goto out_unlock;

	chunk = dr_icm_chunk_create(pool, chunk_size, buddy, seg);
	if (!chunk)
		goto out_err;

	mutex_unlock(&pool->mutex);
	return chunk;

out_err:
	dr_buddy_free_mem(buddy, seg, chunk_size);
out_unlock:
	mutex_unlock(&pool->mutex);
	return NULL;
}

void mlx5dr_icm_free_chunk(struct mlx5dr_icm_chunk *chunk)
{
	struct mlx5dr_icm_buddy_mem *buddy = chunk->buddy_mem;

	/* move the memory to the waiting list AKA "hot" */
	mutex_lock(&buddy->pool->mutex);
	list_del_init(&chunk->chunk_list);
	list_add_tail(&chunk->chunk_list, &buddy->hot_list);
	buddy->hot_memory_size += chunk->byte_size;
	/* Check if we have chunks that are waiting for sync-ste */
	if (dr_icm_pool_is_sync_required(buddy->pool))
		dr_icm_pool_sync_all_buddy_pools(buddy->pool);
	mutex_unlock(&buddy->pool->mutex);
}

struct mlx5dr_icm_pool *mlx5dr_icm_pool_create(struct mlx5dr_domain *dmn,
					       enum mlx5dr_icm_type icm_type)
{
	enum mlx5dr_icm_chunk_size max_log_chunk_sz;
	struct mlx5dr_icm_pool *pool;

	if (icm_type == DR_ICM_TYPE_STE)
		max_log_chunk_sz = dmn->info.max_log_sw_icm_sz;
	else
		max_log_chunk_sz = dmn->info.max_log_action_icm_sz;

	pool = kvzalloc(sizeof(*pool), GFP_KERNEL);
	if (!pool)
		return NULL;

	pool->dmn = dmn;
	pool->icm_type = icm_type;
	pool->max_log_chunk_sz = max_log_chunk_sz;
	INIT_LIST_HEAD(&pool->buddy_mem_list);

	mutex_init(&pool->mutex);

	return pool;
}

void mlx5dr_icm_pool_destroy(struct mlx5dr_icm_pool *pool)
{
	struct mlx5dr_icm_buddy_mem *buddy, *tmp_buddy;

	list_for_each_entry_safe(buddy, tmp_buddy, &pool->buddy_mem_list, list_node)
		dr_icm_buddy_destroy(buddy);

	mutex_destroy(&pool->mutex);
	kvfree(pool);
}
