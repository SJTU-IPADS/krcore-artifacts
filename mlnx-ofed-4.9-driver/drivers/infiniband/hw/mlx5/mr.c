/*
 * Copyright (c) 2013-2015, Mellanox Technologies. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */


#include <linux/kref.h>
#include <linux/random.h>
#include <linux/debugfs.h>
#include <linux/export.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/sysfs.h>
#include <linux/sched/signal.h>
#ifdef CONFIG_CXL_LIB
#include <linux/sched/mm.h>
#endif
#include <rdma/ib_umem.h>
#include <rdma/ib_umem_odp.h>
#include <rdma/ib_verbs.h>
#include "mlx5_ib.h"

static void mlx5_invalidate_umem(void *invalidation_cookie,
				 struct ib_umem *umem,
				 unsigned long addr, size_t size);

enum {
	MAX_PENDING_REG_MR = 8,
	MAX_MR_RELEASE_TIMEOUT = (60 * 20) /* Allow release timeout up to 20 min */
};

#define MLX5_UMR_ALIGN 2048

static void
create_mkey_callback(int status, struct mlx5_async_work *context);

static void
assign_mkey_variant(struct mlx5_ib_dev *dev, struct mlx5_core_mkey *mkey,
		    u32 *in)
{
	void *mkc;
	u8 key;

	spin_lock_irq(&dev->mkey_lock);
	key = dev->mkey_key++;
	spin_unlock_irq(&dev->mkey_lock);

	mkc = MLX5_ADDR_OF(create_mkey_in, in, memory_key_mkey_entry);
	MLX5_SET(mkc, mkc, mkey_7_0, key);
	mkey->key = key;
}

static int
mlx5_ib_create_mkey(struct mlx5_ib_dev *dev, struct mlx5_core_mkey *mkey,
		    u32 *in, int inlen)
{
	assign_mkey_variant(dev, mkey, in);
	return mlx5_core_create_mkey(dev->mdev, mkey, in, inlen);
}

static int
mlx5_ib_create_mkey_cb(struct mlx5_ib_dev *dev,
		       struct mlx5_core_mkey *mkey,
		       struct mlx5_async_ctx *async_ctx,
		       u32 *in, int inlen, u32 *out, int outlen,
		       struct mlx5_async_work *context)
{
	MLX5_SET(create_mkey_in, in, opcode, MLX5_CMD_OP_CREATE_MKEY);
	assign_mkey_variant(dev, mkey, in);
	return mlx5_cmd_exec_cb(async_ctx, in, inlen, out, outlen,
				create_mkey_callback, context);
}

static void clean_mr(struct mlx5_ib_dev *dev, struct mlx5_ib_mr *mr);
static void dereg_mr(struct mlx5_ib_dev *dev, struct mlx5_ib_mr *mr);
static int mr_cache_max_order(struct mlx5_ib_dev *dev);
static int unreg_umr(struct mlx5_ib_dev *dev, struct mlx5_ib_mr *mr);

static int mlx5_mr_sysfs_init(struct mlx5_ib_dev *dev);
static void mlx5_mr_sysfs_cleanup(struct mlx5_ib_dev *dev);

static struct mlx5_ib_mr *reg_klm(struct ib_pd *pd, struct ib_umem *umem,
				  u64 virt_addr, u64 len, int npages,
				  int page_shift, int order, int access_flags);
static int use_klm(int order);
static void populate_klm(void *dma, struct mlx5_ib_mr **lmr, int n);

static bool umr_can_modify_entity_size(struct mlx5_ib_dev *dev)
{
	return !MLX5_CAP_GEN(dev->mdev, umr_modify_entity_size_disabled);
}

static bool umr_can_use_indirect_mkey(struct mlx5_ib_dev *dev)
{
	return !MLX5_CAP_GEN(dev->mdev, umr_indirect_mkey_disabled);
}

static bool use_umr(struct mlx5_ib_dev *dev, int order)
{
	return order <= mr_cache_max_order(dev) &&
		umr_can_modify_entity_size(dev);
}

static int destroy_mkey(struct mlx5_ib_dev *dev, struct mlx5_ib_mr *mr)
{
	int err = mlx5_core_destroy_mkey(dev->mdev, &mr->mmkey);

	if (IS_ENABLED(CONFIG_INFINIBAND_ON_DEMAND_PAGING))
		/* Wait until all page fault handlers using the mr complete. */
		synchronize_srcu(&dev->mr_srcu);

	return err;
}

static int order2idx(struct mlx5_ib_dev *dev, int order)
{
	struct mlx5_mr_cache *cache = &dev->cache;

	if (order < cache->ent[0].order)
		return 0;
	else
		return order - cache->ent[0].order;
}

static bool use_umr_mtt_update(struct mlx5_ib_mr *mr, u64 start, u64 length)
{
	return ((u64)1 << mr->order) * MLX5_ADAPTER_PAGE_SIZE >=
		length + (start & (MLX5_ADAPTER_PAGE_SIZE - 1));
}

static void update_odp_mr(struct mlx5_ib_mr *mr, struct mlx5_ib_dev *dev)
{
	if (is_odp_mr(mr)) {
		/*
		 * This barrier prevents the compiler from moving the
		 * setting of umem->odp_data->private to point to our
		 * MR, before reg_umr finished, to ensure that the MR
		 * initialization have finished before starting to
		 * handle invalidations.
		 */
		smp_wmb();
		to_ib_umem_odp(mr->umem)->private = mr;
		/*
		 * Make sure we will see the new
		 * umem->odp_data->private value in the invalidation
		 * routines, before we can get page faults on the
		 * MR. Page faults can happen once we put the MR in
		 * the tree, below this line. Without the barrier,
		 * there can be a fault handling and an invalidation
		 * before umem->odp_data->private == mr is visible to
		 * the invalidation handler.
		 */
		smp_wmb();
		if (dev)
			atomic_inc(&dev->odp_stats.num_odp_mrs);
	}
}

static void create_mkey_callback(int status, struct mlx5_async_work *context)
{
	struct mlx5_ib_mr *mr =
		container_of(context, struct mlx5_ib_mr, cb_work);
	struct mlx5_ib_dev *dev = mr->dev;
	struct mlx5_mr_cache *cache = &dev->cache;
	int c = order2idx(dev, mr->order);
	struct mlx5_cache_ent *ent = &cache->ent[c];
	struct mlx5_core_mkey *mkey = &mr->mmkey;
	struct mlx5_mkey_table *table = &dev->mdev->priv.mkey_table;
	unsigned long flags;
	int err;

	spin_lock_irqsave(&ent->lock, flags);
	ent->pending--;
	spin_unlock_irqrestore(&ent->lock, flags);
	if (status) {
		mlx5_ib_warn(dev, "async reg mr failed. status %d\n", status);
		kfree(mr);
		dev->fill_delay = 1;
		mod_timer(&dev->delay_timer, jiffies + HZ);
		goto do_complete;
	}

	mr->mmkey.type = MLX5_MKEY_MR;
	mkey->key |= mlx5_idx_to_mkey(
		MLX5_GET(create_mkey_out, mr->out, mkey_index));

	cache->last_add = jiffies;

	spin_lock_irqsave(&ent->lock, flags);
	list_add_tail(&mr->list, &ent->head);
	ent->cur++;
	ent->size++;
	spin_unlock_irqrestore(&ent->lock, flags);

	spin_lock_irqsave(&table->lock, flags);
	err = radix_tree_insert(&table->tree, mlx5_mkey_to_idx(mkey->key),
				mkey);
	spin_unlock_irqrestore(&table->lock, flags);
	if (err)
		pr_err("failed radix tree insert of mkey 0x%x, %d\n",
		       mkey->key, err);

do_complete:
	spin_lock_irqsave(&ent->lock, flags);
	if (atomic_read(&ent->do_complete)) {
		complete(&ent->compl);
		atomic_dec(&ent->do_complete);
	}
	spin_unlock_irqrestore(&ent->lock, flags);
}

static int add_keys(struct mlx5_ib_dev *dev, int c, int num)
{
	struct mlx5_mr_cache *cache = &dev->cache;
	struct mlx5_cache_ent *ent = &cache->ent[c];
	int inlen = MLX5_ST_SZ_BYTES(create_mkey_in);
	struct mlx5_ib_mr *mr;
	void *mkc;
	u32 *in;
	int err = 0;
	int i;

	in = kzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	mkc = MLX5_ADDR_OF(create_mkey_in, in, memory_key_mkey_entry);
	for (i = 0; i < num; i++) {
		if (ent->pending >= MAX_PENDING_REG_MR) {
			err = -EAGAIN;
			break;
		}

		mr = kzalloc(sizeof(*mr), GFP_KERNEL);
		if (!mr) {
			err = -ENOMEM;
			break;
		}
		mr->order = ent->order;
		mr->allocated_from_cache = 1;
		mr->free = 1;
		mr->access_mode = ent->access_mode;
		mr->dev = dev;

		MLX5_SET(mkc, mkc, free, 1);
		MLX5_SET(mkc, mkc, umr_en, 1);
		MLX5_SET(mkc, mkc, access_mode_1_0, ent->access_mode & 0x3);
		MLX5_SET(mkc, mkc, access_mode_4_2,
			 (ent->access_mode >> 2) & 0x7);

		MLX5_SET(mkc, mkc, qpn, 0xffffff);
		MLX5_SET(mkc, mkc, translations_octword_size, ent->xlt);
		MLX5_SET(mkc, mkc, log_page_size, ent->page);

		spin_lock_irq(&ent->lock);
		ent->pending++;
		spin_unlock_irq(&ent->lock);
		err = mlx5_ib_create_mkey_cb(dev, &mr->mmkey,
					       &dev->async_ctx, in, inlen,
					       mr->out, sizeof(mr->out),
					       &mr->cb_work);
		if (err) {
			spin_lock_irq(&ent->lock);
			ent->pending--;
			spin_unlock_irq(&ent->lock);
			mlx5_ib_warn(dev, "create mkey failed %d\n", err);
			kfree(mr);
			break;
		}
	}

	kfree(in);
	return err;
}

static void remove_keys(struct mlx5_ib_dev *dev, int c, int num)
{
	struct mlx5_mr_cache *cache = &dev->cache;
	struct mlx5_cache_ent *ent = &cache->ent[c];
	struct mlx5_ib_mr *tmp_mr;
	struct mlx5_ib_mr *mr;
	LIST_HEAD(del_list);
	int i;

	for (i = 0; i < num; i++) {
		spin_lock_irq(&ent->lock);
		if (list_empty(&ent->head)) {
			spin_unlock_irq(&ent->lock);
			break;
		}
		mr = list_first_entry(&ent->head, struct mlx5_ib_mr, list);
		list_move(&mr->list, &del_list);
		ent->cur--;
		ent->size--;
		spin_unlock_irq(&ent->lock);
		mlx5_core_destroy_mkey(dev->mdev, &mr->mmkey);
	}

	if (IS_ENABLED(CONFIG_INFINIBAND_ON_DEMAND_PAGING))
		synchronize_srcu(&dev->mr_srcu);

	list_for_each_entry_safe(mr, tmp_mr, &del_list, list) {
		list_del(&mr->list);
		kfree(mr);
	}
}

static int someone_adding(struct mlx5_mr_cache *cache)
{
	int i;

	for (i = 0; i < MAX_MR_CACHE_ENTRIES; i++) {
		if (cache->ent[i].cur < cache->ent[i].limit)
			return 1;
	}

	return 0;
}

static int someone_releasing(struct mlx5_mr_cache *cache)
{
	int i;

	for (i = 0; i < MAX_MR_CACHE_ENTRIES; i++) {
		if (cache->ent[i].cur > 2 * cache->ent[i].limit)
			return 1;
	}

	return 0;
}

static void __cache_work_func(struct mlx5_cache_ent *ent)
{
	struct mlx5_ib_dev *dev = ent->dev;
	struct mlx5_mr_cache *cache = &dev->cache;
	int i = order2idx(dev, ent->order);
	int err;
	s64 dtime;

	if (cache->stopped)
		return;

	ent = &dev->cache.ent[i];
	if (ent->cur < 2 * ent->limit && !dev->fill_delay) {
		err = add_keys(dev, i, 1);
		if (ent->cur < 2 * ent->limit) {
			if (err == -EAGAIN) {
				mlx5_ib_dbg(dev, "returned eagain, order %d\n",
					    i + 2);
				cancel_delayed_work(&ent->dwork);
				queue_delayed_work(cache->wq, &ent->dwork,
						   msecs_to_jiffies(3));
			} else if (err) {
				mlx5_ib_warn(dev, "command failed order %d, err %d\n",
					     i + 2, err);
				cancel_delayed_work(&ent->dwork);
				queue_delayed_work(cache->wq, &ent->dwork,
						   msecs_to_jiffies(1000));
			} else {
				queue_work(cache->wq, &ent->work);
			}
		}
	} else if (ent->cur > 2 * ent->limit) {
		/*
		 * The remove_keys() logic is performed as garbage collection
		 * task. Such task is intended to be run when no other active
		 * processes are running.
		 *
		 * The need_resched() will return TRUE if there are user tasks
		 * to be activated in near future.
		 *
		 * In such case, we don't execute remove_keys() and postpone
		 * the garbage collection work to try to run in next cycle,
		 * in order to free CPU resources to other tasks.
		 */
		dtime = (cache->last_add + (s64)cache->rel_timeout * HZ) -
			jiffies;
		if (cache->rel_imm || (!need_resched() &&
		    cache->rel_timeout >= 0 && !someone_adding(cache) &&
		    dtime <= 0)) {
			remove_keys(dev, i, 1);
			if (ent->cur > ent->limit)
				queue_work(cache->wq, &ent->work);
		} else if (cache->rel_timeout >= 0) {
			dtime = max_t(s64, dtime, msecs_to_jiffies(3));
			dtime = min_t(s64, dtime, (MAX_MR_RELEASE_TIMEOUT * HZ));
			cancel_delayed_work(&ent->dwork);
			queue_delayed_work(cache->wq, &ent->dwork, dtime);
		}
	} else if (cache->rel_imm && !someone_releasing(cache)) {
		cache->rel_imm = 0;
	}
}

static void delayed_cache_work_func(struct work_struct *work)
{
	struct mlx5_cache_ent *ent;

	ent = container_of(work, struct mlx5_cache_ent, dwork.work);
	__cache_work_func(ent);
}

static void cache_work_func(struct work_struct *work)
{
	struct mlx5_cache_ent *ent;

	ent = container_of(work, struct mlx5_cache_ent, work);
	__cache_work_func(ent);
}

struct mlx5_ib_mr *mlx5_mr_cache_alloc(struct mlx5_ib_dev *dev, int entry)
{
	struct mlx5_mr_cache *cache = &dev->cache;
	struct mlx5_cache_ent *ent;
	struct mlx5_ib_mr *mr;
	int err;

