/*
 * Copyright (c) 2017, Mellanox Technologies, Ltd.  All rights reserved.
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

#ifndef _RDMA_OFFLOAD_H
#define _RDMA_OFFLOAD_H

#include <rdma/ib_verbs.h>
#include "nvmet.h"

#define NVMET_DYNAMIC_STAGING_BUFFER_PAGE_SIZE_MB 1
#define NVMET_DEFAULT_CMD_TIMEOUT_USEC 30000000

struct nvmet_rdma_xrq;
struct nvmet_rdma_device;
struct nvmet_rdma_cmd;
struct nvmet_rdma_queue queue;

enum nvmet_rdma_offload_ns_counter {
	NVMET_RDMA_OFFLOAD_NS_READ_CMDS,
	NVMET_RDMA_OFFLOAD_NS_READ_BLOCKS,
	NVMET_RDMA_OFFLOAD_NS_WRITE_CMDS,
	NVMET_RDMA_OFFLOAD_NS_WRITE_BLOCKS,
	NVMET_RDMA_OFFLOAD_NS_WRITE_INLINE_CMDS,
	NVMET_RDMA_OFFLOAD_NS_FLUSH_CMDS,
	NVMET_RDMA_OFFLOAD_NS_ERROR_CMDS,
	NVMET_RDMA_OFFLOAD_NS_BACKEND_ERROR_CMDS,
};

struct nvmet_rdma_backend_ctrl {
	struct ib_nvmf_ctrl	  *ibctrl;
	struct ib_nvmf_ns	  *ibns;
	struct nvmet_ns		  *ns;
	struct pci_dev		  *pdev;
	struct list_head	  entry;
	struct nvme_peer_resource *ofl;
	struct nvmet_rdma_xrq	  *xrq;
	struct work_struct	  release_work;
	/* Restart the nvme queue for future usage */
	bool			  restart;
};

struct nvmet_rdma_offload_ctx {
	struct nvmet_rdma_xrq	*xrq;
	struct list_head	entry;
};

struct nvmet_rdma_offload_ctrl {
	struct list_head	ctx_list;
	struct mutex		ctx_mutex;
};

struct nvmet_rdma_staging_buf_pool {
	struct list_head	list;
	int			size;
};

struct nvmet_rdma_staging_buf {
	void			  **staging_pages;
	dma_addr_t		  *staging_dma_addrs;
	u16 			  num_pages;
	unsigned int		  page_size; // in Bytes
	struct list_head	  entry;
	bool 			  dynamic;
	struct nvmet_rdma_xrq	  *xrq;
};

struct nvmet_rdma_xrq {
	struct nvmet_rdma_device	*ndev;
	struct nvmet_port		*port;
	struct nvmet_subsys		*subsys;
	int				offload_ctrls_cnt;
	struct mutex			offload_ctrl_mutex;
	struct list_head		be_ctrls_list;
	int				nr_be_ctrls;
	struct mutex			be_mutex;
	struct ib_srq			*ofl_srq;
	struct ib_cq			*cq;
	struct nvmet_rdma_cmd		*ofl_srq_cmds;
	size_t				ofl_srq_size;
	struct nvmet_rdma_staging_buf	*st;
	struct kref			ref;
	struct list_head		entry;
	unsigned int			nvme_queue_depth;
};

static void nvmet_rdma_free_st_buff(struct nvmet_rdma_staging_buf *st);
static void nvmet_rdma_destroy_xrq(struct kref *ref);
static int nvmet_rdma_find_get_xrq(struct nvmet_rdma_queue *queue,
				   struct nvmet_ctrl *ctrl);
static u16 nvmet_rdma_install_offload_queue(struct nvmet_sq *sq);
static int nvmet_rdma_create_offload_ctrl(struct nvmet_ctrl *ctrl);
static void nvmet_rdma_destroy_offload_ctrl(struct nvmet_ctrl *ctrl);
static int nvmet_rdma_enable_offload_ns(struct nvmet_ctrl *ctrl,
					struct nvmet_ns *ns);
static void nvmet_rdma_disable_offload_ns(struct nvmet_ctrl *ctrl,
					  struct nvmet_ns *ns);
static bool nvmet_rdma_peer_to_peer_capable(struct nvmet_port *nport);
static bool nvmet_rdma_check_subsys_match_offload_port(struct nvmet_port *nport,
						struct nvmet_subsys *subsys);
static unsigned int nvmet_rdma_peer_to_peer_sqe_inline_size(struct nvmet_ctrl *ctrl);
static u8 nvmet_rdma_peer_to_peer_mdts(struct nvmet_port *nport);
static u64 nvmet_rdma_offload_subsys_unknown_ns_cmds(struct nvmet_subsys *subsys);
static u64 nvmet_rdma_offload_ns_read_cmds(struct nvmet_ns *ns);
static u64 nvmet_rdma_offload_ns_read_blocks(struct nvmet_ns *ns);
static u64 nvmet_rdma_offload_ns_write_cmds(struct nvmet_ns *ns);
static u64 nvmet_rdma_offload_ns_write_blocks(struct nvmet_ns *ns);
static u64 nvmet_rdma_offload_ns_write_inline_cmds(struct nvmet_ns *ns);
static u64 nvmet_rdma_offload_ns_flush_cmds(struct nvmet_ns *ns);
static u64 nvmet_rdma_offload_ns_error_cmds(struct nvmet_ns *ns);
static u64 nvmet_rdma_offload_ns_backend_error_cmds(struct nvmet_ns *ns);
static int nvmet_rdma_init_st_pool(struct nvmet_rdma_staging_buf_pool *pool,
				   unsigned long long mem_start,
				   unsigned int mem_size,
				   unsigned int buffer_size);

#endif /* _RDMA_OFFLOAD_H */
