/*
 * Copyright (c) 2018, Mellanox Technologies. All rights reserved.
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

#include "mlx4.h"

#define BAD_ACCESS			0xBADACCE5
#define HEALTH_BUFFER_SIZE		0x40
#define CR_ENABLE_BIT			swab32(BIT(6))
#define CR_ENABLE_BIT_OFFSET		0xF3F04
#define MAX_NUM_OF_DUMPS_TO_STORE	(8)
#define CRDUMP_PROC_DIR "crdump"

static const char *region_cr_space_str = "cr-space";
static const char *region_fw_health_str = "fw-health";

/* Set to true in case cr enable bit was set to true before crdump */
static bool crdump_enbale_bit_set;

static struct proc_dir_entry *crdump_proc_dir;

static void crdump_enable_crspace_access(struct mlx4_dev *dev,
					 u8 __iomem *cr_space)
{
	/* Get current enable bit value */
	crdump_enbale_bit_set =
		readl(cr_space + CR_ENABLE_BIT_OFFSET) & CR_ENABLE_BIT;

	/* Enable FW CR filter (set bit6 to 0) */
	if (crdump_enbale_bit_set)
		writel(readl(cr_space + CR_ENABLE_BIT_OFFSET) & ~CR_ENABLE_BIT,
		       cr_space + CR_ENABLE_BIT_OFFSET);

	/* Enable block volatile crspace accesses */
	writel(swab32(1), cr_space + dev->caps.health_buffer_addrs +
	       HEALTH_BUFFER_SIZE);
}

static void crdump_disable_crspace_access(struct mlx4_dev *dev,
					  u8 __iomem *cr_space)
{
	/* Disable block volatile crspace accesses */
	writel(0, cr_space + dev->caps.health_buffer_addrs +
	       HEALTH_BUFFER_SIZE);

	/* Restore FW CR filter value (set bit6 to original value) */
	if (crdump_enbale_bit_set)
		writel(readl(cr_space + CR_ENABLE_BIT_OFFSET) | CR_ENABLE_BIT,
		       cr_space + CR_ENABLE_BIT_OFFSET);
}

void mlx4_crdump_proc_init(struct proc_dir_entry *proc_core_dir)
{
	if (proc_core_dir)
		crdump_proc_dir = proc_mkdir(CRDUMP_PROC_DIR, proc_core_dir);
}

void mlx4_crdump_proc_cleanup(struct proc_dir_entry *proc_core_dir)
{
	if (proc_core_dir && crdump_proc_dir)
		remove_proc_entry(CRDUMP_PROC_DIR, proc_core_dir);
}

static void mlx4_crdump_collect_crspace(struct mlx4_dev *dev,
					u8 __iomem *cr_space,
					u32 id)
{
	struct mlx4_fw_crdump *crdump = &dev->persist->crdump;
	struct pci_dev *pdev = dev->persist->pdev;
	unsigned long cr_res_size;
	u8 *crspace_data;
	int offset;
	int err;

	if (!crdump->region_crspace) {
		mlx4_err(dev, "crdump: cr-space region is NULL\n");
		return;
	}

	/* Try to collect CR space */
	cr_res_size = pci_resource_len(pdev, 0);
	crspace_data = kvmalloc(cr_res_size, GFP_KERNEL);
	if (crspace_data) {
		for (offset = 0; offset < cr_res_size; offset += 4)
			*(u32 *)(crspace_data + offset) =
					readl(cr_space + offset);

		err = devlink_region_snapshot_create(crdump->region_crspace,
						     cr_res_size, crspace_data,
						     id, &kvfree);
		if (err) {
			kvfree(crspace_data);
			mlx4_warn(dev, "crdump: devlink create %s snapshot id %d err %d\n",
				  region_cr_space_str, id, err);
		} else {
			mlx4_info(dev, "crdump: added snapshot %d to devlink region %s\n",
				  id, region_cr_space_str);
		}
	} else {
		mlx4_err(dev, "crdump: Failed to allocate crspace buffer\n");
	}
}