	if (entry < 0 || entry >= MAX_MR_CACHE_ENTRIES) {
		mlx5_ib_err(dev, "cache entry %d is out of range\n", entry);
		return NULL;
	}

	ent = &cache->ent[entry];
	while (1) {
		spin_lock_irq(&ent->lock);
		if (list_empty(&ent->head)) {
			spin_unlock_irq(&ent->lock);

			atomic_inc(&ent->do_complete);
			err = add_keys(dev, entry, 1);
			if (err && err != -EAGAIN) {
				atomic_dec(&ent->do_complete);
				return ERR_PTR(err);
			}

			if (err)
				msleep(20);
			else
				wait_for_completion(&ent->compl);
		} else {
			mr = list_first_entry(&ent->head, struct mlx5_ib_mr,
					      list);
			list_del(&mr->list);
			ent->cur--;
			spin_unlock_irq(&ent->lock);
			if (ent->cur < ent->limit)
				queue_work(cache->wq, &ent->work);
			return mr;
		}
	}
}

static struct mlx5_ib_mr *alloc_cached_mr(struct mlx5_ib_dev *dev, int order)
{
	struct mlx5_mr_cache *cache = &dev->cache;
	struct mlx5_ib_mr *mr = NULL;
	struct mlx5_cache_ent *ent;
	int last_umr_cache_entry;
	int c;
	int i;

	c = order2idx(dev, order);
	last_umr_cache_entry = order2idx(dev, mr_cache_max_order(dev));
	if (c < 0 || c > last_umr_cache_entry) {
		mlx5_ib_warn(dev, "order %d, cache index %d\n", order, c);
		return NULL;
	}

	for (i = c; i <= last_umr_cache_entry; i++) {
		ent = &cache->ent[i];

		mlx5_ib_dbg(dev, "order %d, cache index %d\n", ent->order, i);

		spin_lock_irq(&ent->lock);
		if (!list_empty(&ent->head)) {
			mr = list_first_entry(&ent->head, struct mlx5_ib_mr,
					      list);
			list_del(&mr->list);
			ent->cur--;
			spin_unlock_irq(&ent->lock);
			if (ent->cur < ent->limit)
				queue_work(cache->wq, &ent->work);
			break;
		}
		spin_unlock_irq(&ent->lock);

		queue_work(cache->wq, &ent->work);
	}

	if (!mr)
		cache->ent[c].miss++;

	return mr;
}

void mlx5_mr_cache_free(struct mlx5_ib_dev *dev, struct mlx5_ib_mr *mr)
{
	struct mlx5_mr_cache *cache = &dev->cache;
	struct mlx5_cache_ent *ent;
	int shrink = 0;
	int c;

	if (!mr->allocated_from_cache)
		return;

	c = order2idx(dev, mr->order);
	if (c < 0 || c >= MAX_MR_CACHE_ENTRIES) {
		mlx5_ib_warn(dev, "order %d, cache index %d\n", mr->order, c);
		return;
	}

	if (unreg_umr(dev, mr))
		return;

	ent = &cache->ent[c];
	spin_lock_irq(&ent->lock);
	list_add_tail(&mr->list, &ent->head);
	ent->cur++;
	if (ent->cur > 2 * ent->limit)
		shrink = 1;
	spin_unlock_irq(&ent->lock);

	if (shrink)
		queue_work(cache->wq, &ent->work);
}

static void clean_keys(struct mlx5_ib_dev *dev, int c)
{
	struct mlx5_mr_cache *cache = &dev->cache;
	struct mlx5_cache_ent *ent = &cache->ent[c];
	struct mlx5_ib_mr *tmp_mr;
	struct mlx5_ib_mr *mr;
	LIST_HEAD(del_list);

	cancel_delayed_work(&ent->dwork);
	while (1) {
		spin_lock_irq(&ent->lock);
		if (list_empty(&ent->head)) {
			spin_unlock_irq(&ent->lock);
			break;
		}
		mr = list_first_entry(&ent->head, struct mlx5_ib_mr, list);
		list_move(&mr->list, &del_list);
		ent->cur--;
		ent->size--;
		spin_unlock_irq(&ent->lock);
		mlx5_core_destroy_mkey(dev->mdev, &mr->mmkey);
	}

#ifdef CONFIG_INFINIBAND_ON_DEMAND_PAGING
	synchronize_srcu(&dev->mr_srcu);
#endif

	list_for_each_entry_safe(mr, tmp_mr, &del_list, list) {
		list_del(&mr->list);
		kfree(mr);
	}
}

static void delay_time_func(struct timer_list *t)
{
	struct mlx5_ib_dev *dev = from_timer(dev, t, delay_timer);

	dev->fill_delay = 0;
}

int mlx5_mr_cache_init(struct mlx5_ib_dev *dev)
{
	struct mlx5_mr_cache *cache = &dev->cache;
	struct mlx5_cache_ent *ent;
	int i;

	mutex_init(&dev->slow_path_mutex);
	cache->rel_timeout = 300;
	cache->wq = alloc_ordered_workqueue("mkey_cache", WQ_MEM_RECLAIM);
	if (!cache->wq) {
		mlx5_ib_warn(dev, "failed to create work queue\n");
		return -ENOMEM;
	}

	mlx5_cmd_init_async_ctx(dev->mdev, &dev->async_ctx);
	timer_setup(&dev->delay_timer, delay_time_func, 0);
	for (i = 0; i < MAX_MR_CACHE_ENTRIES; i++) {
		ent = &cache->ent[i];
		INIT_LIST_HEAD(&ent->head);
		spin_lock_init(&ent->lock);
		ent->order = i + 2;
		ent->dev = dev;
		ent->limit = 0;
		atomic_set(&ent->do_complete, 0);

		init_completion(&ent->compl);
		INIT_WORK(&ent->work, cache_work_func);
		INIT_DELAYED_WORK(&ent->dwork, delayed_cache_work_func);

		if (i > MR_CACHE_LAST_STD_ENTRY) {
			mlx5_odp_init_mr_cache_entry(ent);
			continue;
		}

		if (ent->order > mr_cache_max_order(dev))
			continue;

		ent->page = PAGE_SHIFT;
		ent->xlt = (1 << ent->order) * sizeof(struct mlx5_mtt) /
			   MLX5_IB_UMR_OCTOWORD;
		ent->access_mode = MLX5_MKC_ACCESS_MODE_MTT;
		if ((dev->mdev->profile->mask & MLX5_PROF_MASK_MR_CACHE) &&
		    !dev->is_rep &&
		    mlx5_core_is_pf(dev->mdev))
			ent->limit = dev->mdev->profile->mr_cache[i].limit;
		else
			ent->limit = 0;
		queue_work(cache->wq, &ent->work);
	}

	mlx5_mr_sysfs_init(dev);

	return 0;
}

int mlx5_mr_cache_cleanup(struct mlx5_ib_dev *dev)
{
	int i;

	if (!dev->cache.wq)
		return 0;

	dev->cache.stopped = 1;
	flush_workqueue(dev->cache.wq);

	mlx5_mr_sysfs_cleanup(dev);
	mlx5_cmd_cleanup_async_ctx(&dev->async_ctx);

	for (i = 0; i < MAX_MR_CACHE_ENTRIES; i++)
		clean_keys(dev, i);

	destroy_workqueue(dev->cache.wq);
	del_timer_sync(&dev->delay_timer);

	return 0;
}

struct ib_mr *mlx5_ib_get_dma_mr_ex(struct ib_pd *pd, int acc,
				    u64 start_addr, u64 length)
{
	struct mlx5_ib_dev *dev = to_mdev(pd->device);
	int inlen = MLX5_ST_SZ_BYTES(create_mkey_in);
	struct mlx5_ib_mr *mr;
	void *mkc;
	u32 *in;
	int err;

	mr = kzalloc(sizeof(*mr), GFP_KERNEL);
	if (!mr)
		return ERR_PTR(-ENOMEM);

	in = kzalloc(inlen, GFP_KERNEL);
	if (!in) {
		err = -ENOMEM;
		goto err_free;
	}

	mkc = MLX5_ADDR_OF(create_mkey_in, in, memory_key_mkey_entry);

	MLX5_SET(mkc, mkc, access_mode_1_0, MLX5_MKC_ACCESS_MODE_PA);
	MLX5_SET(mkc, mkc, a, !!(acc & IB_ACCESS_REMOTE_ATOMIC));
	MLX5_SET(mkc, mkc, rw, !!(acc & IB_ACCESS_REMOTE_WRITE));
	MLX5_SET(mkc, mkc, rr, !!(acc & IB_ACCESS_REMOTE_READ));
	MLX5_SET(mkc, mkc, lw, !!(acc & IB_ACCESS_LOCAL_WRITE));
	MLX5_SET(mkc, mkc, lr, 1);

	if (MLX5_CAP_GEN(dev->mdev, relaxed_ordering_write))
		MLX5_SET(mkc, mkc, relaxed_ordering_write,
			 !!(acc & IB_ACCESS_RELAXED_ORDERING));
	if (MLX5_CAP_GEN(dev->mdev, relaxed_ordering_read))
		MLX5_SET(mkc, mkc, relaxed_ordering_read,
			 !!(acc & IB_ACCESS_RELAXED_ORDERING));

	MLX5_SET(mkc, mkc, length64, length ? 0 : 1);
	MLX5_SET64(mkc, mkc, len, length);
	MLX5_SET(mkc, mkc, pd, to_mpd(pd)->pdn);
	MLX5_SET(mkc, mkc, qpn, 0xffffff);
	MLX5_SET64(mkc, mkc, start_addr, start_addr);

	err = mlx5_ib_create_mkey(dev, &mr->mmkey, in, inlen);
	if (err)
		goto err_in;

	kfree(in);
	mr->mmkey.type = MLX5_MKEY_MR;
	mr->ibmr.lkey = mr->mmkey.key;
	mr->ibmr.rkey = mr->mmkey.key;
	mr->live = 1;
	mr->umem = NULL;

	return &mr->ibmr;

err_in:
	kfree(in);

err_free:
	kfree(mr);

	return ERR_PTR(err);
}

struct ib_mr *mlx5_ib_get_dma_mr(struct ib_pd *pd, int acc)
{
	return mlx5_ib_get_dma_mr_ex(pd, acc, 0, 0);
}

static int get_octo_len(u64 addr, u64 len, int page_shift)
{
	u64 page_size = 1ULL << page_shift;
	u64 offset;
	int npages;

	offset = addr & (page_size - 1);
	npages = ALIGN(len + offset, page_size) >> page_shift;
	return (npages + 1) / 2;
}

static int mr_cache_max_order(struct mlx5_ib_dev *dev)
{
	if (MLX5_CAP_GEN(dev->mdev, umr_extended_translation_offset))
		return MR_CACHE_LAST_STD_ENTRY + 2;
	return MLX5_MAX_UMR_SHIFT;
}

static int mr_umem_get(struct mlx5_ib_dev *dev, struct ib_udata *udata,
		       u64 start, u64 length, int access_flags,
		       struct ib_umem **umem, int *npages, int *page_shift,
		       int *ncont, int *order, int peer_mem_flags)
{
	struct ib_umem *u;
	int err;

	*umem = NULL;

	u = ib_umem_get(udata, start, length, access_flags, 0, peer_mem_flags);
	err = PTR_ERR_OR_ZERO(u);
	if (err) {
		mlx5_ib_dbg(dev, "umem get failed (%d)\n", err);
		return err;
	}

	/* Skip if it is CAPI mr */
	if ((access_flags & IB_ACCESS_ON_DEMAND) && mlx5_ib_capi_enabled(dev)) {
		*umem = u;
		return 0;
	}

	mlx5_ib_cont_pages(u, start, MLX5_MKEY_PAGE_SHIFT_MASK, npages,
			   page_shift, ncont, order);
	if (!*npages) {
		mlx5_ib_warn(dev, "avoid zero region\n");
		ib_umem_release(u);
		return -EINVAL;
	}

	*umem = u;

	mlx5_ib_dbg(dev, "npages %d, ncont %d, order %d, page_shift %d\n",
		    *npages, *ncont, *order, *page_shift);

	return 0;
}

static void mlx5_ib_umr_done(struct ib_cq *cq, struct ib_wc *wc)
{
	struct mlx5_ib_umr_context *context =
		container_of(wc->wr_cqe, struct mlx5_ib_umr_context, cqe);

	context->status = wc->status;
	complete(&context->done);
}

static inline void mlx5_ib_init_umr_context(struct mlx5_ib_umr_context *context)
{
	context->cqe.done = mlx5_ib_umr_done;
	context->status = -1;
	init_completion(&context->done);
}

static int mlx5_ib_post_send_wait(struct mlx5_ib_dev *dev,
				  struct mlx5_umr_wr *umrwr)
{
	struct umr_common *umrc = &dev->umrc;
	const struct ib_send_wr *bad;
	int err;
	struct mlx5_ib_umr_context umr_context;

	mlx5_ib_init_umr_context(&umr_context);
	umrwr->wr.wr_cqe = &umr_context.cqe;

	down(&umrc->sem);
	err = ib_post_send(umrc->qp, &umrwr->wr, &bad);
	if (err) {
		mlx5_ib_warn(dev, "UMR post send failed, err %d\n", err);
	} else {
		wait_for_completion(&umr_context.done);
		if (umr_context.status != IB_WC_SUCCESS) {
			mlx5_ib_warn(dev, "reg umr failed (%u)\n",
				     umr_context.status);
			err = -EFAULT;
		}
	}
	up(&umrc->sem);
	return err;
}

static struct mlx5_ib_mr *alloc_mr_from_cache(
				  struct ib_pd *pd, struct ib_umem *umem,
				  u64 virt_addr, u64 len, int npages,
				  int page_shift, int order, int access_flags)
{
	struct mlx5_ib_dev *dev = to_mdev(pd->device);
	struct mlx5_ib_mr *mr;
	int err = 0;
	int i;

	for (i = 0; i < 1; i++) {
		mr = alloc_cached_mr(dev, order);
		if (mr)
			break;

		err = add_keys(dev, order2idx(dev, order), 1);
		if (err && err != -EAGAIN) {
			mlx5_ib_warn(dev, "add_keys failed, err %d\n", err);
			break;
		}
	}

	if (!mr)
		return ERR_PTR(-EAGAIN);

	mr->ibmr.pd = pd;
	mr->umem = umem;
	mr->access_flags = access_flags;
	mr->desc_size = sizeof(struct mlx5_mtt);
	mr->mmkey.iova = virt_addr;
	mr->mmkey.size = len;
	mr->mmkey.pd = to_mpd(pd)->pdn;

	return mr;
}

