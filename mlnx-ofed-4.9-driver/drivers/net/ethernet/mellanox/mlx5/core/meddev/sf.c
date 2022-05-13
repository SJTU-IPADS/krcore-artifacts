// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
// Copyright (c) 2018-19 Mellanox Technologies

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/io-mapping.h>
#include <linux/mlx5/driver.h>
#include <linux/bitmap.h>
#include "eswitch.h"
#include "sf.h"
#include "mlx5_core.h"
#include "eswitch.h"
#include "devlink.h"

static int
mlx5_cmd_query_sf_partitions(struct mlx5_core_dev *mdev, u32 *out, int outlen)
{
	u32 in[MLX5_ST_SZ_DW(query_sf_partitions_in)] = {};

	/* Query sf partitions */
	MLX5_SET(query_sf_partitions_in, in, opcode,
		 MLX5_CMD_OP_QUERY_SF_PARTITION);
	return mlx5_cmd_exec(mdev, in, sizeof(in), out, outlen);
}

int mlx5_sf_table_init(struct mlx5_core_dev *dev,
		       struct mlx5_sf_table *sf_table)
{
	void *sf_parts;
	int n_support;
	int outlen;
	u32 *out;
	int err;

	outlen = MLX5_ST_SZ_BYTES(query_sf_partitions_out) + MLX5_ST_SZ_BYTES(sf_partition);
	out = kvzalloc(outlen, GFP_KERNEL);
	if (!out)
		return -ENOMEM;

	mutex_init(&sf_table->lock);
	/* SFs BAR is implemented in PCI BAR2 */
	sf_table->base_address = pci_resource_start(dev->pdev, 2);

	/* Query first partition */
	err = mlx5_cmd_query_sf_partitions(dev, out, outlen);
	if (err)
		goto free_outmem;

	n_support = MLX5_GET(query_sf_partitions_out, out, num_sf_partitions);
	sf_parts = MLX5_ADDR_OF(query_sf_partitions_out, out, sf_partition);
	sf_table->max_sfs = 1 << MLX5_GET(sf_partition, sf_parts, log_num_sf);
	sf_table->log_sf_bar_size =
		MLX5_GET(sf_partition, sf_parts, log_sf_bar_size);

	mlx5_core_dbg(dev, "supported partitions(%d)\n", n_support);
	mlx5_core_dbg(dev, "SF_part(0) log_num_sf(%d) log_sf_bar_size(%d)\n",
		      sf_table->max_sfs, sf_table->log_sf_bar_size);

	sf_table->sf_id_bitmap = bitmap_zalloc(sf_table->max_sfs, GFP_KERNEL);
	if (!sf_table->sf_id_bitmap) {
		err = -ENOMEM;
		goto free_outmem;
	}

free_outmem:
	kvfree(out);
	return err;
}

void mlx5_sf_table_cleanup(struct mlx5_core_dev *dev,
			   struct mlx5_sf_table *sf_table)
{
	mutex_destroy(&sf_table->lock);
	if (sf_table->sf_id_bitmap)
		bitmap_free(sf_table->sf_id_bitmap);
}

static int mlx5_cmd_alloc_sf(struct mlx5_core_dev *mdev, u16 function_id)
{
	u32 out[MLX5_ST_SZ_DW(alloc_sf_out)] = {};
	u32 in[MLX5_ST_SZ_DW(alloc_sf_in)] = {};

	MLX5_SET(alloc_sf_in, in, opcode, MLX5_CMD_OP_ALLOC_SF);
	MLX5_SET(alloc_sf_in, in, function_id, function_id);

	return mlx5_cmd_exec(mdev, in, sizeof(in), out, sizeof(out));
}

