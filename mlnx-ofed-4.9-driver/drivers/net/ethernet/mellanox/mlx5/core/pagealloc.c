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

#include <linux/highmem.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/mlx5/driver.h>
#include <linux/mlx5/cmd.h>
#include "mlx5_core.h"
#include "lib/eq.h"

enum {
	MLX5_PAGES_CANT_GIVE	= 0,
	MLX5_PAGES_GIVE		= 1,
	MLX5_PAGES_TAKE		= 2
};

struct mlx5_pages_req {
	struct mlx5_core_dev *dev;
	u16	func_id;
	u8	ec_function;
	s32	npages;
	struct work_struct work;
};

struct fw_page {
	struct rb_node		rb_node;
	u64			addr;
	struct page	       *page;
	u16			func_id;
	unsigned long		bitmask;
	struct list_head	list;
	unsigned		free_count;
};

enum {
	MAX_RECLAIM_TIME_MSECS	= 5000,
	MAX_RECLAIM_VFS_PAGES_TIME_MSECS = 2 * 1000 * 60,
	GARBAGE_COLLECTOR_DELAY_MSECS	= 30000,
};

enum {
	MLX5_MAX_RECLAIM_TIME_MILI	= 5000,
	MLX5_NUM_4K_IN_PAGE		= PAGE_SIZE / MLX5_ADAPTER_PAGE_SIZE,
};

static int insert_page(struct mlx5_core_dev *dev, u64 addr, struct page *page, u16 func_id)
{
	struct rb_root *root = &dev->priv.page_root;
	struct rb_node **new = &root->rb_node;
	struct rb_node *parent = NULL;
	struct fw_page *nfp;
	struct fw_page *tfp;
	int i;

	while (*new) {
		parent = *new;
		tfp = rb_entry(parent, struct fw_page, rb_node);
		if (tfp->addr < addr)
			new = &parent->rb_left;
		else if (tfp->addr > addr)
			new = &parent->rb_right;
		else
			return -EEXIST;
	}

	nfp = kzalloc(sizeof(*nfp), GFP_KERNEL);
	if (!nfp)
		return -ENOMEM;

	nfp->addr = addr;
	nfp->page = page;
	nfp->func_id = func_id;
	nfp->free_count = MLX5_NUM_4K_IN_PAGE;
	for (i = 0; i < MLX5_NUM_4K_IN_PAGE; i++)
		set_bit(i, &nfp->bitmask);

	rb_link_node(&nfp->rb_node, parent, new);
	rb_insert_color(&nfp->rb_node, root);
	list_add(&nfp->list, &dev->priv.free_list);

	return 0;
}

static struct fw_page *find_fw_page(struct mlx5_core_dev *dev, u64 addr)
{
	struct rb_root *root = &dev->priv.page_root;
	struct rb_node *tmp = root->rb_node;
	struct fw_page *result = NULL;
	struct fw_page *tfp;

	while (tmp) {
		tfp = rb_entry(tmp, struct fw_page, rb_node);
		if (tfp->addr < addr) {
			tmp = tmp->rb_left;
		} else if (tfp->addr > addr) {
			tmp = tmp->rb_right;
		} else {
			result = tfp;
			break;
		}
	}

	return result;
}

static int mlx5_cmd_query_pages(struct mlx5_core_dev *dev, u16 *func_id,
				s32 *npages, int boot)
{
	u32 out[MLX5_ST_SZ_DW(query_pages_out)] = {0};
	u32 in[MLX5_ST_SZ_DW(query_pages_in)]   = {0};
	int err;

	MLX5_SET(query_pages_in, in, opcode, MLX5_CMD_OP_QUERY_PAGES);
	MLX5_SET(query_pages_in, in, op_mod, boot ?
		 MLX5_QUERY_PAGES_IN_OP_MOD_BOOT_PAGES :
		 MLX5_QUERY_PAGES_IN_OP_MOD_INIT_PAGES);
	MLX5_SET(query_pages_in, in, embedded_cpu_function, mlx5_core_is_ecpf(dev));

	err = mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
	if (err)
		return err;

	*npages = MLX5_GET(query_pages_out, out, num_pages);
	*func_id = MLX5_GET(query_pages_out, out, function_id);

	return err;
}