static inline int populate_xlt(struct mlx5_ib_mr *mr, int idx, int npages,
			       void *xlt, int page_shift, size_t size,
			       int flags)
{
	struct mlx5_ib_dev *dev = mr->dev;
	struct ib_umem *umem = mr->umem;

#ifdef CONFIG_INFINIBAND_ON_DEMAND_PAGING
	if (umem->is_odp)
		return mlx5_odp_populate_xlt(xlt, idx, npages, mr, flags);
#endif

	if (flags & MLX5_IB_UPD_XLT_INDIRECT) {
		if (!umr_can_use_indirect_mkey(dev))
			return -EPERM;
		populate_klm(xlt, mr->children, mr->nchild);
		return mr->nchild;
	}

	npages = min_t(size_t, npages, ib_umem_num_pages(umem) - idx);

	if (!(flags & MLX5_IB_UPD_XLT_ZAP)) {
		__mlx5_ib_populate_pas(dev, umem, page_shift,
				       idx, npages, xlt,
				       MLX5_IB_MTT_PRESENT);
		/* Clear padding after the pages
		 * brought from the umem.
		 */
		memset(xlt + (npages * sizeof(struct mlx5_mtt)), 0,
		       size - npages * sizeof(struct mlx5_mtt));
	}

	return npages;
}

#define MLX5_MAX_UMR_CHUNK ((1 << (MLX5_MAX_UMR_SHIFT + 4)) - \
			    MLX5_UMR_MTT_ALIGNMENT)
#define MLX5_SPARE_UMR_CHUNK 0x10000

int mlx5_ib_update_xlt(struct mlx5_ib_mr *mr, u64 xlt_idx, u64 umem_idx,
		       int npages, int page_shift, int flags)
{
	struct mlx5_ib_dev *dev = mr->dev;
	struct device *ddev = dev->ib_dev.dev.parent;
	int size;
	void *xlt;
	dma_addr_t dma;
	struct mlx5_umr_wr wr;
	struct ib_sge sg;
	int err = 0;
	int desc_size = (flags & MLX5_IB_UPD_XLT_INDIRECT)
			       ? sizeof(struct mlx5_klm)
			       : sizeof(struct mlx5_mtt);
	const int page_align = MLX5_UMR_MTT_ALIGNMENT / desc_size;
	const int page_mask = page_align - 1;
	size_t pages_mapped = 0;
	size_t pages_to_map = 0;
	size_t pages_iter = 0;
	gfp_t gfp;
	bool use_emergency_page = false;

	if ((flags & MLX5_IB_UPD_XLT_INDIRECT) &&
	    !umr_can_use_indirect_mkey(dev))
		return -EPERM;

	/* UMR copies MTTs in units of MLX5_UMR_MTT_ALIGNMENT bytes,
	 * so we need to align the offset and length accordingly
	 */
	if (xlt_idx & page_mask) {
		npages += xlt_idx & page_mask;
		xlt_idx &= ~page_mask;
	}

	gfp = flags & MLX5_IB_UPD_XLT_ATOMIC ? GFP_ATOMIC : GFP_KERNEL;
	gfp |= __GFP_ZERO | __GFP_NOWARN;

	pages_to_map = ALIGN(npages, page_align);
	size = desc_size * pages_to_map;
	size = min_t(int, size, MLX5_MAX_UMR_CHUNK);

	xlt = (void *)__get_free_pages(gfp, get_order(size));
	if (!xlt && size > MLX5_SPARE_UMR_CHUNK) {
		mlx5_ib_dbg(dev, "Failed to allocate %d bytes of order %d. fallback to spare UMR allocation of %d bytes\n",
			    size, get_order(size), MLX5_SPARE_UMR_CHUNK);

		size = MLX5_SPARE_UMR_CHUNK;
		xlt = (void *)__get_free_pages(gfp, get_order(size));
	}

	if (!xlt) {
		mlx5_ib_warn(dev, "Using XLT emergency buffer\n");
		xlt = (void *)mlx5_ib_get_xlt_emergency_page();
		size = PAGE_SIZE;
		memset(xlt, 0, size);
		use_emergency_page = true;
	}
	pages_iter = size / desc_size;
	dma = dma_map_single(ddev, xlt, size, DMA_TO_DEVICE);
	if (dma_mapping_error(ddev, dma)) {
		mlx5_ib_err(dev, "unable to map DMA during XLT update.\n");
		err = -ENOMEM;
		goto free_xlt;
	}

	sg.addr = dma;
	sg.lkey = dev->umrc.pd->local_dma_lkey;

	memset(&wr, 0, sizeof(wr));
	wr.wr.send_flags = MLX5_IB_SEND_UMR_UPDATE_XLT;
	if (!(flags & MLX5_IB_UPD_XLT_ENABLE))
		wr.wr.send_flags |= MLX5_IB_SEND_UMR_FAIL_IF_FREE;
	wr.wr.sg_list = &sg;
	wr.wr.num_sge = 1;
	wr.wr.opcode = MLX5_IB_WR_UMR;

	wr.pd = mr->ibmr.pd;
	wr.mkey = mr->mmkey.key;
	wr.length = mr->mmkey.size;
	wr.virt_addr = mr->mmkey.iova;
	wr.access_flags = mr->access_flags;
	wr.page_shift = page_shift;

	for (pages_mapped = 0;
	     pages_mapped < pages_to_map && !err;
	     pages_mapped += pages_iter, xlt_idx += pages_iter) {
		npages = min_t(int, pages_iter, pages_to_map - pages_mapped);
		dma_sync_single_for_cpu(ddev, dma, size, DMA_TO_DEVICE);
		npages = populate_xlt(mr, umem_idx + xlt_idx, npages, xlt,
				      page_shift, size, flags);

		dma_sync_single_for_device(ddev, dma, size, DMA_TO_DEVICE);

		sg.length = ALIGN(npages * desc_size,
				  MLX5_UMR_MTT_ALIGNMENT);

		if (pages_mapped + pages_iter >= pages_to_map) {
			if (flags & MLX5_IB_UPD_XLT_ENABLE)
				wr.wr.send_flags |=
					MLX5_IB_SEND_UMR_ENABLE_MR |
					MLX5_IB_SEND_UMR_UPDATE_PD_ACCESS |
					MLX5_IB_SEND_UMR_UPDATE_TRANSLATION;
			if (flags & MLX5_IB_UPD_XLT_PD ||
			    flags & MLX5_IB_UPD_XLT_ACCESS)
				wr.wr.send_flags |=
					MLX5_IB_SEND_UMR_UPDATE_PD_ACCESS;
			if (flags & MLX5_IB_UPD_XLT_ADDR)
				wr.wr.send_flags |=
					MLX5_IB_SEND_UMR_UPDATE_TRANSLATION;
		}

		wr.offset = xlt_idx * desc_size;
		wr.xlt_size = sg.length;

		err = mlx5_ib_post_send_wait(dev, &wr);
	}
	dma_unmap_single(ddev, dma, size, DMA_TO_DEVICE);

free_xlt:
	if (use_emergency_page)
		mlx5_ib_put_xlt_emergency_page();
	else
		free_pages((unsigned long)xlt, get_order(size));

	return err;
}

static struct mlx5_ib_mr *create_capi_mr(struct ib_pd *pd, u64 virt_addr,
					 u64 length, int acc)
{
	struct mlx5_ib_dev *dev = to_mdev(pd->device);
	int inlen = MLX5_ST_SZ_BYTES(create_mkey_in);
	struct mlx5_core_dev *mdev = dev->mdev;
	struct mlx5_ib_mr *mr;
	void *mkc;
	u32 *in;
	int err;
	struct mlx5_ib_ucontext *mctx = to_mucontext(pd->uobject->context);

	mr = kzalloc(sizeof(*mr), GFP_KERNEL);
	if (!mr)
		return ERR_PTR(-ENOMEM);

	in = kzalloc(inlen, GFP_KERNEL);
	if (!in) {
		err = -ENOMEM;
		goto err_free;
	}

	mkc = MLX5_ADDR_OF(create_mkey_in, in, memory_key_mkey_entry);

	MLX5_SET(mkc, mkc, access_mode_1_0, MLX5_MKC_ACCESS_MODE_PA);
	MLX5_SET(mkc, mkc, a, !!(acc & IB_ACCESS_REMOTE_ATOMIC));
	MLX5_SET(mkc, mkc, rw, !!(acc & IB_ACCESS_REMOTE_WRITE));
	MLX5_SET(mkc, mkc, rr, !!(acc & IB_ACCESS_REMOTE_READ));
	MLX5_SET(mkc, mkc, lw, !!(acc & IB_ACCESS_LOCAL_WRITE));
	MLX5_SET(mkc, mkc, lr, 1);

	if (MLX5_CAP_GEN(dev->mdev, relaxed_ordering_write))
		MLX5_SET(mkc, mkc, relaxed_ordering_write,
			 !!(acc & IB_ACCESS_RELAXED_ORDERING));
	if (MLX5_CAP_GEN(dev->mdev, relaxed_ordering_read))
		MLX5_SET(mkc, mkc, relaxed_ordering_read,
			 !!(acc & IB_ACCESS_RELAXED_ORDERING));

	MLX5_SET(mkc, mkc, pd, to_mpd(pd)->pdn);
	MLX5_SET(mkc, mkc, qpn, 0xffffff);
	MLX5_SET64(mkc, mkc, start_addr, virt_addr);
	MLX5_SET64(mkc, mkc, len, length);
	MLX5_SET(mkc, mkc, pasid, mctx->cctx.pasid);
	MLX5_SET(mkc, mkc, ma_tranlation_mode, 3);

	err = mlx5_core_create_mkey(mdev, &mr->mmkey, in, inlen);
	if (err)
		goto err_in;

	kfree(in);
	mr->mmkey.type = MLX5_MKEY_MR;
	mr->live = 1;
	mr->ibmr.lkey = mr->mmkey.key;
	mr->ibmr.rkey = mr->mmkey.key;
	mr->umem = NULL;

	return mr;

err_in:
	kfree(in);

err_free:
	kfree(mr);

	return ERR_PTR(err);
}

/*
 * If ibmr is NULL it will be allocated by reg_create.
 * Else, the given ibmr will be used.
 */
static struct mlx5_ib_mr *reg_create(struct ib_mr *ibmr, struct ib_pd *pd,
				     u64 virt_addr, u64 length,
				     struct ib_umem *umem, int npages,
				     int page_shift, int access_flags,
				     bool populate)
{
	struct mlx5_ib_dev *dev = to_mdev(pd->device);
	struct mlx5_ib_mr *mr;
	__be64 *pas;
	void *mkc;
	int inlen;
	u32 *in;
	int err;
	bool pg_cap = !!(MLX5_CAP_GEN(dev->mdev, pg));

	mr = ibmr ? to_mmr(ibmr) : kzalloc(sizeof(*mr), GFP_KERNEL);
	if (!mr)
		return ERR_PTR(-ENOMEM);

	mr->ibmr.pd = pd;
	mr->access_flags = access_flags;

	inlen = MLX5_ST_SZ_BYTES(create_mkey_in);
	if (populate)
		inlen += sizeof(*pas) * roundup(npages, 2);
	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in) {
		err = -ENOMEM;
		goto err_1;
	}
	pas = (__be64 *)MLX5_ADDR_OF(create_mkey_in, in, klm_pas_mtt);
	if (populate && !(access_flags & IB_ACCESS_ON_DEMAND))
		mlx5_ib_populate_pas(dev, umem, page_shift, pas,
				     pg_cap ? MLX5_IB_MTT_PRESENT : 0);

	/* The pg_access bit allows setting the access flags
	 * in the page list submitted with the command. */
	MLX5_SET(create_mkey_in, in, pg_access, !!(pg_cap));

	mkc = MLX5_ADDR_OF(create_mkey_in, in, memory_key_mkey_entry);
	MLX5_SET(mkc, mkc, free, !populate);
	MLX5_SET(mkc, mkc, access_mode_1_0, MLX5_MKC_ACCESS_MODE_MTT);
	if (MLX5_CAP_GEN(dev->mdev, relaxed_ordering_write))
		MLX5_SET(mkc, mkc, relaxed_ordering_write,
			 !!(access_flags & IB_ACCESS_RELAXED_ORDERING));
	if (MLX5_CAP_GEN(dev->mdev, relaxed_ordering_read))
		MLX5_SET(mkc, mkc, relaxed_ordering_read,
			 !!(access_flags & IB_ACCESS_RELAXED_ORDERING));
	MLX5_SET(mkc, mkc, a, !!(access_flags & IB_ACCESS_REMOTE_ATOMIC));
	MLX5_SET(mkc, mkc, rw, !!(access_flags & IB_ACCESS_REMOTE_WRITE));
	MLX5_SET(mkc, mkc, rr, !!(access_flags & IB_ACCESS_REMOTE_READ));
	MLX5_SET(mkc, mkc, lw, !!(access_flags & IB_ACCESS_LOCAL_WRITE));
	MLX5_SET(mkc, mkc, lr, 1);
	MLX5_SET(mkc, mkc, umr_en, 1);
	if (access_flags & IB_EXP_ACCESS_TUNNELED_ATOMIC)
		MLX5_SET(mkc, mkc, tunneled_atomic, 1);

	MLX5_SET64(mkc, mkc, start_addr, virt_addr);
	MLX5_SET64(mkc, mkc, len, length);
	MLX5_SET(mkc, mkc, pd, to_mpd(pd)->pdn);
	MLX5_SET(mkc, mkc, bsf_octword_size, 0);
	MLX5_SET(mkc, mkc, translations_octword_size,
		 get_octo_len(virt_addr, length, page_shift));
	MLX5_SET(mkc, mkc, log_page_size, page_shift);
	MLX5_SET(mkc, mkc, qpn, 0xffffff);
	if (populate) {
		MLX5_SET(create_mkey_in, in, translations_octword_actual_size,
			 get_octo_len(virt_addr, length, page_shift));
	}

	if (umem->ib_peer_mem && MLX5_CAP_GEN(dev->mdev, ats))
		MLX5_SET(mkc, mkc, ma_tranlation_mode, 1);

	err = mlx5_ib_create_mkey(dev, &mr->mmkey, in, inlen);
	if (err) {
		mlx5_ib_warn(dev, "create mkey failed\n");
		goto err_2;
	}
	mr->mmkey.type = MLX5_MKEY_MR;
	mr->desc_size = sizeof(struct mlx5_mtt);
	mr->dev = dev;
	kvfree(in);

	mlx5_ib_dbg(dev, "mkey = 0x%x\n", mr->mmkey.key);

	return mr;

err_2:
	kvfree(in);

err_1:
	if (!ibmr)
		kfree(mr);

	return ERR_PTR(err);
}

static void set_mr_fields(struct mlx5_ib_dev *dev, struct mlx5_ib_mr *mr,
			  int npages, u64 length, int access_flags)
{
	mr->npages = npages;
	atomic_add(npages, &dev->mdev->priv.reg_pages);
	mr->ibmr.lkey = mr->mmkey.key;
	mr->ibmr.rkey = mr->mmkey.key;
	mr->ibmr.length = length;
	mr->access_flags = access_flags;
}