static int mlx5_cmd_dealloc_sf(struct mlx5_core_dev *mdev, u16 function_id)
{
	u32 out[MLX5_ST_SZ_DW(dealloc_sf_out)] = {};
	u32 in[MLX5_ST_SZ_DW(dealloc_sf_in)] = {};

	MLX5_SET(dealloc_sf_in, in, opcode, MLX5_CMD_OP_DEALLOC_SF);
	MLX5_SET(dealloc_sf_in, in, function_id, function_id);

	return mlx5_cmd_exec(mdev, in, sizeof(in), out, sizeof(out));
}

static int alloc_sf_id(struct mlx5_sf_table *sf_table, u16 *sf_id)
{
	int ret = 0;
	u16 idx;

	mutex_lock(&sf_table->lock);
	if (!sf_table->sf_id_bitmap) {
		ret = -ENOSPC;
		goto done;
	}
	idx = find_first_zero_bit(sf_table->sf_id_bitmap, sf_table->max_sfs);
	if (idx == sf_table->max_sfs) {
		ret = -ENOSPC;
		goto done;
	}
	bitmap_set(sf_table->sf_id_bitmap, idx, 1);
	*sf_id = idx;
done:
	mutex_unlock(&sf_table->lock);
	return ret;
}

static void free_sf_id(struct mlx5_sf_table *sf_table, u16 sf_id)
{
	mutex_lock(&sf_table->lock);
	bitmap_clear(sf_table->sf_id_bitmap, sf_id, 1);
	mutex_unlock(&sf_table->lock);
}

static u16 mlx5_sf_hw_id(const struct mlx5_core_dev *coredev, u16 sf_id)
{
	return mlx5_sf_base_id(coredev) + sf_id;
}

/* Perform SF allocation using parent device BAR. */
struct mlx5_sf *
mlx5_sf_alloc(struct mlx5_core_dev *coredev, struct mlx5_sf_table *sf_table,
	      struct device *dev)
{
	phys_addr_t base_addr;
	struct mlx5_sf *sf;
	u16 hw_function_id;
	u16 sf_id;
	int ret;

	sf = kzalloc(sizeof(*sf), GFP_KERNEL);
	if (!sf)
		return ERR_PTR(-ENOMEM);

	ret = alloc_sf_id(sf_table, &sf_id);
	if (ret)
		goto id_err;

	hw_function_id = mlx5_sf_hw_id(coredev, sf_id);
	ret = mlx5_cmd_alloc_sf(coredev, hw_function_id);
	if (ret)
		goto alloc_sf_err;

	ret = mlx5_core_enable_sf_hca(coredev, hw_function_id);
	if (ret)
		goto enable_err;

	ret = mlx5_eswitch_setup_sf_vport(coredev->priv.eswitch, hw_function_id);
	if (ret)
		goto vport_err;

	sf->idx = sf_id;
	sf->parent_dev = coredev;
	base_addr = sf_table->base_address +
			(sf_id << (sf_table->log_sf_bar_size + 12));
	sf->bar_base_addr = base_addr;
	return sf;

vport_err:
	mlx5_core_disable_sf_hca(coredev, hw_function_id);
enable_err:
	mlx5_cmd_dealloc_sf(coredev, hw_function_id);
alloc_sf_err:
	free_sf_id(sf_table, sf_id);
id_err:
	kfree(sf);
	return ERR_PTR(ret);
}

void mlx5_sf_free(struct mlx5_core_dev *coredev, struct mlx5_sf_table *sf_table,
		  struct mlx5_sf *sf)
{
	u16 hw_function_id;

	hw_function_id = mlx5_sf_hw_id(coredev, sf->idx);
	mlx5_eswitch_cleanup_sf_vport(coredev->priv.eswitch, hw_function_id);
	mlx5_core_disable_sf_hca(coredev, hw_function_id);
	mlx5_cmd_dealloc_sf(coredev, hw_function_id);
	free_sf_id(sf_table, sf->idx);
	kfree(sf);
}

u16 mlx5_get_free_sfs_locked(struct mlx5_core_dev *dev,
			     struct mlx5_sf_table *sf_table)
{
	u16 free_sfs = 0;