static int alloc_4k(struct mlx5_core_dev *dev, u64 *addr)
{
	struct fw_page *fp;
	unsigned n;

	if (list_empty(&dev->priv.free_list))
		return -ENOMEM;

	fp = list_entry(dev->priv.free_list.next, struct fw_page, list);
	n = find_first_bit(&fp->bitmask, 8 * sizeof(fp->bitmask));
	if (n >= MLX5_NUM_4K_IN_PAGE) {
		mlx5_core_warn(dev, "alloc 4k bug: fw page = 0x%llx, n = %u, bitmask: %lu, max num of 4K pages: %d\n",
			       fp->addr, n, fp->bitmask,  MLX5_NUM_4K_IN_PAGE);
		return -ENOENT;
	}
	clear_bit(n, &fp->bitmask);
	fp->free_count--;
	if (!fp->free_count)
		list_del(&fp->list);

	*addr = fp->addr + n * MLX5_ADAPTER_PAGE_SIZE;

	return 0;
}

#define MLX5_U64_4K_PAGE_MASK ((~(u64)0U) << PAGE_SHIFT)

static int free_4k(struct mlx5_core_dev *dev, u64 addr)
{
	struct fw_page *fwp;
	int n;

	fwp = find_fw_page(dev, addr & MLX5_U64_4K_PAGE_MASK);
	if (!fwp) {
		mlx5_core_warn(dev, "page not found\n");
		return -ENOMEM;
	}

	n = (addr & ~MLX5_U64_4K_PAGE_MASK) >> MLX5_ADAPTER_PAGE_SHIFT;
	if (test_bit(n, &fwp->bitmask)) {
		mlx5_core_warn(dev, "addr 0x%llx is already freed, n %d\n", addr, n);
		return -EINVAL;
	}

	fwp->free_count++;
	set_bit(n, &fwp->bitmask);
	if (fwp->free_count == 1)
		list_add(&fwp->list, &dev->priv.free_list);

	return 0;
}

static int alloc_system_page(struct mlx5_core_dev *dev, u16 func_id)
{
	struct device *device = dev->device;
	int nid = dev_to_node(device);
	struct page *page;
	u64 zero_addr = 1;
	u64 addr;
	int err;

	page = alloc_pages_node(nid, GFP_HIGHUSER, 0);
	if (!page) {
		mlx5_core_warn(dev, "failed to allocate page\n");
		return -ENOMEM;
	}
map:
	addr = dma_map_page(device, page, 0, PAGE_SIZE, DMA_BIDIRECTIONAL);
	if (dma_mapping_error(device, addr)) {
		mlx5_core_warn(dev, "failed dma mapping page\n");
		err = -ENOMEM;
		goto err_mapping;
	}

	/* Firmware doesn't support page with physical address 0 */
	if (addr == 0) {
		zero_addr = addr;
		goto map;
	}

	err = insert_page(dev, addr, page, func_id);
	if (err) {
		mlx5_core_err(dev, "failed to track allocated page\n");
		dma_unmap_page(device, addr, PAGE_SIZE, DMA_BIDIRECTIONAL);
	}

err_mapping:
	if (err)
		__free_page(page);

	if (zero_addr == 0)
		dma_unmap_page(device, zero_addr, PAGE_SIZE,
			       DMA_BIDIRECTIONAL);

	return err;
}

static void page_notify_fail(struct mlx5_core_dev *dev, u16 func_id,
			     bool ec_function)
{
	u32 out[MLX5_ST_SZ_DW(manage_pages_out)] = {0};
	u32 in[MLX5_ST_SZ_DW(manage_pages_in)]   = {0};
	int err;

	MLX5_SET(manage_pages_in, in, opcode, MLX5_CMD_OP_MANAGE_PAGES);
	MLX5_SET(manage_pages_in, in, op_mod, MLX5_PAGES_CANT_GIVE);
	MLX5_SET(manage_pages_in, in, function_id, func_id);
	MLX5_SET(manage_pages_in, in, embedded_cpu_function, ec_function);

	err = mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
	if (err)
		mlx5_core_warn(dev, "page notify failed func_id(%d) err(%d)\n",
			       func_id, err);
	else
		mlx5_core_warn(dev, "Page allocation failure notification on func_id(%d) sent to fw\n",
			       func_id);
}