static struct ib_mr *mlx5_ib_get_dm_mr(struct ib_pd *pd, u64 start_addr,
				       u64 length, int acc, int mode)
{
	struct mlx5_ib_dev *dev = to_mdev(pd->device);
	int inlen = MLX5_ST_SZ_BYTES(create_mkey_in);
	struct mlx5_ib_mr *mr;
	void *mkc;
	u32 *in;
	int err;

	mr = kzalloc(sizeof(*mr), GFP_KERNEL);
	if (!mr)
		return ERR_PTR(-ENOMEM);

	in = kzalloc(inlen, GFP_KERNEL);
	if (!in) {
		err = -ENOMEM;
		goto err_free;
	}

	mkc = MLX5_ADDR_OF(create_mkey_in, in, memory_key_mkey_entry);

	MLX5_SET(mkc, mkc, access_mode_1_0, mode & 0x3);
	MLX5_SET(mkc, mkc, access_mode_4_2, (mode >> 2) & 0x7);
	MLX5_SET(mkc, mkc, a, !!(acc & IB_ACCESS_REMOTE_ATOMIC));
	MLX5_SET(mkc, mkc, rw, !!(acc & IB_ACCESS_REMOTE_WRITE));
	MLX5_SET(mkc, mkc, rr, !!(acc & IB_ACCESS_REMOTE_READ));
	MLX5_SET(mkc, mkc, lw, !!(acc & IB_ACCESS_LOCAL_WRITE));
	MLX5_SET(mkc, mkc, lr, 1);

	if (MLX5_CAP_GEN(dev->mdev, relaxed_ordering_write))
		MLX5_SET(mkc, mkc, relaxed_ordering_write,
			 !!(acc & IB_ACCESS_RELAXED_ORDERING));
	if (MLX5_CAP_GEN(dev->mdev, relaxed_ordering_read))
		MLX5_SET(mkc, mkc, relaxed_ordering_read,
			 !!(acc & IB_ACCESS_RELAXED_ORDERING));

	MLX5_SET64(mkc, mkc, len, length);
	MLX5_SET(mkc, mkc, pd, to_mpd(pd)->pdn);
	MLX5_SET(mkc, mkc, qpn, 0xffffff);
	MLX5_SET64(mkc, mkc, start_addr, start_addr);

	err = mlx5_ib_create_mkey(dev, &mr->mmkey, in, inlen);
	if (err)
		goto err_in;

	kfree(in);

	mr->umem = NULL;
	set_mr_fields(dev, mr, 0, length, acc);

	return &mr->ibmr;

err_in:
	kfree(in);

err_free:
	kfree(mr);

	return ERR_PTR(err);
}

int mlx5_ib_advise_mr(struct ib_pd *pd,
		      enum ib_uverbs_advise_mr_advice advice,
		      u32 flags,
		      struct ib_sge *sg_list,
		      u32 num_sge,
		      struct uverbs_attr_bundle *attrs)
{
	if (advice != IB_UVERBS_ADVISE_MR_ADVICE_PREFETCH &&
	    advice != IB_UVERBS_ADVISE_MR_ADVICE_PREFETCH_WRITE)
		return -EOPNOTSUPP;

	return mlx5_ib_advise_mr_prefetch(pd, advice, flags,
					 sg_list, num_sge);
}

struct ib_mr *mlx5_ib_reg_dm_mr(struct ib_pd *pd, struct ib_dm *dm,
				struct ib_dm_mr_attr *attr,
				struct uverbs_attr_bundle *attrs)
{
	struct mlx5_ib_dm *mdm = to_mdm(dm);
	struct mlx5_core_dev *dev = to_mdev(dm->device)->mdev;
	u64 start_addr = mdm->dev_addr + attr->offset;
	int mode;

	switch (mdm->type) {
	case MLX5_IB_UAPI_DM_TYPE_MEMIC:
		if (attr->access_flags & ~MLX5_IB_DM_MEMIC_ALLOWED_ACCESS)
			return ERR_PTR(-EINVAL);

		mode = MLX5_MKC_ACCESS_MODE_MEMIC;
		start_addr -= pci_resource_start(dev->pdev, 0);
		break;
	case MLX5_IB_UAPI_DM_TYPE_STEERING_SW_ICM:
	case MLX5_IB_UAPI_DM_TYPE_HEADER_MODIFY_SW_ICM:
		if (attr->access_flags & ~MLX5_IB_DM_SW_ICM_ALLOWED_ACCESS)
			return ERR_PTR(-EINVAL);

		mode = MLX5_MKC_ACCESS_MODE_SW_ICM;
		break;
	default:
		return ERR_PTR(-EINVAL);
	}

	return mlx5_ib_get_dm_mr(pd, start_addr, attr->length,
				 attr->access_flags, mode);
}

struct ib_mr *mlx5_ib_reg_user_mr(struct ib_pd *pd,
				  struct ib_mr_init_attr *attr,
				  struct ib_udata *udata)
{
	struct mlx5_ib_dev *dev = to_mdev(pd->device);
	struct mlx5_ib_mr *mr = NULL;
	bool populate_mtts = false;
	struct ib_umem *umem;
	int page_shift;
	int npages;
	int ncont;
	int order;
	int err;
	struct ib_peer_memory_client *ib_peer_mem;
	struct ib_dm_mr_attr dm_mr_attr = {0};
	struct mlx5_ib_peer_id *mlx5_ib_peer_id = NULL;
	int access_flags = attr->access_flags;
	u64 length = attr->length;
	u64 start = attr->start;
	u64 virt_addr = attr->hca_va;

	if (!IS_ENABLED(CONFIG_INFINIBAND_USER_MEM))
		return ERR_PTR(-EOPNOTSUPP);

	if (access_flags & IB_EXP_ACCESS_PHYSICAL_ADDR)
		return mlx5_ib_phys_addr(pd, length, virt_addr, access_flags);

	if (attr->mr_type == IB_MR_TYPE_DM) {
		if (!attr->dm)
			return ERR_PTR(-EINVAL);

		dm_mr_attr.length = length;
		dm_mr_attr.offset = start;
		dm_mr_attr.access_flags = access_flags;

		return mlx5_ib_reg_dm_mr(pd, attr->dm, &dm_mr_attr, NULL);
	}

	mlx5_ib_dbg(dev, "start 0x%llx, virt_addr 0x%llx, length 0x%llx, access_flags 0x%x\n",
		    start, virt_addr, length, access_flags);

	if (IS_ENABLED(CONFIG_INFINIBAND_ON_DEMAND_PAGING) && !start &&
	    length == U64_MAX  && !mlx5_ib_capi_enabled(dev)) {
		if (!(access_flags & IB_ACCESS_ON_DEMAND) ||
		    !(dev->odp_caps.general_caps & IB_ODP_SUPPORT_IMPLICIT))
			return ERR_PTR(-EINVAL);

		mr = mlx5_ib_alloc_implicit_mr(to_mpd(pd), udata, access_flags);
		if (IS_ERR(mr))
			return ERR_CAST(mr);
		atomic_inc(&dev->odp_stats.num_odp_mrs);

		return &mr->ibmr;

	}

	if ( IS_ENABLED(CONFIG_INFINIBAND_ON_DEMAND_PAGING) && mlx5_ib_capi_enabled(dev) && (access_flags & IB_ACCESS_ON_DEMAND)) {
		/* Use zero address and zero length for CAPI's umem struct */
		umem = ib_umem_get(udata, 0, 0, access_flags, 0, 0);
		err = PTR_ERR_OR_ZERO(umem);
	} else {
		err = mr_umem_get(dev, udata, start, length, access_flags, &umem, &npages,
				  &page_shift, &ncont, &order,
				  IB_PEER_MEM_ALLOW | IB_PEER_MEM_INVAL_SUPP);
	}

	if (err < 0)
		return ERR_PTR(err);

	ib_peer_mem = umem->ib_peer_mem;
	if (ib_peer_mem) {
		if (access_flags & IB_ACCESS_ON_DEMAND) {
			err = -EINVAL;
			mlx5_ib_dbg(dev, "Peer memory with ODP is not supported\n");
			goto error;
		}
		mlx5_ib_peer_id = kzalloc(sizeof(*mlx5_ib_peer_id), GFP_KERNEL);
		if (!mlx5_ib_peer_id) {
			err = -ENOMEM;
			goto error;
		}

		init_completion(&mlx5_ib_peer_id->comp);
		err = ib_umem_activate_invalidation_notifier(umem, mlx5_invalidate_umem,
							     mlx5_ib_peer_id);
		if (err)
			goto error;
	}

	if (access_flags & IB_EXP_ACCESS_TUNNELED_ATOMIC) {
		if (!mlx5_ib_tunnel_atomic_supported(dev)) {
			err = -EINVAL;
			mlx5_ib_dbg(dev, "Tunneled atomic is not supported");
			goto error;
		}

		if (access_flags & IB_ACCESS_ON_DEMAND) {
			err = -EINVAL;
			mlx5_ib_dbg(dev, "Tunneled atomic and ODP can't both be enabled");
			goto error;
		}
	}

	if (ib_peer_mem && MLX5_CAP_GEN(dev->mdev, ats)) {
		;
	} else if ((access_flags & IB_ACCESS_ON_DEMAND) &&
	    mlx5_ib_capi_enabled(dev)) {
		mr = create_capi_mr(pd, virt_addr, length, access_flags);
		if (IS_ERR(mr)) {
			err = PTR_ERR(mr);
			mlx5_ib_warn(dev, "failed to create capi mr. err = %d\n", err);
			goto error;
		}

		populate_mtts = true;
	} else if (access_flags & IB_ACCESS_RELAXED_ORDERING) {
		;
	} else if (use_umr(dev, order)) {
		mr = alloc_mr_from_cache(pd, umem, virt_addr, length, ncont,
					 page_shift, order, access_flags);
		if (PTR_ERR(mr) == -EAGAIN) {
			mlx5_ib_dbg(dev, "cache empty for order %d\n", order);
			mr = NULL;
		}
		populate_mtts = false;
	} else if (!MLX5_CAP_GEN(dev->mdev, umr_extended_translation_offset)) {
		if (access_flags & IB_ACCESS_ON_DEMAND) {
			err = -EINVAL;
			pr_err("Got MR registration for ODP MR > 512MB, not supported for Connect-IB\n");
			goto error;
		}

		if (use_klm(order)) {
			mr = reg_klm(pd, umem, virt_addr, length,
				     ncont, page_shift, order,
				     access_flags);
			if (IS_ERR(mr)) {
				mlx5_ib_dbg(dev, "reg_klm failed for order %d (%ld)",
					    order, PTR_ERR(mr));
				mr = NULL;
			}
		}
		populate_mtts = true;
	}

	if (!mr) {
		if (!umr_can_modify_entity_size(dev))
			populate_mtts = true;
		mutex_lock(&dev->slow_path_mutex);
		mr = reg_create(NULL, pd, virt_addr, length, umem, ncont,
				page_shift, access_flags, populate_mtts);
		mutex_unlock(&dev->slow_path_mutex);
	}

	if (IS_ERR(mr)) {
		err = PTR_ERR(mr);
		goto error;
	}

	mlx5_ib_dbg(dev, "mkey 0x%x\n", mr->mmkey.key);

	mr->umem = umem;
	set_mr_fields(dev, mr, npages, length, access_flags);
	atomic_set(&mr->invalidated, 0);
	if (ib_peer_mem) {
		init_completion(&mr->invalidation_comp);
		mlx5_ib_peer_id->mr = mr;
		mr->peer_id = mlx5_ib_peer_id;
		complete(&mlx5_ib_peer_id->comp);
	}

	update_odp_mr(mr, dev);

	if (!populate_mtts) {
		int update_xlt_flags = MLX5_IB_UPD_XLT_ENABLE;

		if (access_flags & IB_ACCESS_ON_DEMAND)
			update_xlt_flags |= MLX5_IB_UPD_XLT_ZAP;

		err = mlx5_ib_update_xlt(mr, 0, 0, ncont, page_shift,
					 update_xlt_flags);

		if (err) {
			dereg_mr(dev, mr);
			return ERR_PTR(err);
		}
	}

	mr->free = 0;
	if (IS_ENABLED(CONFIG_INFINIBAND_ON_DEMAND_PAGING)) {
		mr->live = 1;
		atomic_set(&mr->num_pending_prefetch, 0);
	}

	return &mr->ibmr;
error:
	if (mlx5_ib_peer_id)
		complete(&mlx5_ib_peer_id->comp);

	ib_umem_release(umem);
	kfree(mlx5_ib_peer_id);
	return ERR_PTR(err);
}

static int unreg_umr(struct mlx5_ib_dev *dev, struct mlx5_ib_mr *mr)
{
	struct mlx5_core_dev *mdev = dev->mdev;
	struct mlx5_umr_wr umrwr = {};

	if (mdev->state == MLX5_DEVICE_STATE_INTERNAL_ERROR || mr->free)
		return 0;

	umrwr.wr.send_flags = MLX5_IB_SEND_UMR_DISABLE_MR |
			      MLX5_IB_SEND_UMR_FAIL_IF_FREE;
	umrwr.wr.opcode = MLX5_IB_WR_UMR;
	umrwr.mkey = mr->mmkey.key;
	mr->free = 1;

	/* Need to explicitly clear the atomic bit in the mkey */
	if (mlx5_ib_tunnel_atomic_supported(dev) &&
	    (mr->access_flags & IB_EXP_ACCESS_TUNNELED_ATOMIC))
		umrwr.access_flags |= IB_EXP_ACCESS_TUNNELED_ATOMIC;

	return mlx5_ib_post_send_wait(dev, &umrwr);
}

static int rereg_umr(struct ib_pd *pd, struct mlx5_ib_mr *mr,
		     int access_flags, int flags)
{
	struct mlx5_ib_dev *dev = to_mdev(pd->device);
	struct mlx5_umr_wr umrwr = {};
	int err;

	umrwr.wr.send_flags = MLX5_IB_SEND_UMR_FAIL_IF_FREE;

	umrwr.wr.opcode = MLX5_IB_WR_UMR;
	umrwr.mkey = mr->mmkey.key;

	if (flags & IB_MR_REREG_PD || flags & IB_MR_REREG_ACCESS) {
		umrwr.pd = pd;
		umrwr.access_flags = access_flags;
		umrwr.wr.send_flags |= MLX5_IB_SEND_UMR_UPDATE_PD_ACCESS;
	}

	err = mlx5_ib_post_send_wait(dev, &umrwr);

	return err;
}