	if (sf_table->sf_id_bitmap)
		free_sfs = sf_table->max_sfs -
				bitmap_weight(sf_table->sf_id_bitmap,
					      sf_table->max_sfs);
	return free_sfs;
}

u16 mlx5_get_free_sfs(struct mlx5_core_dev *dev, struct mlx5_sf_table *sf_table)
{
	u16 free_sfs = 0;

	if (!mlx5_core_is_sf_supported(dev))
		return 0;

	mutex_lock(&sf_table->lock);
	free_sfs = mlx5_get_free_sfs_locked(dev, sf_table);
	mutex_unlock(&sf_table->lock);
	return free_sfs;
}

u16 mlx5_core_max_sfs(const struct mlx5_core_dev *dev,
		      struct mlx5_sf_table *sf_table)
{
	u16 max_sfs;

	mutex_lock(&sf_table->lock);
	max_sfs = mlx5_core_is_sf_supported(dev) ? sf_table->max_sfs : 0;
	mutex_unlock(&sf_table->lock);
	return max_sfs;
}

static void *mlx5_sf_dma_alloc(struct device *dev, size_t size,
			       dma_addr_t *dma_handle, gfp_t gfp,
			       unsigned long attrs)
{
	return dma_alloc_attrs(dev->parent, size, dma_handle, gfp, attrs);
}

static void
mlx5_sf_dma_free(struct device *dev, size_t size,
		 void *vaddr, dma_addr_t dma_handle,
		 unsigned long attrs)
{
	dma_free_attrs(dev->parent, size, vaddr, dma_handle, attrs);
}

static int
mlx5_sf_dma_mmap(struct device *dev, struct vm_area_struct *vma,
		 void *cpu_addr, dma_addr_t dma_addr, size_t size,
		 unsigned long attrs)
{
	return dma_mmap_attrs(dev->parent, vma, cpu_addr,
			      dma_addr, size, attrs);
}

static int
mlx5_sf_dma_get_sgtable(struct device *dev, struct sg_table *sgt,
			void *cpu_addr, dma_addr_t dma_addr, size_t size,
			unsigned long attrs)
{
	return dma_get_sgtable_attrs(dev->parent, sgt, cpu_addr,
				     dma_addr, size, attrs);
}

static dma_addr_t
mlx5_sf_dma_map_page(struct device *dev, struct page *page,
		     unsigned long offset, size_t size,
		     enum dma_data_direction dir,
		     unsigned long attrs)
{
	return dma_map_page_attrs(dev->parent, page, offset, size, dir, attrs);
}

static void
mlx5_sf_dma_unmap_page(struct device *dev, dma_addr_t dma_handle,
		       size_t size, enum dma_data_direction dir,
		       unsigned long attrs)
{
	dma_unmap_page_attrs(dev->parent, dma_handle, size, dir, attrs);
}

static int
mlx5_sf_dma_map_sg(struct device *dev, struct scatterlist *sg,
		   int nents, enum dma_data_direction dir,
		   unsigned long attrs)
{
	return dma_map_sg_attrs(dev->parent, sg, nents, dir, attrs);
}

static void
mlx5_sf_dma_unmap_sg(struct device *dev, struct scatterlist *sg, int nents,
		     enum dma_data_direction dir, unsigned long attrs)
{
	dma_unmap_sg_attrs(dev->parent, sg, nents, dir, attrs);
}

static dma_addr_t
mlx5_sf_dma_map_resource(struct device *dev, phys_addr_t phys_addr,
			 size_t size, enum dma_data_direction dir,
			 unsigned long attrs)
{
	return dma_map_resource(dev->parent, phys_addr, size, dir, attrs);
}

static void
mlx5_sf_dma_unmap_resource(struct device *dev, dma_addr_t dma_handle,
			   size_t size, enum dma_data_direction dir,
			   unsigned long attrs)
{
	dma_unmap_resource(dev->parent, dma_handle, size, dir, attrs);
}