static int give_pages(struct mlx5_core_dev *dev, u16 func_id, int npages,
		      int notify_fail, bool ec_function)
{
	unsigned long max_duration = jiffies + msecs_to_jiffies(MLX5_CMD_TIMEOUT_MSEC / 2);
	u32 out[MLX5_ST_SZ_DW(manage_pages_out)] = {0};
	int inlen = MLX5_ST_SZ_BYTES(manage_pages_in);
	u64 addr;
	int err;
	u32 *in;
	int i;

	inlen += npages * MLX5_FLD_SZ_BYTES(manage_pages_in, pas[0]);
	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in) {
		err = -ENOMEM;
		mlx5_core_warn(dev, "vzalloc failed %d\n", inlen);
		goto out_free;
	}

	for (i = 0; i < npages; i++) {
		if (time_after(jiffies, max_duration)) {
			mlx5_core_warn(dev,
				       "%d pages alloc time exceeded the max permitted duration\n",
				       npages);
			err = -ENOMEM;
			goto out_4k;
		}
retry:
		err = alloc_4k(dev, &addr);
		if (err) {
			if (err == -ENOMEM)
				err = alloc_system_page(dev, func_id);
			if (err)
				goto out_4k;

			goto retry;
		}
		MLX5_ARRAY_SET64(manage_pages_in, in, pas, i, addr);
	}

	MLX5_SET(manage_pages_in, in, opcode, MLX5_CMD_OP_MANAGE_PAGES);
	MLX5_SET(manage_pages_in, in, op_mod, MLX5_PAGES_GIVE);
	MLX5_SET(manage_pages_in, in, function_id, func_id);
	MLX5_SET(manage_pages_in, in, input_num_entries, npages);
	MLX5_SET(manage_pages_in, in, embedded_cpu_function, ec_function);

	err = mlx5_cmd_exec(dev, in, inlen, out, sizeof(out));
	if (err) {
		mlx5_core_warn(dev, "func_id 0x%x, npages %d, err %d\n",
			       func_id, npages, err);
		goto out_4k;
	}

	dev->priv.fw_pages += npages;
	if (func_id)
		dev->priv.vfs_pages += npages;
	else if (mlx5_core_is_ecpf(dev) && !ec_function)
		dev->priv.peer_pf_pages += npages;

	mlx5_core_dbg(dev, "npages %d, ec_function %d, func_id 0x%x, err %d\n",
		      npages, ec_function, func_id, err);

	kvfree(in);
	return 0;

out_4k:
	for (i--; i >= 0; i--)
		free_4k(dev, MLX5_GET64(manage_pages_in, in, pas[i]));
out_free:
	kvfree(in);
	if (notify_fail)
		page_notify_fail(dev, func_id, ec_function);
	return err;
}

static void free_pages_list(struct mlx5_core_dev *dev)
{
	struct fw_page *tmp;
	struct fw_page *fp;

	list_for_each_entry_safe(fp, tmp, &dev->priv.free_list, list) {
		/* In case of shared page that is in use leave it in the list */
		if (fp->free_count != MLX5_NUM_4K_IN_PAGE)
			continue;

		list_del(&fp->list);
		rb_erase(&fp->rb_node, &dev->priv.page_root);
		dma_unmap_page(&dev->pdev->dev, fp->addr & MLX5_U64_4K_PAGE_MASK,
			       PAGE_SIZE, DMA_BIDIRECTIONAL);
		__free_page(fp->page);
		kfree(fp);
	}
}