int mlx5_ib_rereg_user_mr(struct ib_mr *ib_mr, int flags, u64 start,
			  u64 length, u64 virt_addr, int new_access_flags,
			  struct ib_pd *new_pd, struct ib_udata *udata)
{
	struct mlx5_ib_dev *dev = to_mdev(ib_mr->device);
	struct mlx5_ib_mr *mr = to_mmr(ib_mr);
	struct ib_pd *pd = (flags & IB_MR_REREG_PD) ? new_pd : ib_mr->pd;
	int access_flags = flags & IB_MR_REREG_ACCESS ?
			    new_access_flags :
			    mr->access_flags;
	int page_shift = 0;
	int upd_flags = 0;
	int npages = 0;
	int ncont = 0;
	int order = 0;
	u64 addr, len;
	int err;

	mlx5_ib_dbg(dev, "start 0x%llx, virt_addr 0x%llx, length 0x%llx, access_flags 0x%x\n",
		    start, virt_addr, length, access_flags);

	atomic_sub(mr->npages, &dev->mdev->priv.reg_pages);

	/* Peer memory isn't supported */
	 if (mr->umem->ib_peer_mem)
		return -ENOTSUPP;

	if (!mr->umem)
		return -EINVAL;

	if (flags & IB_MR_REREG_TRANS) {
		addr = virt_addr;
		len = length;
	} else {
		addr = mr->umem->address;
		len = mr->umem->length;
	}

	if (flags != IB_MR_REREG_PD) {
		/*
		 * Replace umem. This needs to be done whether or not UMR is
		 * used.
		 */
		flags |= IB_MR_REREG_TRANS;
		ib_umem_release(mr->umem);
		mr->umem = NULL;
		err = mr_umem_get(dev, udata, addr, len, access_flags,
				  &mr->umem, &npages, &page_shift, &ncont,
				  &order, 0);
		if (err)
			goto err;
	}

	if ((flags & IB_MR_REREG_TRANS && !use_umr_mtt_update(mr, addr, len)) ||
	    (access_flags & IB_ACCESS_RELAXED_ORDERING)) {
		/*
		 * UMR can't be used - MKey needs to be replaced.
		 */
		if (mr->allocated_from_cache)
			err = unreg_umr(dev, mr);
		else
			err = destroy_mkey(dev, mr);
		if (err)
			goto err;

		mr = reg_create(ib_mr, pd, addr, len, mr->umem, ncont,
				page_shift, access_flags, true);

		if (IS_ERR(mr)) {
			err = PTR_ERR(mr);
			mr = to_mmr(ib_mr);
			goto err;
		}

		mr->allocated_from_cache = 0;
		if (IS_ENABLED(CONFIG_INFINIBAND_ON_DEMAND_PAGING))
			mr->live = 1;
	} else {
		/*
		 * Send a UMR WQE
		 */
		mr->ibmr.pd = pd;
		mr->access_flags = access_flags;
		mr->mmkey.iova = addr;
		mr->mmkey.size = len;
		mr->mmkey.pd = to_mpd(pd)->pdn;

		if (flags & IB_MR_REREG_TRANS) {
			upd_flags = MLX5_IB_UPD_XLT_ADDR;
			if (flags & IB_MR_REREG_PD)
				upd_flags |= MLX5_IB_UPD_XLT_PD;
			if (flags & IB_MR_REREG_ACCESS)
				upd_flags |= MLX5_IB_UPD_XLT_ACCESS;
			err = mlx5_ib_update_xlt(mr, 0, 0, npages,
						 page_shift, upd_flags);
		} else {
			err = rereg_umr(pd, mr, access_flags, flags);
		}

		if (err)
			goto err;
	}

	set_mr_fields(dev, mr, npages, len, access_flags);

	update_odp_mr(mr, NULL);
	return 0;

err:
	if (mr->umem) {
		ib_umem_release(mr->umem);
		mr->umem = NULL;
	}
	clean_mr(dev, mr);
	return err;
}

static int
mlx5_alloc_priv_descs(struct ib_device *device,
		      struct mlx5_ib_mr *mr,
		      int ndescs,
		      int desc_size)
{
	int size = ndescs * desc_size;
	int add_size;
	int ret;

	add_size = max_t(int, MLX5_UMR_ALIGN - ARCH_KMALLOC_MINALIGN, 0);

	mr->descs_alloc = kzalloc(size + add_size, GFP_KERNEL);
	if (!mr->descs_alloc)
		return -ENOMEM;

	mr->descs = PTR_ALIGN(mr->descs_alloc, MLX5_UMR_ALIGN);

	mr->desc_map = dma_map_single(device->dev.parent, mr->descs,
				      size, DMA_TO_DEVICE);
	if (dma_mapping_error(device->dev.parent, mr->desc_map)) {
		ret = -ENOMEM;
		goto err;
	}

	return 0;
err:
	kfree(mr->descs_alloc);

	return ret;
}

static void
mlx5_free_priv_descs(struct mlx5_ib_mr *mr)
{
	if (mr->descs) {
		struct ib_device *device = mr->ibmr.device;
		int size = mr->max_descs * mr->desc_size;

		dma_unmap_single(device->dev.parent, mr->desc_map,
				 size, DMA_TO_DEVICE);
		kfree(mr->descs_alloc);
		mr->descs = NULL;
	}
}

static void clean_mr(struct mlx5_ib_dev *dev, struct mlx5_ib_mr *mr)
{
	int allocated_from_cache = mr->allocated_from_cache;
	int i;

	if (mr->sig) {
		if (mlx5_core_destroy_psv(dev->mdev,
					  mr->sig->psv_memory.psv_idx))
			mlx5_ib_warn(dev, "failed to destroy mem psv %d\n",
				     mr->sig->psv_memory.psv_idx);
		if (mlx5_core_destroy_psv(dev->mdev,
					  mr->sig->psv_wire.psv_idx))
			mlx5_ib_warn(dev, "failed to destroy wire psv %d\n",
				     mr->sig->psv_wire.psv_idx);
		kfree(mr->sig);
		mr->sig = NULL;
	}

	mlx5_free_priv_descs(mr);

	if (!allocated_from_cache) {
		for (i = 0; i < mr->nchild; ++i)
			mlx5_mr_cache_free(dev, mr->children[i]);
		kfree(mr->children);

		destroy_mkey(dev, mr);
		kfree(mr->peer_id);
		mr->peer_id = NULL;
	}
}

static void dereg_mr(struct mlx5_ib_dev *dev, struct mlx5_ib_mr *mr)
{
	int npages = mr->npages;
	struct ib_umem *umem = mr->umem;

	if (is_odp_mr(mr)) {
		struct ib_umem_odp *umem_odp = to_ib_umem_odp(umem);

		/* Prevent new page faults and
		 * prefetch requests from succeeding
		 */
		mr->live = 0;

		/* dequeue pending prefetch requests for the mr */
		if (atomic_read(&mr->num_pending_prefetch))
			flush_workqueue(system_unbound_wq);
		WARN_ON(atomic_read(&mr->num_pending_prefetch));

		/* Wait for all running page-fault handlers to finish. */
		synchronize_srcu(&dev->mr_srcu);
		/* Destroy all page mappings */
		if (umem_odp->page_list)
			mlx5_ib_invalidate_range(umem_odp, ib_umem_start(umem),
						 ib_umem_end(umem));
		else
			mlx5_ib_free_implicit_mr(mr);
		atomic_dec(&dev->odp_stats.num_odp_mrs);
		/*
		 * We kill the umem before the MR for ODP,
		 * so that there will not be any invalidations in
		 * flight, looking at the *mr struct.
		 */
		ib_umem_release(umem);
		atomic_sub(npages, &dev->mdev->priv.reg_pages);

		/* Avoid double-freeing the umem. */
		umem = NULL;
	}

	clean_mr(dev, mr);

	if (!mr->allocated_from_cache) {
		if (umem) {
			ib_umem_release(umem);
			atomic_sub(npages, &dev->mdev->priv.reg_pages);
		}
		/* Skip mr free in peer invalidation flow as it's freed by dereg_mr */
		if (umem && umem->invalidation_ctx &&
			umem->invalidation_ctx->peer_callback)
			return;
		kfree(mr);
	} else {
		/*
		 * We should unregister the DMA address from the HCA before
		 * we remove the DMA mapping
		 */
		unreg_umr(dev, mr);
		kfree(mr->peer_id);
		mr->peer_id = NULL;
		if (umem) {
			ib_umem_release(umem);
			/* Avoid double-freeing the umem in the next dereg */
			mr->umem = NULL;
			atomic_sub(npages, &dev->mdev->priv.reg_pages);
		}
	}
}

int mlx5_ib_invalidate_mr(struct ib_mr *ibmr)
{
	struct mlx5_ib_mr *mmr = to_mmr(ibmr);

	if (ibmr->type == IB_MR_TYPE_INTEGRITY) {
		dereg_mr(to_mdev(mmr->mtt_mr->ibmr.device), mmr->mtt_mr);
		dereg_mr(to_mdev(mmr->klm_mr->ibmr.device), mmr->klm_mr);
	}

	dereg_mr(to_mdev(ibmr->device), mmr);

	return 0;
}

int mlx5_ib_dereg_mr(struct ib_mr *ibmr, struct ib_udata *udata)
{
	struct mlx5_ib_dev *dev = to_mdev(ibmr->device);
	struct mlx5_ib_mr *mr = to_mmr(ibmr);
	int ret = 0;
	int allocated_from_cache = mr->allocated_from_cache;

	if (atomic_inc_return(&mr->invalidated) > 1) {
		/* In case there is inflight invalidation call pending for its termination */
		wait_for_completion(&mr->invalidation_comp);
		if (!allocated_from_cache)
			kfree(mr);
	} else {
		ret = mlx5_ib_invalidate_mr(ibmr);
		if (ret) {
			atomic_set(&mr->invalidated, 0);
			return ret;
		}
	}

	if (allocated_from_cache) {
		atomic_set(&mr->invalidated, 0);
		mlx5_mr_cache_free(dev, mr);
	}

	return 0;
}

static void mlx5_set_umr_free_mkey(struct ib_pd *pd, u32 *in, int ndescs,
				   int access_mode, int page_shift)
{
	void *mkc;

	mkc = MLX5_ADDR_OF(create_mkey_in, in, memory_key_mkey_entry);

	MLX5_SET(mkc, mkc, free, 1);
	MLX5_SET(mkc, mkc, qpn, 0xffffff);
	MLX5_SET(mkc, mkc, pd, to_mpd(pd)->pdn);
	MLX5_SET(mkc, mkc, translations_octword_size, ndescs);
	MLX5_SET(mkc, mkc, access_mode_1_0, access_mode & 0x3);
	MLX5_SET(mkc, mkc, access_mode_4_2, (access_mode >> 2) & 0x7);
	MLX5_SET(mkc, mkc, umr_en, 1);
	MLX5_SET(mkc, mkc, log_page_size, page_shift);
}

static int _mlx5_alloc_mkey_descs(struct ib_pd *pd, struct mlx5_ib_mr *mr,
				  int ndescs, int desc_size, int page_shift,
				  int access_mode, u32 *in, int inlen)
{
	struct mlx5_ib_dev *dev = to_mdev(pd->device);
	int err;

	mr->access_mode = access_mode;
	mr->desc_size = desc_size;
	mr->max_descs = ndescs;

	err = mlx5_alloc_priv_descs(pd->device, mr, ndescs, desc_size);
	if (err)
		return err;

	mlx5_set_umr_free_mkey(pd, in, ndescs, access_mode, page_shift);

	err = mlx5_ib_create_mkey(dev, &mr->mmkey, in, inlen);
	if (err)
		goto err_free_descs;

	mr->mmkey.type = MLX5_MKEY_MR;
	mr->ibmr.lkey = mr->mmkey.key;
	mr->ibmr.rkey = mr->mmkey.key;

	return 0;

err_free_descs:
	mlx5_free_priv_descs(mr);
	return err;
}

static struct mlx5_ib_mr *mlx5_ib_alloc_pi_mr(struct ib_pd *pd,
				u32 max_num_sg, u32 max_num_meta_sg,
				int desc_size, int access_mode)
{
	int inlen = MLX5_ST_SZ_BYTES(create_mkey_in);
	int ndescs = ALIGN(max_num_sg + max_num_meta_sg, 4);
	int page_shift = 0;
	struct mlx5_ib_mr *mr;
	u32 *in;
	int err;

	mr = kzalloc(sizeof(*mr), GFP_KERNEL);
	if (!mr)
		return ERR_PTR(-ENOMEM);

	mr->ibmr.pd = pd;
	mr->ibmr.device = pd->device;

	in = kzalloc(inlen, GFP_KERNEL);
	if (!in) {
		err = -ENOMEM;
		goto err_free;
	}

	if (access_mode == MLX5_MKC_ACCESS_MODE_MTT)
		page_shift = PAGE_SHIFT;

	err = _mlx5_alloc_mkey_descs(pd, mr, ndescs, desc_size, page_shift,
				     access_mode, in, inlen);
	if (err)
		goto err_free_in;

	mr->umem = NULL;
	kfree(in);

	return mr;

err_free_in:
	kfree(in);
err_free:
	kfree(mr);
	return ERR_PTR(err);
}

static int mlx5_alloc_mem_reg_descs(struct ib_pd *pd, struct mlx5_ib_mr *mr,
				    int ndescs, u32 *in, int inlen)
{
	return _mlx5_alloc_mkey_descs(pd, mr, ndescs, sizeof(struct mlx5_mtt),
				      PAGE_SHIFT, MLX5_MKC_ACCESS_MODE_MTT, in,
				      inlen);
}

static int mlx5_alloc_sg_gaps_descs(struct ib_pd *pd, struct mlx5_ib_mr *mr,
				    int ndescs, u32 *in, int inlen)
{
	return _mlx5_alloc_mkey_descs(pd, mr, ndescs, sizeof(struct mlx5_klm),
				      0, MLX5_MKC_ACCESS_MODE_KLMS, in, inlen);
}

static int mlx5_alloc_descs_common(struct ib_pd *pd, struct mlx5_ib_mr *mr,
				     int ndescs, u32 *in, int inlen)
{
	struct mlx5_ib_dev *dev = to_mdev(pd->device);
	void *mkc;
	int err;

	mkc = MLX5_ADDR_OF(create_mkey_in, in, memory_key_mkey_entry);
	MLX5_SET(mkc, mkc, free, 1);
	MLX5_SET(mkc, mkc, qpn, 0xffffff);
	MLX5_SET(mkc, mkc, pd, to_mpd(pd)->pdn);

	MLX5_SET(mkc, mkc, access_mode_1_0, mr->access_mode & 0x3);
	MLX5_SET(mkc, mkc, access_mode_4_2, (mr->access_mode >> 2) & 0x7);
	MLX5_SET(mkc, mkc, umr_en, 1);

	mr->ibmr.device = pd->device;
	err = mlx5_core_create_mkey(dev->mdev, &mr->mmkey, in, inlen);
	if (err)
		return err;

	mr->mmkey.type = MLX5_MKEY_MR_USER;
	mr->ibmr.pd = pd;
	mr->ibmr.lkey = mr->mmkey.key;
	mr->ibmr.rkey = mr->mmkey.key;
	mr->umem = NULL;
	mr->live = 1;

	return err;
}