static void
mlx5_sf_dma_sync_single_for_cpu(struct device *dev,
				dma_addr_t dma_handle, size_t size,
				enum dma_data_direction dir)
{
	dma_sync_single_for_cpu(dev->parent, dma_handle, size, dir);
}

static void
mlx5_sf_dma_sync_single_for_device(struct device *dev,
				   dma_addr_t dma_handle, size_t size,
				   enum dma_data_direction dir)
{
	dma_sync_single_for_device(dev->parent, dma_handle, size, dir);
}

static void
mlx5_sf_dma_sync_sg_for_cpu(struct device *dev,
			    struct scatterlist *sg, int nents,
			    enum dma_data_direction dir)
{
	dma_sync_sg_for_cpu(dev->parent, sg, nents, dir);
}

static void
mlx5_sf_dma_sync_sg_for_device(struct device *dev,
			       struct scatterlist *sg, int nents,
			       enum dma_data_direction dir)
{
	dma_sync_sg_for_device(dev->parent, sg, nents, dir);
}

static void
mlx5_sf_dma_cache_sync(struct device *dev, void *vaddr, size_t size,
		       enum dma_data_direction dir)
{
	dma_cache_sync(dev->parent, vaddr, size, dir);
}

static const struct dma_map_ops mlx5_sf_dma_ops = {
	.alloc = mlx5_sf_dma_alloc,
	.free = mlx5_sf_dma_free,
	.mmap = mlx5_sf_dma_mmap,
	.get_sgtable = mlx5_sf_dma_get_sgtable,
	.map_page = mlx5_sf_dma_map_page,
	.unmap_page = mlx5_sf_dma_unmap_page,
	.map_sg = mlx5_sf_dma_map_sg,
	.unmap_sg = mlx5_sf_dma_unmap_sg,
	.map_resource = mlx5_sf_dma_map_resource,
	.unmap_resource = mlx5_sf_dma_unmap_resource,
	.sync_single_for_cpu = mlx5_sf_dma_sync_single_for_cpu,
	.sync_sg_for_cpu = mlx5_sf_dma_sync_sg_for_cpu,
	.sync_sg_for_device = mlx5_sf_dma_sync_sg_for_device,
	.sync_single_for_device = mlx5_sf_dma_sync_single_for_device,
	.cache_sync = mlx5_sf_dma_cache_sync,
};

static void set_dma_params(struct mlx5_core_dev *coredev, struct device *dev)
{
	struct pci_dev *pdev = coredev->pdev;

	dev->dma_ops = &mlx5_sf_dma_ops;
	dev->dma_mask = pdev->dev.dma_mask;
	dev->dma_parms = pdev->dev.dma_parms;
	dma_set_coherent_mask(dev, pdev->dev.coherent_dma_mask);
	dma_set_max_seg_size(dev, dma_get_max_seg_size(&pdev->dev));
}

int mlx5_sf_load(struct mlx5_sf *sf)
{
	struct mlx5_core_dev *dev = sf->dev;
	struct mlx5_core_dev *parent_dev;
	int err;

	dev->iseg = ioremap(dev->iseg_base, sizeof(*dev->iseg));
	if (!dev->iseg) {
		mlx5_core_warn(dev, "remap error for sf=%d\n", sf->idx);
		return -ENOMEM;
	}
	parent_dev = sf->parent_dev;
	set_dma_params(parent_dev, dev->device);

	err = mlx5_mdev_init(dev, MLX5_DEFAULT_PROF);
	if (err) {
		mlx5_core_warn(dev, "mlx5_mdev_init on sf=%d err=%d\n",
			       sf->idx, err);
		goto mdev_err;
	}

	err = mlx5_load_one(dev, true);
	if (err) {
		mlx5_core_warn(dev, "mlx5_load_one sf=%d err=%d\n",
			       sf->idx, err);
		goto load_one_err;
	}
	return 0;

load_one_err:
	mlx5_mdev_uninit(dev);
mdev_err:
	iounmap(dev->iseg);
	return err;
}