static int reclaim_pages_cmd(struct mlx5_core_dev *dev,
			     u32 *in, int in_size, u32 *out, int out_size)
{
	struct fw_page *fwp;
	struct rb_node *p;
	u32 func_id;
	u32 npages;
	u32 i = 0, j;

	if (dev->state != MLX5_DEVICE_STATE_INTERNAL_ERROR)
		return mlx5_cmd_exec(dev, in, in_size, out, out_size);

	/* No hard feelings, we want our pages back! */
	npages = MLX5_GET(manage_pages_in, in, input_num_entries);
	func_id = MLX5_GET(manage_pages_in, in, function_id);

	p = rb_first(&dev->priv.page_root);
	while (p && i < npages) {
		j = 0;
		fwp = rb_entry(p, struct fw_page, rb_node);
		p = rb_next(p);
		if (fwp->func_id != func_id)
			continue;

		j = find_next_zero_bit(&fwp->bitmask, MLX5_NUM_4K_IN_PAGE, j);
		if (j >= MLX5_NUM_4K_IN_PAGE)
			continue;

		MLX5_ARRAY_SET64(manage_pages_out, out, pas, i,
				 fwp->addr + (j << MLX5_ADAPTER_PAGE_SHIFT));
		i++;
	}

	MLX5_SET(manage_pages_out, out, output_num_entries, i);
	return 0;
}

static int reclaim_pages(struct mlx5_core_dev *dev, u32 func_id, int npages,
			 int *nclaimed, bool ec_function)
{
	int outlen = MLX5_ST_SZ_BYTES(manage_pages_out);
	u32 in[MLX5_ST_SZ_DW(manage_pages_in)] = {0};
	int num_claimed;
	u32 *out;
	int err;
	int i;
	int claimed = 0;

	if (nclaimed)
		*nclaimed = 0;

	outlen += npages * MLX5_FLD_SZ_BYTES(manage_pages_out, pas[0]);
	out = kvzalloc(outlen, GFP_KERNEL);
	if (!out)
		return -ENOMEM;

	MLX5_SET(manage_pages_in, in, opcode, MLX5_CMD_OP_MANAGE_PAGES);
	MLX5_SET(manage_pages_in, in, op_mod, MLX5_PAGES_TAKE);
	MLX5_SET(manage_pages_in, in, function_id, func_id);
	MLX5_SET(manage_pages_in, in, input_num_entries, npages);
	MLX5_SET(manage_pages_in, in, embedded_cpu_function, ec_function);

	mlx5_core_dbg(dev, "npages %d, outlen %d\n", npages, outlen);
	err = reclaim_pages_cmd(dev, in, sizeof(in), out, outlen);
	if (err) {
		mlx5_core_err(dev, "failed reclaiming pages: err %d\n", err);
		goto out_free;
	}

	num_claimed = MLX5_GET(manage_pages_out, out, output_num_entries);
	if (num_claimed > npages) {
		mlx5_core_warn(dev, "fw returned %d, driver asked %d => corruption\n",
			       num_claimed, npages);
		err = -EINVAL;
		goto out_free;
	}

	for (i = 0; i < num_claimed; i++)
		if (!free_4k(dev, MLX5_GET64(manage_pages_out, out, pas[i])))
			claimed++;

	if (dev->state == MLX5_DEVICE_STATE_INTERNAL_ERROR)
		free_pages_list(dev);

	if (nclaimed)
		*nclaimed = claimed;

	dev->priv.fw_pages -= claimed;
	if (func_id)
		dev->priv.vfs_pages -= claimed;
	else if (mlx5_core_is_ecpf(dev) && !ec_function)
		dev->priv.peer_pf_pages -= num_claimed;

out_free:
	kvfree(out);
	return err;
}

static void gc_work_handler(struct work_struct *work)
{
	struct delayed_work *dwork = container_of(work, struct delayed_work, work);
	struct mlx5_priv *priv = container_of(dwork, struct mlx5_priv, gc_dwork);
	struct mlx5_core_dev *dev = container_of(priv, struct mlx5_core_dev, priv);

	if (!priv->gc_allowed)
		return;

	free_pages_list(dev);
}

static void pages_work_handler(struct work_struct *work)
{
	struct mlx5_pages_req *req = container_of(work, struct mlx5_pages_req, work);
	struct mlx5_core_dev *dev = req->dev;
	int err = 0;

	if (req->npages < 0)
		err = reclaim_pages(dev, req->func_id, -1 * req->npages, NULL,
				    req->ec_function);
	else if (req->npages > 0)
		err = give_pages(dev, req->func_id, req->npages, 1, req->ec_function);

	if (err)
		mlx5_core_warn(dev, "%s fail %d\n",
			       req->npages < 0 ? "reclaim" : "give", err);

	kfree(req);
}