static int mlx5_alloc_indirect_descs(struct ib_pd *pd, struct mlx5_ib_mr *mr,
				    int ndescs, u32 *in, int inlen,
				    u32 max_num_sg)
{
	void *mkc;

	mkc = MLX5_ADDR_OF(create_mkey_in, in, memory_key_mkey_entry);
	MLX5_SET(mkc, mkc, translations_octword_size, ALIGN(max_num_sg + 1, 4));

	mr->access_mode = MLX5_MKC_ACCESS_MODE_KLMS | MLX5_PERM_UMR_EN;
	mr->max_descs = ndescs;
	mr->desc_size = 0;

	return mlx5_alloc_descs_common(pd, mr, ndescs, in, inlen);

}


static int mlx5_alloc_fixed_size_descs(struct ib_pd *pd, struct mlx5_ib_mr *mr,
				       int ndescs, u32 *in, int inlen,
				       u32 max_num_sg)
{
	void *mkc;

	mkc = MLX5_ADDR_OF(create_mkey_in, in, memory_key_mkey_entry);
	MLX5_SET(mkc, mkc, translations_octword_size, ALIGN(max_num_sg + 1, 4));
	MLX5_SET(mkc, mkc, log_page_size, 31);

	mr->access_mode = MLX5_MKC_ACCESS_MODE_KSM | MLX5_PERM_UMR_EN;
	mr->max_descs = ndescs;
	mr->desc_size = 0;

	return mlx5_alloc_descs_common(pd, mr, ndescs, in, inlen);
}


static int mlx5_alloc_integrity_descs(struct ib_pd *pd, struct mlx5_ib_mr *mr,
				      int max_num_sg, int max_num_meta_sg,
				      u32 *in, int inlen)
{
	struct mlx5_ib_dev *dev = to_mdev(pd->device);
	u32 psv_index[2];
	void *mkc;
	int err;

	mr->sig = kzalloc(sizeof(*mr->sig), GFP_KERNEL);
	if (!mr->sig)
		return -ENOMEM;

	/* create mem & wire PSVs */
	err = mlx5_core_create_psv(dev->mdev, to_mpd(pd)->pdn, 2, psv_index);
	if (err)
		goto err_free_sig;

	mr->sig->psv_memory.psv_idx = psv_index[0];
	mr->sig->psv_wire.psv_idx = psv_index[1];

	mr->sig->sig_status_checked = true;
	mr->sig->sig_err_exists = false;
	/* Next UMR, Arm SIGERR */
	++mr->sig->sigerr_count;
	mr->klm_mr = mlx5_ib_alloc_pi_mr(pd, max_num_sg, max_num_meta_sg,
					 sizeof(struct mlx5_klm),
					 MLX5_MKC_ACCESS_MODE_KLMS);
	if (IS_ERR(mr->klm_mr)) {
		err = PTR_ERR(mr->klm_mr);
		goto err_destroy_psv;
	}
	mr->mtt_mr = mlx5_ib_alloc_pi_mr(pd, max_num_sg, max_num_meta_sg,
					 sizeof(struct mlx5_mtt),
					 MLX5_MKC_ACCESS_MODE_MTT);
	if (IS_ERR(mr->mtt_mr)) {
		err = PTR_ERR(mr->mtt_mr);
		goto err_free_klm_mr;
	}

	/* Set bsf descriptors for mkey */
	mkc = MLX5_ADDR_OF(create_mkey_in, in, memory_key_mkey_entry);
	MLX5_SET(mkc, mkc, bsf_en, 1);
	MLX5_SET(mkc, mkc, bsf_octword_size, MLX5_MKEY_BSF_OCTO_SIZE);

	err = _mlx5_alloc_mkey_descs(pd, mr, 4, sizeof(struct mlx5_klm), 0,
				     MLX5_MKC_ACCESS_MODE_KLMS, in, inlen);
	if (err)
		goto err_free_mtt_mr;

	return 0;

err_free_mtt_mr:
	dereg_mr(to_mdev(mr->mtt_mr->ibmr.device), mr->mtt_mr);
	mr->mtt_mr = NULL;
err_free_klm_mr:
	dereg_mr(to_mdev(mr->klm_mr->ibmr.device), mr->klm_mr);
	mr->klm_mr = NULL;
err_destroy_psv:
	if (mlx5_core_destroy_psv(dev->mdev, mr->sig->psv_memory.psv_idx))
		mlx5_ib_warn(dev, "failed to destroy mem psv %d\n",
			     mr->sig->psv_memory.psv_idx);
	if (mlx5_core_destroy_psv(dev->mdev, mr->sig->psv_wire.psv_idx))
		mlx5_ib_warn(dev, "failed to destroy wire psv %d\n",
			     mr->sig->psv_wire.psv_idx);
err_free_sig:
	kfree(mr->sig);

	return err;
}

static struct ib_mr *__mlx5_ib_alloc_mr(struct ib_pd *pd,
					enum ib_mr_type mr_type, u32 max_num_sg,
					u32 max_num_meta_sg)
{
	struct mlx5_ib_dev *dev = to_mdev(pd->device);
	int inlen = MLX5_ST_SZ_BYTES(create_mkey_in);
	int ndescs = ALIGN(max_num_sg, 4);
	struct mlx5_ib_mr *mr;
	u32 *in;
	int err;

	mr = kzalloc(sizeof(*mr), GFP_KERNEL);
	if (!mr)
		return ERR_PTR(-ENOMEM);

	in = kzalloc(inlen, GFP_KERNEL);
	if (!in) {
		err = -ENOMEM;
		goto err_free;
	}

	mr->ibmr.device = pd->device;
	mr->umem = NULL;

	switch (mr_type) {
	case IB_MR_TYPE_MEM_REG:
		err = mlx5_alloc_mem_reg_descs(pd, mr, ndescs, in, inlen);
		break;
	case IB_MR_TYPE_SG_GAPS:
		err = mlx5_alloc_sg_gaps_descs(pd, mr, ndescs, in, inlen);
		break;
	case IB_MR_TYPE_INTEGRITY:
		err = mlx5_alloc_integrity_descs(pd, mr, max_num_sg,
						 max_num_meta_sg, in, inlen);
		break;
	case IB_MR_INDIRECT_REG:
		err = mlx5_alloc_indirect_descs(pd, mr, ndescs, in,
						inlen, max_num_sg);
		break;
	case IB_MR_TYPE_FIXED_SIZE:
		err = mlx5_alloc_fixed_size_descs(pd, mr, ndescs, in,
						  inlen, max_num_sg);
		break;
	default:
		mlx5_ib_warn(dev, "Invalid mr type %d\n", mr_type);
		err = -EINVAL;
	}

	if (err)
		goto err_free_in;

	kfree(in);

	return &mr->ibmr;

err_free_in:
	kfree(in);
err_free:
	kfree(mr);
	return ERR_PTR(err);
}

struct ib_mr *mlx5_ib_alloc_mr(struct ib_pd *pd, enum ib_mr_type mr_type,
			       u32 max_num_sg)
{
	return __mlx5_ib_alloc_mr(pd, mr_type, max_num_sg, 0);
}

struct ib_mr *mlx5_ib_alloc_mr_integrity(struct ib_pd *pd,
					 u32 max_num_sg, u32 max_num_meta_sg)
{
	return __mlx5_ib_alloc_mr(pd, IB_MR_TYPE_INTEGRITY, max_num_sg,
				  max_num_meta_sg);
}

struct ib_mw *mlx5_ib_alloc_mw(struct ib_pd *pd, enum ib_mw_type type,
			       struct ib_udata *udata)
{
	struct mlx5_ib_dev *dev = to_mdev(pd->device);
	int inlen = MLX5_ST_SZ_BYTES(create_mkey_in);
	struct mlx5_ib_mw *mw = NULL;
	u32 *in = NULL;
	void *mkc;
	int ndescs;
	int err;
	struct mlx5_ib_alloc_mw req = {};
	struct {
		__u32	comp_mask;
		__u32	response_length;
	} resp = {};

	err = ib_copy_from_udata(&req, udata, min(udata->inlen, sizeof(req)));
	if (err)
		return ERR_PTR(err);

	if (req.comp_mask || req.reserved1 || req.reserved2)
		return ERR_PTR(-EOPNOTSUPP);

	if (udata->inlen > sizeof(req) &&
	    !ib_is_udata_cleared(udata, sizeof(req),
				 udata->inlen - sizeof(req)))
		return ERR_PTR(-EOPNOTSUPP);

	ndescs = req.num_klms ? roundup(req.num_klms, 4) : roundup(1, 4);

	mw = kzalloc(sizeof(*mw), GFP_KERNEL);
	in = kzalloc(inlen, GFP_KERNEL);
	if (!mw || !in) {
		err = -ENOMEM;
		goto free;
	}

	mkc = MLX5_ADDR_OF(create_mkey_in, in, memory_key_mkey_entry);

	MLX5_SET(mkc, mkc, free, 1);
	MLX5_SET(mkc, mkc, translations_octword_size, ndescs);
	MLX5_SET(mkc, mkc, pd, to_mpd(pd)->pdn);
	MLX5_SET(mkc, mkc, umr_en, 1);
	MLX5_SET(mkc, mkc, lr, 1);
	MLX5_SET(mkc, mkc, access_mode_1_0, MLX5_MKC_ACCESS_MODE_KLMS);
	MLX5_SET(mkc, mkc, en_rinval, !!((type == IB_MW_TYPE_2)));
	MLX5_SET(mkc, mkc, qpn, 0xffffff);

	err = mlx5_ib_create_mkey(dev, &mw->mmkey, in, inlen);
	if (err)
		goto free;

	mw->mmkey.type = MLX5_MKEY_MW;
	mw->ibmw.rkey = mw->mmkey.key;
	mw->ndescs = ndescs;

	resp.response_length = min(offsetof(typeof(resp), response_length) +
				   sizeof(resp.response_length), udata->outlen);
	if (resp.response_length) {
		err = ib_copy_to_udata(udata, &resp, resp.response_length);
		if (err) {
			mlx5_core_destroy_mkey(dev->mdev, &mw->mmkey);
			goto free;
		}
	}

	kfree(in);
	return &mw->ibmw;

free:
	kfree(mw);
	kfree(in);
	return ERR_PTR(err);
}

int mlx5_ib_dealloc_mw(struct ib_mw *mw)
{
	struct mlx5_ib_mw *mmw = to_mmw(mw);
	int err;

	err =  mlx5_core_destroy_mkey((to_mdev(mw->device))->mdev,
				      &mmw->mmkey);
	if (!err)
		kfree(mmw);
	return err;
}

int mlx5_ib_check_mr_status(struct ib_mr *ibmr, u32 check_mask,
			    struct ib_mr_status *mr_status)
{
	struct mlx5_ib_mr *mr = to_mmr(ibmr);
	int ret = 0;

	if (check_mask & ~IB_MR_CHECK_SIG_STATUS) {
		pr_err("Invalid status check mask\n");
		ret = -EINVAL;
		goto done;
	}

	mr_status->fail_status = 0;
	if (check_mask & IB_MR_CHECK_SIG_STATUS) {
		if (!mr->sig) {
			ret = -EINVAL;
			pr_err("signature status check requested on a non-signature enabled MR\n");
			goto done;
		}

		mr->sig->sig_status_checked = true;
		if (!mr->sig->sig_err_exists)
			goto done;

		if (ibmr->lkey == mr->sig->err_item.key)
			memcpy(&mr_status->sig_err, &mr->sig->err_item,
			       sizeof(mr_status->sig_err));
		else {
			mr_status->sig_err.err_type = IB_SIG_BAD_GUARD;
			mr_status->sig_err.sig_err_offset = 0;
			mr_status->sig_err.key = mr->sig->err_item.key;
		}

		mr->sig->sig_err_exists = false;
		mr_status->fail_status |= IB_MR_CHECK_SIG_STATUS;
	}

done:
	return ret;
}

static int
mlx5_ib_map_pa_mr_sg_pi(struct ib_mr *ibmr, struct scatterlist *data_sg,
			int data_sg_nents, unsigned int *data_sg_offset,
			struct scatterlist *meta_sg, int meta_sg_nents,
			unsigned int *meta_sg_offset)
{
	struct mlx5_ib_mr *mr = to_mmr(ibmr);
	unsigned int sg_offset = 0;
	int n = 0;

	mr->meta_length = 0;
	if (data_sg_nents == 1) {
		n++;
		mr->ndescs = 1;
		if (data_sg_offset)
			sg_offset = *data_sg_offset;
		mr->data_length = sg_dma_len(data_sg) - sg_offset;
		mr->data_iova = sg_dma_address(data_sg) + sg_offset;
		if (meta_sg_nents == 1) {
			n++;
			mr->meta_ndescs = 1;
			if (meta_sg_offset)
				sg_offset = *meta_sg_offset;
			else
				sg_offset = 0;
			mr->meta_length = sg_dma_len(meta_sg) - sg_offset;
			mr->pi_iova = sg_dma_address(meta_sg) + sg_offset;
		}
		ibmr->length = mr->data_length + mr->meta_length;
	}

	return n;
}

static int
mlx5_ib_sg_to_klms(struct mlx5_ib_mr *mr,
		   struct scatterlist *sgl,
		   unsigned short sg_nents,
		   unsigned int *sg_offset_p,
		   struct scatterlist *meta_sgl,
		   unsigned short meta_sg_nents,
		   unsigned int *meta_sg_offset_p)
{
	struct scatterlist *sg = sgl;
	struct mlx5_klm *klms = mr->descs;
	unsigned int sg_offset = sg_offset_p ? *sg_offset_p : 0;
	u32 lkey = mr->ibmr.pd->local_dma_lkey;
	int i, j = 0;

	mr->ibmr.iova = sg_dma_address(sg) + sg_offset;
	mr->ibmr.length = 0;

	for_each_sg(sgl, sg, sg_nents, i) {
		if (unlikely(i >= mr->max_descs))
			break;
		klms[i].va = cpu_to_be64(sg_dma_address(sg) + sg_offset);
		klms[i].bcount = cpu_to_be32(sg_dma_len(sg) - sg_offset);
		klms[i].key = cpu_to_be32(lkey);
		mr->ibmr.length += sg_dma_len(sg) - sg_offset;

		sg_offset = 0;
	}

	if (sg_offset_p)
		*sg_offset_p = sg_offset;

	mr->ndescs = i;
	mr->data_length = mr->ibmr.length;

	if (meta_sg_nents) {
		sg = meta_sgl;
		sg_offset = meta_sg_offset_p ? *meta_sg_offset_p : 0;
		for_each_sg(meta_sgl, sg, meta_sg_nents, j) {
			if (unlikely(i + j >= mr->max_descs))
				break;
			klms[i + j].va = cpu_to_be64(sg_dma_address(sg) +
						     sg_offset);
			klms[i + j].bcount = cpu_to_be32(sg_dma_len(sg) -
							 sg_offset);
			klms[i + j].key = cpu_to_be32(lkey);
			mr->ibmr.length += sg_dma_len(sg) - sg_offset;

			sg_offset = 0;
		}
		if (meta_sg_offset_p)
			*meta_sg_offset_p = sg_offset;

		mr->meta_ndescs = j;
		mr->meta_length = mr->ibmr.length - mr->data_length;
	}

	return i + j;
}