static void mlx4_crdump_collect_fw_health(struct mlx4_dev *dev,
					  u8 __iomem *cr_space,
					  u32 id)
{
	struct mlx4_fw_crdump *crdump = &dev->persist->crdump;
	u8 *health_data;
	int offset;
	int err;

	if (!crdump->region_fw_health) {
		mlx4_err(dev, "crdump: fw-health region is NULL\n");
		return;
	}

	/* Try to collect health buffer */
	health_data = kvmalloc(HEALTH_BUFFER_SIZE, GFP_KERNEL);
	if (health_data) {
		u8 __iomem *health_buf_start =
				cr_space + dev->caps.health_buffer_addrs;

		for (offset = 0; offset < HEALTH_BUFFER_SIZE; offset += 4)
			*(u32 *)(health_data + offset) =
					readl(health_buf_start + offset);

		err = devlink_region_snapshot_create(crdump->region_fw_health,
						     HEALTH_BUFFER_SIZE,
						     health_data,
						     id, &kvfree);
		if (err) {
			kvfree(health_data);
			mlx4_warn(dev, "crdump: devlink create %s snapshot id %d err %d\n",
				  region_fw_health_str, id, err);
		} else {
			mlx4_info(dev, "crdump: added snapshot %d to devlink region %s\n",
				  id, region_fw_health_str);
		}
	} else {
		mlx4_err(dev, "crdump: Failed to allocate health buffer\n");
	}
}

int mlx4_crdump_collect(struct mlx4_dev *dev)
{
	struct devlink *devlink = priv_to_devlink(mlx4_priv(dev));
	struct mlx4_fw_crdump *crdump = &dev->persist->crdump;
	struct pci_dev *pdev = dev->persist->pdev;
	unsigned long cr_res_size;
	u8 __iomem *cr_space;
	u32 id;
	int offset;

	if (!dev->caps.health_buffer_addrs) {
		mlx4_info(dev, "crdump: FW doesn't support health buffer access, skipping\n");
		return 0;
	}

	if (crdump->crspace || crdump->health) {
		mlx4_info(dev, "crdump: Dump was already collected, skipping\n");
		return 0;
	}

	cr_res_size = pci_resource_len(pdev, 0);

	cr_space = ioremap(pci_resource_start(pdev, 0), cr_res_size);
	if (!cr_space) {
		mlx4_err(dev, "crdump: Failed to map pci cr region\n");
		return -ENODEV;
	}

	crdump_enable_crspace_access(dev, cr_space);

	/* Try to collect CR space */
	crdump->crspace = kzalloc(cr_res_size, GFP_KERNEL);
	if (crdump->crspace) {
		for (offset = 0; offset < cr_res_size; offset += 4)
			*(u32*)(crdump->crspace + offset) =
					swab32(readl(cr_space + offset));
		crdump->crspace_size = cr_res_size;
		mlx4_err(dev, "crdump: read CR-Cpace\n");
	} else {
		mlx4_err(dev, "crdump: Failed to allocate crspace buffer\n");
	}

	/* Try to collect health buffer */
	crdump->health = kzalloc(HEALTH_BUFFER_SIZE, GFP_KERNEL);
	if (crdump->health) {
		u8 *health_buf_s = cr_space + dev->caps.health_buffer_addrs;
		for (offset = 0; offset < HEALTH_BUFFER_SIZE; offset += 4)
			*(u32*)(crdump->health + offset) =
					swab32(readl(health_buf_s + offset));
		crdump->health_size = HEALTH_BUFFER_SIZE;
	} else {
		mlx4_err(dev, "crdump: Failed to allocate health buffer\n");
	}

	if (crdump->crspace || crdump->health)
		mlx4_info(dev, "crdump: Crash snapshot collected to /proc/%s/%s/%s\n",
				MLX4_CORE_PROC, CRDUMP_PROC_DIR,
				pci_name(dev->persist->pdev));

	if (!crdump->snapshot_enable) {
		mlx4_info(dev, "crdump: devlink snapshot disabled, skipping\n");
		goto out;
	}

	/* Get the available snapshot ID for the dumps */
	id = devlink_region_shapshot_id_get(devlink);

	/* Try to capture dumps */
	mlx4_crdump_collect_crspace(dev, cr_space, id);
	mlx4_crdump_collect_fw_health(dev, cr_space, id);

	if (crdump->crspace || crdump->health)
		mlx4_info(dev, "crdump: Crash snapshot collected to /proc/%s/%s/%s\n",
				MLX4_CORE_PROC, CRDUMP_PROC_DIR,
				pci_name(dev->persist->pdev));

out:
	crdump_disable_crspace_access(dev, cr_space);
	iounmap(cr_space);
	return 0;
}

