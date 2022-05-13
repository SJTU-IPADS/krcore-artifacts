/*
 * Copyright (c) 2014-2016, Mellanox Technologies, Ltd.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _LINUX_NVME_PEER_H
#define _LINUX_NVME_PERR_H

#include <linux/pci.h>
#include <linux/blkdev.h>

enum nvme_peer_resource_mask {
	NVME_PEER_SQT_DBR		= 1 << 0,
	NVME_PEER_CQH_DBR		= 1 << 1,
	NVME_PEER_SQ_PAS		= 1 << 2,
	NVME_PEER_CQ_PAS		= 1 << 3,
	NVME_PEER_SQ_SZ			= 1 << 4,
	NVME_PEER_CQ_SZ			= 1 << 5,
	NVME_PEER_MEM_LOG_PG_SZ		= 1 << 6,
};


struct nvme_peer_resource {
	struct mutex lock;
	bool in_use;
	phys_addr_t sqt_dbr_addr;
	phys_addr_t cqh_dbr_addr;
	dma_addr_t  sq_dma_addr;
	dma_addr_t  cq_dma_addr;
	__u32       nvme_cq_size;
	__u32       nvme_sq_size;
	__u8        memory_log_page_size;
	__u8        flags;

	/*
	 * Allows notifying the master peer to free his resources when slave peer
	 * initiates destruction.
	 */
	void (* stop_master_peer)(void *priv);
	void *dd_data;
};


struct nvme_peer_resource *nvme_peer_get_resource(struct pci_dev *pdev,
	enum nvme_peer_resource_mask mask,
	void (* stop_master_peer)(void *priv), void *dd_data);
void nvme_peer_put_resource(struct nvme_peer_resource *resource, bool restart);
struct pci_dev *nvme_find_pdev_from_bdev(struct block_device *bdev);
unsigned nvme_find_ns_id_from_bdev(struct block_device *bdev);

#endif /* _LINUX_NVME_PEER_H */