static int mlx5_set_page(struct ib_mr *ibmr, u64 addr)
{
	struct mlx5_ib_mr *mr = to_mmr(ibmr);
	__be64 *descs;

	if (unlikely(mr->ndescs == mr->max_descs))
		return -ENOMEM;

	descs = mr->descs;
	descs[mr->ndescs++] = cpu_to_be64(addr | MLX5_EN_RD | MLX5_EN_WR);

	return 0;
}

static int mlx5_set_page_pi(struct ib_mr *ibmr, u64 addr)
{
	struct mlx5_ib_mr *mr = to_mmr(ibmr);
	__be64 *descs;

	if (unlikely(mr->ndescs + mr->meta_ndescs == mr->max_descs))
		return -ENOMEM;

	descs = mr->descs;
	descs[mr->ndescs + mr->meta_ndescs++] =
		cpu_to_be64(addr | MLX5_EN_RD | MLX5_EN_WR);

	return 0;
}

static int
mlx5_ib_map_mtt_mr_sg_pi(struct ib_mr *ibmr, struct scatterlist *data_sg,
			 int data_sg_nents, unsigned int *data_sg_offset,
			 struct scatterlist *meta_sg, int meta_sg_nents,
			 unsigned int *meta_sg_offset)
{
	struct mlx5_ib_mr *mr = to_mmr(ibmr);
	struct mlx5_ib_mr *pi_mr = mr->mtt_mr;
	int n;

	pi_mr->ndescs = 0;
	pi_mr->meta_ndescs = 0;
	pi_mr->meta_length = 0;

	ib_dma_sync_single_for_cpu(ibmr->device, pi_mr->desc_map,
				   pi_mr->desc_size * pi_mr->max_descs,
				   DMA_TO_DEVICE);

	pi_mr->ibmr.page_size = ibmr->page_size;
	n = ib_sg_to_pages(&pi_mr->ibmr, data_sg, data_sg_nents, data_sg_offset,
			   mlx5_set_page);
	if (n != data_sg_nents)
		return n;

	pi_mr->data_iova = pi_mr->ibmr.iova;
	pi_mr->data_length = pi_mr->ibmr.length;
	pi_mr->ibmr.length = pi_mr->data_length;
	ibmr->length = pi_mr->data_length;

	if (meta_sg_nents) {
		u64 page_mask = ~((u64)ibmr->page_size - 1);
		u64 iova = pi_mr->data_iova;

		n += ib_sg_to_pages(&pi_mr->ibmr, meta_sg, meta_sg_nents,
				    meta_sg_offset, mlx5_set_page_pi);

		pi_mr->meta_length = pi_mr->ibmr.length;
		/*
		 * PI address for the HW is the offset of the metadata address
		 * relative to the first data page address.
		 * It equals to first data page address + size of data pages +
		 * metadata offset at the first metadata page
		 */
		pi_mr->pi_iova = (iova & page_mask) +
				 pi_mr->ndescs * ibmr->page_size +
				 (pi_mr->ibmr.iova & ~page_mask);
		/*
		 * In order to use one MTT MR for data and metadata, we register
		 * also the gaps between the end of the data and the start of
		 * the metadata (the sig MR will verify that the HW will access
		 * to right addresses). This mapping is safe because we use
		 * internal mkey for the registration.
		 */
		pi_mr->ibmr.length = pi_mr->pi_iova + pi_mr->meta_length - iova;
		pi_mr->ibmr.iova = iova;
		ibmr->length += pi_mr->meta_length;
	}

	ib_dma_sync_single_for_device(ibmr->device, pi_mr->desc_map,
				      pi_mr->desc_size * pi_mr->max_descs,
				      DMA_TO_DEVICE);

	return n;
}

static int
mlx5_ib_map_klm_mr_sg_pi(struct ib_mr *ibmr, struct scatterlist *data_sg,
			 int data_sg_nents, unsigned int *data_sg_offset,
			 struct scatterlist *meta_sg, int meta_sg_nents,
			 unsigned int *meta_sg_offset)
{
	struct mlx5_ib_mr *mr = to_mmr(ibmr);
	struct mlx5_ib_mr *pi_mr = mr->klm_mr;
	int n;

	pi_mr->ndescs = 0;
	pi_mr->meta_ndescs = 0;
	pi_mr->meta_length = 0;

	ib_dma_sync_single_for_cpu(ibmr->device, pi_mr->desc_map,
				   pi_mr->desc_size * pi_mr->max_descs,
				   DMA_TO_DEVICE);

	n = mlx5_ib_sg_to_klms(pi_mr, data_sg, data_sg_nents, data_sg_offset,
			       meta_sg, meta_sg_nents, meta_sg_offset);

	ib_dma_sync_single_for_device(ibmr->device, pi_mr->desc_map,
				      pi_mr->desc_size * pi_mr->max_descs,
				      DMA_TO_DEVICE);

	/* This is zero-based memory region */
	pi_mr->data_iova = 0;
	pi_mr->ibmr.iova = 0;
	pi_mr->pi_iova = pi_mr->data_length;
	ibmr->length = pi_mr->ibmr.length;

	return n;
}

int mlx5_ib_map_mr_sg_pi(struct ib_mr *ibmr, struct scatterlist *data_sg,
			 int data_sg_nents, unsigned int *data_sg_offset,
			 struct scatterlist *meta_sg, int meta_sg_nents,
			 unsigned int *meta_sg_offset)
{
	struct mlx5_ib_mr *mr = to_mmr(ibmr);
	struct mlx5_ib_mr *pi_mr = NULL;
	int n;

	WARN_ON(ibmr->type != IB_MR_TYPE_INTEGRITY);

	mr->ndescs = 0;
	mr->data_length = 0;
	mr->data_iova = 0;
	mr->meta_ndescs = 0;
	mr->pi_iova = 0;
	/*
	 * As a performance optimization, if possible, there is no need to
	 * perform UMR operation to register the data/metadata buffers.
	 * First try to map the sg lists to PA descriptors with local_dma_lkey.
	 * Fallback to UMR only in case of a failure.
	 */
	n = mlx5_ib_map_pa_mr_sg_pi(ibmr, data_sg, data_sg_nents,
				    data_sg_offset, meta_sg, meta_sg_nents,
				    meta_sg_offset);
	if (n == data_sg_nents + meta_sg_nents)
		goto out;
	/*
	 * As a performance optimization, if possible, there is no need to map
	 * the sg lists to KLM descriptors. First try to map the sg lists to MTT
	 * descriptors and fallback to KLM only in case of a failure.
	 * It's more efficient for the HW to work with MTT descriptors
	 * (especially in high load).
	 * Use KLM (indirect access) only if it's mandatory.
	 */
	pi_mr = mr->mtt_mr;
	n = mlx5_ib_map_mtt_mr_sg_pi(ibmr, data_sg, data_sg_nents,
				     data_sg_offset, meta_sg, meta_sg_nents,
				     meta_sg_offset);
	if (n == data_sg_nents + meta_sg_nents)
		goto out;

	pi_mr = mr->klm_mr;
	n = mlx5_ib_map_klm_mr_sg_pi(ibmr, data_sg, data_sg_nents,
				     data_sg_offset, meta_sg, meta_sg_nents,
				     meta_sg_offset);
	if (unlikely(n != data_sg_nents + meta_sg_nents))
		return -ENOMEM;

out:
	/* This is zero-based memory region */
	ibmr->iova = 0;
	mr->pi_mr = pi_mr;
	if (pi_mr)
		ibmr->sig_attrs->meta_length = pi_mr->meta_length;
	else
		ibmr->sig_attrs->meta_length = mr->meta_length;

	return 0;
}

int mlx5_ib_map_mr_sg(struct ib_mr *ibmr, struct scatterlist *sg, int sg_nents,
		      unsigned int *sg_offset)
{
	struct mlx5_ib_mr *mr = to_mmr(ibmr);
	int n;

	mr->ndescs = 0;

	ib_dma_sync_single_for_cpu(ibmr->device, mr->desc_map,
				   mr->desc_size * mr->max_descs,
				   DMA_TO_DEVICE);

	if (mr->access_mode == MLX5_MKC_ACCESS_MODE_KLMS)
		n = mlx5_ib_sg_to_klms(mr, sg, sg_nents, sg_offset, NULL, 0,
				       NULL);
	else
		n = ib_sg_to_pages(ibmr, sg, sg_nents, sg_offset,
				mlx5_set_page);

	ib_dma_sync_single_for_device(ibmr->device, mr->desc_map,
				      mr->desc_size * mr->max_descs,
				      DMA_TO_DEVICE);

	return n;
}

struct order_attribute {
	struct attribute attr;
	ssize_t (*show)(struct cache_order *, struct order_attribute *, char *buf);
	ssize_t (*store)(struct cache_order *, struct order_attribute *,
			 const char *buf, size_t count);
};

static ssize_t cur_show(struct cache_order *co, struct order_attribute *oa,
			char *buf)
{
	struct mlx5_ib_dev *dev = co->dev;
	struct mlx5_mr_cache *cache = &dev->cache;
	struct mlx5_cache_ent *ent = &cache->ent[co->index];
	int err;

	err = snprintf(buf, 20, "%d\n", ent->cur);
	return err;
}

static ssize_t limit_show(struct cache_order *co, struct order_attribute *oa,
			  char *buf)
{
	struct mlx5_ib_dev *dev = co->dev;
	struct mlx5_mr_cache *cache = &dev->cache;
	struct mlx5_cache_ent *ent = &cache->ent[co->index];
	int err;

	err = snprintf(buf, 20, "%d\n", ent->limit);
	return err;
}

static ssize_t limit_store(struct cache_order *co, struct order_attribute *oa,
			   const char *buf, size_t count)
{
	struct mlx5_ib_dev *dev = co->dev;
	struct mlx5_mr_cache *cache = &dev->cache;
	struct mlx5_cache_ent *ent = &cache->ent[co->index];
	u32 var;
	int err;

	if (kstrtouint(buf, 0, &var))
		return -EINVAL;

	if (var > ent->size)
		return -EINVAL;

	ent->limit = var;

	if (ent->cur < ent->limit) {
		err = add_keys(dev, co->index, 2 * ent->limit - ent->cur);
		if (err)
			return err;
	}

	return count;
}

static ssize_t miss_show(struct cache_order *co, struct order_attribute *oa,
			 char *buf)
{
	struct mlx5_ib_dev *dev = co->dev;
	struct mlx5_mr_cache *cache = &dev->cache;
	struct mlx5_cache_ent *ent = &cache->ent[co->index];
	int err;

	err = snprintf(buf, 20, "%d\n", ent->miss);
	return err;
}

static ssize_t miss_store(struct cache_order *co, struct order_attribute *oa,
			  const char *buf, size_t count)
{
	struct mlx5_ib_dev *dev = co->dev;
	struct mlx5_mr_cache *cache = &dev->cache;
	struct mlx5_cache_ent *ent = &cache->ent[co->index];
	u32 var;

	if (kstrtouint(buf, 0, &var))
		return -EINVAL;

	if (var != 0)
		return -EINVAL;

	ent->miss = var;

	return count;
}

static ssize_t size_show(struct cache_order *co, struct order_attribute *oa,
			 char *buf)
{
	struct mlx5_ib_dev *dev = co->dev;
	struct mlx5_mr_cache *cache = &dev->cache;
	struct mlx5_cache_ent *ent = &cache->ent[co->index];
	int err;

	err = snprintf(buf, 20, "%d\n", ent->size);
	return err;
}

static ssize_t size_store(struct cache_order *co, struct order_attribute *oa,
			  const char *buf, size_t count)
{
	struct mlx5_ib_dev *dev = co->dev;
	struct mlx5_mr_cache *cache = &dev->cache;
	struct mlx5_cache_ent *ent = &cache->ent[co->index];
	u32 var;
	int err;

	if (kstrtouint(buf, 0, &var))
		return -EINVAL;

	if (var < ent->limit)
		return -EINVAL;

	if (var > ent->size) {
		do {
			err = add_keys(dev, co->index, var - ent->size);
			if (err && err != -EAGAIN)
				return err;

			usleep_range(3000, 5000);
		} while (err);
	} else if (var < ent->size) {
		remove_keys(dev, co->index, ent->size - var);
	}

	return count;
}

static ssize_t order_attr_show(struct kobject *kobj,
			       struct attribute *attr, char *buf)
{
	struct order_attribute *oa =
		container_of(attr, struct order_attribute, attr);
	struct cache_order *co = container_of(kobj, struct cache_order, kobj);

	if (!oa->show)
		return -EIO;

	return oa->show(co, oa, buf);
}

static ssize_t order_attr_store(struct kobject *kobj,
				struct attribute *attr, const char *buf, size_t size)
{
	struct order_attribute *oa =
		container_of(attr, struct order_attribute, attr);
	struct cache_order *co = container_of(kobj, struct cache_order, kobj);

	if (!oa->store)
		return -EIO;

	return oa->store(co, oa, buf, size);
}

static const struct sysfs_ops order_sysfs_ops = {
	.show = order_attr_show,
	.store = order_attr_store,
};