static void *crdump_seq_start(struct seq_file *s, loff_t *pos)
{
	struct mlx4_fw_crdump *crdump = s->private;

	if (!crdump || (!crdump->crspace_size && !crdump->health_size))
		return NULL;

	return pos;
}

static void crdump_seq_stop(struct seq_file *s, void *v)
{
	/* nothing to do */
}

static int crdump_seq_show(struct seq_file *s, void *v)
{
	struct mlx4_fw_crdump *crdump = s->private;
	loff_t *pos = v;
	u32 value, byte_offset;

	if (!crdump || !pos)
		return 0;

	byte_offset = (*pos) * 4;

	if (byte_offset == 0)
		seq_printf(s, "CRDUMP CRSPACE DUMP\n");
	else if (byte_offset == crdump->crspace_size)
		seq_printf(s, "\nCRDUMP HEALTH BUFFER\n");
	else if (byte_offset == crdump->crspace_size + crdump->health_size)
		seq_printf(s, "CRDUMP DONE\n");

	if (byte_offset < crdump->crspace_size) {
		value = *(u32*)(crdump->crspace + byte_offset);
		seq_printf(s, "0x%08x 0x%08x\n", byte_offset, value);
	} else if (byte_offset < crdump->crspace_size + crdump->health_size) {
		byte_offset -= crdump->crspace_size;
		value = *(u32*)(crdump->health + byte_offset);
		seq_printf(s, "0x%08x 0x%08x\n", byte_offset, value);
	}

	return 0;
}

static void *crdump_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
	struct mlx4_fw_crdump *crdump = s->private;

	if (!crdump || !pos)
		return NULL;

	if ((*pos) * 4 >= (crdump->crspace_size + crdump->health_size))
		return NULL;

	*pos += 1;

	return pos;
}

static struct seq_operations crdump_seq_ops = {
	.start = crdump_seq_start,
	.stop  = crdump_seq_stop,
	.show  = crdump_seq_show,
	.next  = crdump_seq_next,
};

static int crdump_proc_open(struct inode *inode, struct file *file)
{
	struct seq_file *seq;
	int ret;

	ret = seq_open(file, &crdump_seq_ops);
	if (ret)
		return ret;

	seq = file->private_data;
	seq->private = PDE_DATA(inode);

	return 0;
}

static const struct file_operations crdump_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= crdump_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

int mlx4_crdump_init(struct mlx4_dev *dev)
{
	struct devlink *devlink = priv_to_devlink(mlx4_priv(dev));
	struct mlx4_fw_crdump *crdump = &dev->persist->crdump;
	struct pci_dev *pdev = dev->persist->pdev;

	memset(crdump, 0, sizeof(struct mlx4_fw_crdump));
	crdump->snapshot_enable = false;

	/* Create cr-space region */
	crdump->region_crspace =
		devlink_region_create(devlink,
				      region_cr_space_str,
				      MAX_NUM_OF_DUMPS_TO_STORE,
				      pci_resource_len(pdev, 0));
	if (IS_ERR(crdump->region_crspace))
		mlx4_warn(dev, "crdump: create devlink region %s err %ld\n",
			  region_cr_space_str,
			  PTR_ERR(crdump->region_crspace));

	/* Create fw-health region */
	crdump->region_fw_health =
		devlink_region_create(devlink,
				      region_fw_health_str,
				      MAX_NUM_OF_DUMPS_TO_STORE,
				      HEALTH_BUFFER_SIZE);
	if (IS_ERR(crdump->region_fw_health))
		mlx4_warn(dev, "crdump: create devlink region %s err %ld\n",
			  region_fw_health_str,
			  PTR_ERR(crdump->region_fw_health));

	if (crdump_proc_dir)
		proc_create_data(pci_name(dev->persist->pdev), S_IRUGO,
				 crdump_proc_dir, &crdump_proc_fops, crdump);

	return 0;
}

void mlx4_crdump_end(struct mlx4_dev *dev)
{
	struct mlx4_fw_crdump *crdump = &dev->persist->crdump;

	devlink_region_destroy(crdump->region_fw_health);
	devlink_region_destroy(crdump->region_crspace);

	if (crdump_proc_dir)
		remove_proc_entry(pci_name(dev->persist->pdev), crdump_proc_dir);

	if (crdump->crspace_size) {
		crdump->crspace_size = 0;
		kfree(crdump->crspace);
	}

	if (crdump->health_size) {
		crdump->health_size = 0;
		kfree(crdump->health);
	}

}