void mlx5_sf_unload(struct mlx5_sf *sf)
{
	{
	/* Yuk, This is done because remove_one()
	 * invokes devlink_unregister() which must be mirror
	 * of mlx5_load_one() which does devlink_register().
	 * Code normally should be,
	 * mlx5_load_one()
	 *    devlink_register()
	 *
	 * mlx5_unload_one()
	 *    devlin_unregister()
	 * This is done incorrectly in upstream currently.
	 * Correcting it, will result into lock dependency asserts
	 * and deadlock described in
	 * http://l-gerrit.mtl.labs.mlnx:8080/#/c/upstream/linux/+/281516/
	 *
	 * Assert on load_one() can be ignored because its false assert.
	 * Its false because, devlink cannot reload a devlink device which
	 * hasn't yet done devlink_register() while holding
	 * interface_state_mutex.
	 * To avoid this devlink user space commands must not take
	 * devlink lock as proposed.
	 */
		struct devlink *devlink = priv_to_devlink(sf->dev);

		devlink_unregister(devlink);
	}

	mlx5_unload_one(sf->dev, true);
	mlx5_mdev_uninit(sf->dev);
	iounmap(sf->dev->iseg);
}

int mlx5_sf_set_mac(struct mlx5_sf *sf, u8 *mac)
{
	struct mlx5_core_dev *parent_dev = sf->parent_dev;
	u16 vport_num;
	int ret;

	vport_num = mlx5_sf_hw_id(parent_dev, sf->idx);
	ret = mlx5_eswitch_set_vport_mac(parent_dev->priv.eswitch,
					 vport_num, mac);
	return ret;
}

int mlx5_sf_get_mac(struct mlx5_sf *sf, u8 *mac)
{
	struct mlx5_core_dev *parent_dev = sf->parent_dev;
	u16 vport_num;
	int ret;

	vport_num = mlx5_sf_hw_id(parent_dev, sf->idx);
	ret = mlx5_eswitch_get_vport_mac(parent_dev->priv.eswitch,
					 vport_num, mac);
	return ret;
}

struct net_device *mlx5_sf_get_netdev(struct mlx5_sf *sf)
{
	struct mlx5_core_dev *parent_dev = sf->parent_dev;
	struct net_device *ndev;
	u16 vport_num;

	vport_num = mlx5_sf_hw_id(parent_dev, sf->idx);

	ndev = mlx5_eswitch_get_proto_dev(parent_dev->priv.eswitch,
					  vport_num, REP_ETH);
	if (!ndev)
		return ERR_PTR(-ENODEV);
	/* FIXME This is racy. get_proto_dev()) is poor API
	 * without a esw lock.
	 */
	dev_hold(ndev);
	return ndev;
}

static int mlx5_cmd_set_sf_partitions(struct mlx5_core_dev *mdev, int n,
				      u8 *parts)
{
	unsigned int inlen = MLX5_ST_SZ_BYTES(set_sf_partitions_out) +
		n * MLX5_ST_SZ_BYTES(sf_partition);
	u32 out[MLX5_ST_SZ_DW(set_sf_partitions_out)] = {};
	void *dest_parts;
	u32 *in;
	int err;

	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	MLX5_SET(set_sf_partitions_in, in, opcode,
		 MLX5_CMD_OP_SET_SF_PARTITION);
	MLX5_SET(set_sf_partitions_in, in, num_sf_partitions, n);
	dest_parts = MLX5_ADDR_OF(set_sf_partitions_in, in, sf_partition);
	memcpy(dest_parts, parts, n * MLX5_ST_SZ_BYTES(sf_partition));

	err = mlx5_cmd_exec(mdev, in, inlen, out, sizeof(out));
	if (err)
		mlx5_core_err(mdev, "%s err %d\n", __func__, err);
	kvfree(in);
	return err;
}