#define ORDER_ATTR(_name) struct order_attribute order_attr_##_name = \
	__ATTR(_name, 0644, _name##_show, _name##_store)
#define ORDER_ATTR_RO(_name) struct order_attribute order_attr_##_name = \
	__ATTR(_name, 0444, _name##_show, NULL)

static ORDER_ATTR_RO(cur);
static ORDER_ATTR(limit);
static ORDER_ATTR(miss);
static ORDER_ATTR(size);

static struct attribute *order_default_attrs[] = {
	&order_attr_cur.attr,
	&order_attr_limit.attr,
	&order_attr_miss.attr,
	&order_attr_size.attr,
	NULL
};

static struct kobj_type order_type = {
	.sysfs_ops     = &order_sysfs_ops,
	.default_attrs = order_default_attrs
};



struct cache_attribute {
	struct attribute attr;
	ssize_t (*show)(struct mlx5_ib_dev *dev, char *buf);
	ssize_t (*store)(struct mlx5_ib_dev *dev, const char *buf, size_t count);
};

static ssize_t rel_imm_show(struct mlx5_ib_dev *dev, char *buf)
{
	struct mlx5_mr_cache *cache = &dev->cache;
	int err;

	err = snprintf(buf, 20, "%d\n", cache->rel_imm);
	return err;
}

static ssize_t rel_imm_store(struct mlx5_ib_dev *dev, const char *buf, size_t count)
{
	struct mlx5_mr_cache *cache = &dev->cache;
	u32 var;
	int i;
	int found = 0;

	if (kstrtouint(buf, 0, &var))
		return -EINVAL;

	if (var > 1)
		return -EINVAL;

	if (var == cache->rel_imm)
		return count;

	cache->rel_imm = var;
	if (cache->rel_imm == 1) {
		for (i = 0; i < MAX_MR_CACHE_ENTRIES; i++) {
			if (cache->ent[i].cur > 2 * cache->ent[i].limit) {
				queue_work(cache->wq, &cache->ent[i].work);
				found = 1;
			}
		}
		if (!found)
			cache->rel_imm = 0;
	}

	return count;
}
static ssize_t rel_timeout_show(struct mlx5_ib_dev *dev, char *buf)
{
	struct mlx5_mr_cache *cache = &dev->cache;
	int err;

	err = snprintf(buf, 20, "%d\n", cache->rel_timeout);
	return err;
}

static ssize_t rel_timeout_store(struct mlx5_ib_dev *dev, const char *buf, size_t count)
{
	struct mlx5_mr_cache *cache = &dev->cache;
	int var;
	int i;

	if (kstrtoint(buf, 0, &var))
		return -EINVAL;

	if (var < -1 || var > MAX_MR_RELEASE_TIMEOUT)
		return -EINVAL;

	if (var == cache->rel_timeout)
		return count;

	if (cache->rel_timeout == -1 || (var < cache->rel_timeout && var != -1)) {
		cache->rel_timeout = var;
		for (i = 0; i < MAX_MR_CACHE_ENTRIES; i++) {
			if (cache->ent[i].cur > 2 * cache->ent[i].limit)
				queue_work(cache->wq, &cache->ent[i].work);
		}
	} else {
		cache->rel_timeout = var;
	}

	return count;
}

static ssize_t cache_attr_show(struct kobject *kobj,
			       struct attribute *attr, char *buf)
{
	struct cache_attribute *ca =
		container_of(attr, struct cache_attribute, attr);
	struct mlx5_ib_dev *dev = container_of(kobj, struct mlx5_ib_dev, mr_cache);

	if (!ca->show)
		return -EIO;

	return ca->show(dev, buf);
}

static ssize_t cache_attr_store(struct kobject *kobj,
				struct attribute *attr, const char *buf, size_t size)
{
	struct cache_attribute *ca =
		container_of(attr, struct cache_attribute, attr);
	struct mlx5_ib_dev *dev = container_of(kobj, struct mlx5_ib_dev, mr_cache);

	if (!ca->store)
		return -EIO;

	return ca->store(dev, buf, size);
}

static const struct sysfs_ops cache_sysfs_ops = {
	.show = cache_attr_show,
	.store = cache_attr_store,
};

#define CACHE_ATTR(_name) struct cache_attribute cache_attr_##_name = \
	__ATTR(_name, 0644, _name##_show, _name##_store)

static CACHE_ATTR(rel_imm);
static CACHE_ATTR(rel_timeout);

static struct attribute *cache_default_attrs[] = {
	&cache_attr_rel_imm.attr,
	&cache_attr_rel_timeout.attr,
	NULL
};

static struct kobj_type cache_type = {
	.sysfs_ops     = &cache_sysfs_ops,
	.default_attrs = cache_default_attrs
};

static int mlx5_mr_sysfs_init(struct mlx5_ib_dev *dev)
{
	struct mlx5_mr_cache *cache = &dev->cache;
	struct device *device = &dev->ib_dev.dev;
	struct cache_order *co;
	int o;
	int i;
	int err;

	if (dev->is_rep)
		return 0;

	err = kobject_init_and_add(&dev->mr_cache, &cache_type,
				   &device->kobj, "mr_cache");
	if (err)
		return -ENOMEM;

	for (o = 2, i = 0; i < MAX_MR_CACHE_ENTRIES; o++, i++) {
		co = &cache->ent[i].co;
		co->order = o;
		co->index = i;
		co->dev = dev;
		err = kobject_init_and_add(&co->kobj, &order_type,
					   &dev->mr_cache, "%d", o);
		if (err)
			goto err_put;

		kobject_uevent(&co->kobj, KOBJ_ADD);
	}

	return 0;

err_put:
	for (; i >= 0; i--) {
		co = &cache->ent[i].co;
		kobject_put(&co->kobj);
	}
	kobject_put(&dev->mr_cache);

	return err;
}

static void mlx5_mr_sysfs_cleanup(struct mlx5_ib_dev *dev)
{
	struct mlx5_mr_cache *cache = &dev->cache;
	struct cache_order *co;
	int i;

	if (dev->is_rep)
		return;

	for (i = MAX_MR_CACHE_ENTRIES - 1; i >= 0; i--) {
		co = &cache->ent[i].co;
		kobject_put(&co->kobj);
	}
	kobject_put(&dev->mr_cache);
}

static int use_klm(int order)
{
	return order <= 31;
}

enum {
	MLX5_MAX_REG_SIZE = 2ul * 1024 * 1024 * 1024,
};

static u32 get_lsize(struct mlx5_ib_dev *dev, int page_shift)
{
	u64 umr_limit;

	umr_limit = (u64)1 << (page_shift + mr_cache_max_order(dev));

	BUILD_BUG_ON(MLX5_MAX_REG_SIZE > (size_t)UINT_MAX);
	return min_t(u64, umr_limit, MLX5_MAX_REG_SIZE);
}

static int alloc_mrs(struct mlx5_ib_dev *dev, struct mlx5_ib_mr **lmr, int n,
		     int order, u32 size, int nchild, int sorder, u64 len,
		     u64 off, int npages)
{
	int err = 0;
	int i;
	int k;

	for (i = 0, k = 0; i < n; i++) {
again:
		if (k++ > 3) {
			err = -EAGAIN;
			goto out;
		}
		lmr[i] = alloc_cached_mr(dev, order);
		if (!lmr[i]) {
			err = add_keys(dev, order2idx(dev, order), n - i);
			if (err) {
				if (err != -EAGAIN) {
					mlx5_ib_warn(dev, "add_keys failed to add %d keys (%d)\n", n - i, err);
					goto out;
				}
				msleep(20);
			}
			goto again;
		}
		lmr[i]->size = size;
		lmr[i]->page_count = 1 << order;
		k = 0;
	}

	lmr[0]->mmkey.iova = off;
	lmr[0]->size = size - off;

	if (nchild == n)
		return 0;

	for (k = 0; k < 3; k++) {
		lmr[i] = alloc_cached_mr(dev, sorder);
		if (lmr[i])
			break;
		err = add_keys(dev, order2idx(dev, sorder), 1);
		if (err) {
			if (err != -EAGAIN) {
				mlx5_ib_warn(dev, "add_keys failed, err %d\n", err);
				goto out;
			}
			msleep(20);
		}
	}

	if (!lmr[i]) {
		err = -EAGAIN;
		goto out;
	}
	lmr[i]->size = len - size * n + off;
	lmr[i]->page_count = npages - (n << order);

	return 0;

out:
	for (--i; i >= 0; --i)
		mlx5_mr_cache_free(dev, lmr[i]);

	return err;
}

static int create_indirect_key(struct mlx5_ib_dev *dev, struct ib_pd *pd,
			       struct mlx5_ib_mr *mr, unsigned n)
{
	int inlen = MLX5_ST_SZ_BYTES(create_mkey_in);
	void *mkc;
	u32 *in;
	int err;

	in = kzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	mkc = MLX5_ADDR_OF(create_mkey_in, in, memory_key_mkey_entry);
	mr->dev = dev;
	MLX5_SET(mkc, mkc, free, 1);
	MLX5_SET(mkc, mkc, umr_en, 1);
	MLX5_SET(mkc, mkc, access_mode_1_0, MLX5_MKC_ACCESS_MODE_KLMS);
	MLX5_SET(mkc, mkc, qpn, 0xffffff);
	MLX5_SET(mkc, mkc, pd, to_mpd(pd)->pdn);
	MLX5_SET(mkc, mkc, translations_octword_size, ALIGN(n, 4));
	err = mlx5_core_create_mkey(dev->mdev, &mr->mmkey, in, inlen);

	kfree(in);
	return err;
}

static int reg_mrs(struct ib_pd *pd, struct mlx5_ib_mr **mrs, int n,
		   int page_shift, struct ib_umem *umem, int access_flags,
		   u64 npages)
{
	struct mlx5_ib_dev *dev = to_mdev(pd->device);
	int err1;
	int err;
	int i;

	for (i = 0; i < n; ++i) {
		mrs[i]->ibmr.pd = pd;
		mrs[i]->umem = umem;
		mrs[i]->access_flags = access_flags;
		mrs[i]->mmkey.type = MLX5_MKEY_MR;
		mrs[i]->mmkey.size = mrs[i]->size;
		mrs[i]->mmkey.pd = to_mpd(pd)->pdn;
		if (i)
			mrs[i]->mmkey.iova = 0;

		err = mlx5_ib_update_xlt(mrs[i], 0, i * npages,
					 mrs[i]->page_count,
					 page_shift, MLX5_IB_UPD_XLT_ENABLE);
		if (err)
			goto out;
		mrs[i]->free = 0;
	}
	return 0;
out:
	for (--i; i >= 0; --i) {
		err1 = unreg_umr(dev, mrs[i]);
		if (err1)
			mlx5_ib_warn(dev, "unreg_umr failed %d\n", err1);
	}

	return err;
}

static void populate_klm(void *dma, struct mlx5_ib_mr **lmr, int n)
{
	struct mlx5_wqe_data_seg *dseg = dma;
	int i;

	for (i = 0; i < n; i++) {
		dseg[i].lkey = cpu_to_be32(lmr[i]->mmkey.key);
		dseg[i].byte_count = cpu_to_be32((u32)(lmr[i]->size));
		dseg[i].addr = cpu_to_be64(lmr[i]->mmkey.iova);
	}
}

static int get_nchild(int npages, u32 lsize, int page_shift, u64 *maxorder, int *sorder, int *quot)
{
	int res;
	u32 denom;

	denom = lsize >> page_shift;
	res = npages % denom;
	*quot = npages / denom;
	*maxorder = ilog2(denom);
	*sorder = max_t(int, ilog2(roundup_pow_of_two(res)), 2);
	return *quot + (res ? 1 : 0);
}

static struct mlx5_ib_mr *reg_klm(struct ib_pd *pd, struct ib_umem *umem,
				  u64 virt_addr, u64 len, int npages,
				  int page_shift, int order, int access_flags)
{
	struct mlx5_ib_dev *dev = to_mdev(pd->device);
	struct mlx5_ib_mr **lmr = NULL;
	struct mlx5_ib_mr *imr = NULL;
	int err = -ENOMEM;
	int nchild;
	int sorder;
	u32 lsize;
	int i = 0;
	int err1;
	int quot;
	u64 off;
	u64 maxorder;

	mlx5_ib_dbg(dev, "addr 0x%llx, len 0x%llx, npages %d, page_shift %d, order %d, access_flags 0x%x\n",
		    virt_addr, len, npages, page_shift, order, access_flags);
	lsize = get_lsize(dev, page_shift);
	nchild = get_nchild(npages, lsize, page_shift, &maxorder, &sorder, &quot);
	off = (virt_addr & ((1 << page_shift) - 1));
	lmr = kcalloc(nchild, sizeof(*lmr), GFP_KERNEL);
	if (!lmr) {
		mlx5_ib_warn(dev, "allocation failed\n");
		return ERR_PTR(-ENOMEM);
	}

	err = alloc_mrs(dev, lmr, quot, maxorder, lsize, nchild, sorder, len, off, npages);
	if (err) {
		mlx5_ib_dbg(dev, "alloc_mrs failed\n");
		goto out;
	}

	imr = kzalloc(sizeof(*imr), GFP_KERNEL);
	if (!imr) {
		err = -ENOMEM;
		mlx5_ib_warn(dev, "failed allocation\n");
		goto out_mrs;
	}

	err = create_indirect_key(dev, pd, imr, nchild);
	if (err) {
		mlx5_ib_warn(dev, "failed creating indirect key %d\n", err);
		goto out_mrs;
	}

	err = reg_mrs(pd, lmr, nchild, page_shift, umem,
		      access_flags, 1 << maxorder);
	if (err) {
		mlx5_ib_warn(dev, "reg_mrs failed %d\n", err);
		goto out_indir;
	}

	imr->size = len;
	imr->ibmr.pd = pd;
	imr->umem = umem;
	imr->access_flags = access_flags;
	imr->mmkey.type = MLX5_MKEY_MR;
	imr->mmkey.iova = virt_addr;
	imr->mmkey.size = len;
	imr->mmkey.pd = to_mpd(pd)->pdn;
	imr->children = lmr;
	imr->nchild = nchild;
	err = mlx5_ib_update_xlt(imr, 0, 0, nchild, 0,
				 MLX5_IB_UPD_XLT_ENABLE |
				 MLX5_IB_UPD_XLT_INDIRECT);
	if (err)
		goto out_indir;

	return imr;

out_indir:
	err1 = mlx5_core_destroy_mkey(dev->mdev, &imr->mmkey);
	if (err1)
		mlx5_ib_warn(dev, "destroy imr mkey failed %d\n", err1);
out_mrs:
	kfree(imr);
	for (i = 0; i < nchild; i++)
		mlx5_mr_cache_free(dev, lmr[i]);
out:
	kfree(lmr);
	return ERR_PTR(err);
}

static void mlx5_invalidate_umem(void *invalidation_cookie,
				 struct ib_umem *umem,
				 unsigned long addr, size_t size)
{
	struct mlx5_ib_mr *mr;
	struct mlx5_ib_peer_id *peer_id = (struct mlx5_ib_peer_id *)invalidation_cookie;

	wait_for_completion(&peer_id->comp);
	if (peer_id->mr == NULL)
		return;

	mr = peer_id->mr;
	/* This function is called under client peer lock so its resources are race protected */
	if (atomic_inc_return(&mr->invalidated) > 1) {
		umem->invalidation_ctx->inflight_invalidation = 1;
		return;
	}

	umem->invalidation_ctx->peer_callback = 1;
	mlx5_ib_invalidate_mr(&mr->ibmr);
	complete(&mr->invalidation_comp);
}