enum {
	EC_FUNCTION_MASK = 0x8000,
};

static int req_pages_handler(struct notifier_block *nb,
			     unsigned long type, void *data)
{
	struct mlx5_pages_req *req;
	struct mlx5_core_dev *dev;
	struct mlx5_priv *priv;
	struct mlx5_eqe *eqe;
	bool ec_function;
	u16 func_id;
	s32 npages;

	priv = mlx5_nb_cof(nb, struct mlx5_priv, pg_nb);
	dev  = container_of(priv, struct mlx5_core_dev, priv);
	eqe  = data;

	func_id = be16_to_cpu(eqe->data.req_pages.func_id);
	npages  = be32_to_cpu(eqe->data.req_pages.num_pages);
	ec_function = be16_to_cpu(eqe->data.req_pages.ec_function) & EC_FUNCTION_MASK;
	mlx5_core_dbg(dev, "page request for func 0x%x, npages %d\n",
		      func_id, npages);

	priv->gc_allowed = false;
	cancel_delayed_work(&priv->gc_dwork);

	req = kzalloc(sizeof(*req), GFP_ATOMIC);
	if (!req) {
		mlx5_core_warn(dev, "failed to allocate pages request\n");
		return NOTIFY_DONE;
	}

	req->dev = dev;
	req->func_id = func_id;
	req->npages = npages;
	req->ec_function = ec_function;
	INIT_WORK(&req->work, pages_work_handler);
	queue_work(priv->pg_wq, &req->work);

	if (req->npages < 0) {
		priv->gc_allowed = true;
		queue_delayed_work(priv->pg_wq, &priv->gc_dwork,
				   msecs_to_jiffies(GARBAGE_COLLECTOR_DELAY_MSECS));
	}

	return NOTIFY_OK;
}

int mlx5_satisfy_startup_pages(struct mlx5_core_dev *dev, int boot)
{
	u16 uninitialized_var(func_id);
	s32 uninitialized_var(npages);
	int err;

	err = mlx5_cmd_query_pages(dev, &func_id, &npages, boot);
	if (err)
		return err;

	mlx5_core_dbg(dev, "requested %d %s pages for func_id 0x%x\n",
		      npages, boot ? "boot" : "init", func_id);

	return give_pages(dev, func_id, npages, 0, mlx5_core_is_ecpf(dev));
}

enum {
	MLX5_BLKS_FOR_RECLAIM_PAGES = 12
};

static int optimal_reclaimed_pages(void)
{
	struct mlx5_cmd_prot_block *block;
	struct mlx5_cmd_layout *lay;
	int ret;

	ret = (sizeof(lay->out) + MLX5_BLKS_FOR_RECLAIM_PAGES * sizeof(block->data) -
	       MLX5_ST_SZ_BYTES(manage_pages_out)) /
	       MLX5_FLD_SZ_BYTES(manage_pages_out, pas[0]);

	return ret;
}

int mlx5_reclaim_startup_pages(struct mlx5_core_dev *dev)
{
	unsigned long end = jiffies + msecs_to_jiffies(MAX_RECLAIM_TIME_MSECS);
	struct fw_page *fwp;
	struct rb_node *p;
	int nclaimed = 0;
	int err = 0;

	while (dev->priv.fw_pages) {
		p = rb_first(&dev->priv.page_root);
		if (p) {
			fwp = rb_entry(p, struct fw_page, rb_node);
			err = reclaim_pages(dev, fwp->func_id,
					    optimal_reclaimed_pages(),
					    &nclaimed, mlx5_core_is_ecpf(dev));

			if (err) {
				mlx5_core_warn(dev, "failed reclaiming pages (%d)\n",
					       err);
				return err;
			}
			if (nclaimed)
				end = jiffies + msecs_to_jiffies(MAX_RECLAIM_TIME_MSECS);
		}
		if (time_after(jiffies, end)) {
			mlx5_core_warn(dev, "FW did not return all pages. giving up...\n");
			break;
		}
	}

	if (dev->state == MLX5_DEVICE_STATE_INTERNAL_ERROR) {
		dev->priv.vfs_pages = 0;
		dev->priv.fw_pages = 0;
	}

	WARN(dev->priv.fw_pages,
	     "FW pages counter is %d after reclaiming all pages\n",
	     dev->priv.fw_pages);
	WARN(dev->priv.vfs_pages,
	     "VFs FW pages counter is %d after reclaiming all pages\n",
	     dev->priv.vfs_pages);

	/* Warning but don't dump stack */
	if (dev->priv.peer_pf_pages)
		mlx5_core_warn(dev, "Peer PF FW pages counter is %d after reclaiming all pages\n",
			       dev->priv.peer_pf_pages);

	return 0;
}