static int reinit_sf_table(struct mlx5_core_dev *dev,
			   struct mlx5_sf_table *sf_table,
			   u16 new_max_sfs, u32 log_sf_bar_size)
{
	unsigned long *sf_id_bitmap;

	if (sf_table->sf_id_bitmap) {
		bitmap_free(sf_table->sf_id_bitmap);
		sf_table->sf_id_bitmap = NULL;
		sf_table->max_sfs = 0;
	}

	sf_id_bitmap = bitmap_zalloc(new_max_sfs, GFP_KERNEL);
	if (!sf_id_bitmap)
		return -ENOMEM;
	sf_table->sf_id_bitmap = sf_id_bitmap;
	sf_table->max_sfs = new_max_sfs;
	sf_table->log_sf_bar_size = log_sf_bar_size;
	return 0;
}

int mlx5_sf_set_max_sfs(struct mlx5_core_dev *dev,
			struct mlx5_sf_table *sf_table, u16 new_max_sfs)
{
	u16 p0_log_new_num_sfs;
	int log_bar_size_delta;
	int p0_log_bar_sz;
	void *sf_parts;
	u16 p0_num_sfs;
	int n_support;
	int outlen;
	u32 *out;
	int ret;

	if (!new_max_sfs || !is_power_of_2(new_max_sfs) ||
	    new_max_sfs > mlx5_eswitch_max_sfs(dev))
		return -EINVAL;

	outlen = MLX5_ST_SZ_BYTES(query_sf_partitions_out) +
					MLX5_ST_SZ_BYTES(sf_partition);
	out = kvzalloc(outlen, GFP_KERNEL);
	if (!out)
		return -ENOMEM;

	/* Lock the sf table so that allocation doesn't happen in parallel */
	mutex_lock(&sf_table->lock);
	if (sf_table->max_sfs &&
	    mlx5_get_free_sfs_locked(dev, sf_table) != sf_table->max_sfs) {
		ret = -EBUSY;
		goto free_parts_out;
	}

	/* Query first partition */
	ret = mlx5_cmd_query_sf_partitions(dev, out, outlen);
	if (ret)
		goto free_parts_out;

	n_support = MLX5_GET(query_sf_partitions_out, out, num_sf_partitions);
	sf_parts = MLX5_ADDR_OF(query_sf_partitions_out, out, sf_partition);
	p0_num_sfs = 1 << MLX5_GET(sf_partition, sf_parts, log_num_sf);
	p0_log_bar_sz = MLX5_GET(sf_partition, sf_parts, log_sf_bar_size);
	if (p0_num_sfs == new_max_sfs) {
		ret = 0;
		goto free_parts_out;
	}
	p0_log_new_num_sfs = ilog2(new_max_sfs);
	if (new_max_sfs > p0_num_sfs) {
		log_bar_size_delta =
			p0_log_new_num_sfs -
				MLX5_GET(sf_partition, sf_parts, log_num_sf);
		p0_log_bar_sz -= log_bar_size_delta;
	} else {
		log_bar_size_delta =
			MLX5_GET(sf_partition, sf_parts, log_num_sf) -
							p0_log_new_num_sfs;
		p0_log_bar_sz += log_bar_size_delta;
	}

	MLX5_SET(sf_partition, sf_parts, log_num_sf, p0_log_new_num_sfs);
	MLX5_SET(sf_partition, sf_parts, log_sf_bar_size, p0_log_bar_sz);

	ret = mlx5_cmd_set_sf_partitions(dev, 1, sf_parts);
	if (ret)
		goto free_parts_out;

	ret = reinit_sf_table(dev, sf_table, new_max_sfs, p0_log_bar_sz);

free_parts_out:
	mutex_unlock(&sf_table->lock);
	kvfree(out);
	return ret;
}