int mlx5_pagealloc_init(struct mlx5_core_dev *dev)
{
	struct mlx5_priv *priv = &dev->priv;

	priv->page_root = RB_ROOT;
	INIT_LIST_HEAD(&priv->free_list);
	INIT_DELAYED_WORK(&priv->gc_dwork, gc_work_handler);
	priv->gc_allowed = true;
	dev->priv.pg_wq = create_singlethread_workqueue("mlx5_page_allocator");
	if (!dev->priv.pg_wq)
		return -ENOMEM;

	return 0;
}

void mlx5_pagealloc_cleanup(struct mlx5_core_dev *dev)
{
	struct mlx5_priv *priv = &dev->priv;
	struct fw_page *fwp;
	struct fw_page *tmp;
	struct rb_node *p;

	/* Remove the free list pages */
	list_for_each_entry_safe(fwp, tmp, &dev->priv.free_list, list) {
		list_del(&fwp->list);
		rb_erase(&fwp->rb_node, &priv->page_root);
		dma_unmap_page(&dev->pdev->dev, fwp->addr & MLX5_U64_4K_PAGE_MASK,
			       PAGE_SIZE, DMA_BIDIRECTIONAL);
		__free_page(fwp->page);
		kfree(fwp);
	}

	/* In case the FW didn't return all the pages free it by force */
	do {
		p = rb_first(&priv->page_root);
		if (p) {
			fwp = rb_entry(p, struct fw_page, rb_node);
			rb_erase(&fwp->rb_node, &priv->page_root);
			dma_unmap_page(&dev->pdev->dev, fwp->addr & MLX5_U64_4K_PAGE_MASK,
				       PAGE_SIZE, DMA_BIDIRECTIONAL);
			__free_page(fwp->page);
			kfree(fwp);
		}
	} while (p);
	destroy_workqueue(dev->priv.pg_wq);
}

void mlx5_pagealloc_start(struct mlx5_core_dev *dev)
{
	MLX5_NB_INIT(&dev->priv.pg_nb, req_pages_handler, PAGE_REQUEST);
	mlx5_eq_notifier_register(dev, &dev->priv.pg_nb);
}

void mlx5_pagealloc_stop(struct mlx5_core_dev *dev)
{
	cancel_delayed_work(&dev->priv.gc_dwork);
	dev->priv.gc_allowed = true;
	queue_delayed_work(dev->priv.pg_wq, &dev->priv.gc_dwork, 0);
	mlx5_eq_notifier_unregister(dev, &dev->priv.pg_nb);
	flush_workqueue(dev->priv.pg_wq);
}

int mlx5_wait_for_pages(struct mlx5_core_dev *dev, int *pages)
{
	unsigned long end = jiffies + msecs_to_jiffies(MAX_RECLAIM_VFS_PAGES_TIME_MSECS);
	int prev_pages = *pages;

	/* In case of internal error we will free the pages manually later */
	if (dev->state == MLX5_DEVICE_STATE_INTERNAL_ERROR) {
		mlx5_core_warn(dev, "Skipping wait for vf pages stage");
		return 0;
	}

	mlx5_core_dbg(dev, "Waiting for %d pages\n", prev_pages);
	while (*pages) {
		if (time_after(jiffies, end)) {
			mlx5_core_warn(dev, "aborting while there are %d pending pages\n", *pages);
			return -ETIMEDOUT;
		}
		if (*pages < prev_pages) {
			end = jiffies + msecs_to_jiffies(MAX_RECLAIM_VFS_PAGES_TIME_MSECS);
			prev_pages = *pages;
		}
		msleep(50);
	}

	mlx5_core_dbg(dev, "All pages received\n");
	return 0;
}
