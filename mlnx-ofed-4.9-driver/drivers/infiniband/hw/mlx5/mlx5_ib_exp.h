/*
 * Copyright (c) 2013-2016, Mellanox Technologies. All rights reserved.
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

#ifndef MLX5_IB_EXP_H
#define MLX5_IB_EXP_H

#include <linux/mlx5/nvmf.h>
#include <rdma/ib_verbs.h>
#include "srq.h"

struct mlx5_ib_dev;
struct mlx5_ib_rwq;
struct mlx5_ib_create_wq_data;
struct mlx5_ib_qp;
#define MLX5_DC_CONNECT_QP_DEPTH 8192
#define MLX5_IB_QPT_SW_CNAK	IB_QPT_RESERVED5

enum mlx5_cap_flags {
	MLX5_CAP_COMPACT_AV = 1 << 0,
	MLX5_CAP_ODP_IMPLICIT = 1 << 1,
};

enum {
	MLX5_DCT_CS_RES_64	= 2,
	MLX5_CNAK_RX_POLL_CQ_QUOTA	= 256,
};

struct mlx5_dc_desc {
	dma_addr_t	dma;
	void		*buf;
};

enum mlx5_op {
	MLX5_WR_OP_MLX	= 1,
};

struct mlx5_mlx_wr {
	u8	sl;
	u16	dlid;
	int	icrc;
};

struct mlx5_send_wr {
	struct ib_send_wr	wr;
	union {
		struct mlx5_mlx_wr	mlx;
	} sel;
};

struct mlx5_dc_stats {
	struct kobject		kobj;
	struct mlx5_ib_dev	*dev;
	int			port;
	atomic64_t		connects;
	atomic64_t		cnaks;
	atomic64_t		discards;
	int			*rx_scatter;
	int			initialized;
};

struct mlx5_dc_data {
	struct ib_mr		*mr;
	struct ib_qp		*dcqp;
	struct ib_cq		*rcq;
	struct ib_cq		*scq;
	unsigned int		rx_npages;
	unsigned int		tx_npages;
	struct mlx5_dc_desc	*rxdesc;
	struct mlx5_dc_desc	*txdesc;
	unsigned int		max_wqes;
	unsigned int		cur_send;
	unsigned int		last_send_completed;
	int			tx_pending;
	struct mlx5_ib_dev	*dev;
	int			port;
	int			initialized;
	int			index;
	int			tx_signal_factor;
	struct ib_wc		wc_tbl[MLX5_CNAK_RX_POLL_CQ_QUOTA];
};

enum {
	TCLASS_MATCH_SRC_ADDR_IP,
	TCLASS_MATCH_DST_ADDR_IP,
	TCLASS_MATCH_SRC_ADDR_IP6,
	TCLASS_MATCH_DST_ADDR_IP6,
	TCLASS_MATCH_TCLASS,
	TCLASS_MATCH_TCLASS_NO_PREFIX,
	TCLASS_MATCH_MAX,
};

struct tclass_match {
	u32 mask;
	u8 s_addr[16];
	u8 d_addr[16];
	u8 d_addr_m[16];
	int     tclass; /* Should be always last! */
};

struct tclass_parse_node {
	int (*parse)(const char *str, void *store, void *store_mask);
	int (*compare)(struct tclass_match *match, struct tclass_match *match2,
		       bool with_mask);
	size_t (*print)(struct tclass_match *match, char *buf, size_t size);
	const char *pattern;
	size_t v_offset;
	size_t m_offset;
	u32 mask;
};

#define TCLASS_CREATE_PARSE_NODE(type, parse, compare, print, pattern,	\
				 mask, v_member, m_member)		\
	[(type)] = {parse, compare, print, pattern,			\
		    offsetof(struct tclass_match, v_member),		\
		    offsetof(struct tclass_match, m_member), mask}

enum {
	TCLASS_MATCH_MASK_SRC_ADDR_IP = BIT(TCLASS_MATCH_SRC_ADDR_IP),
	TCLASS_MATCH_MASK_DST_ADDR_IP = BIT(TCLASS_MATCH_DST_ADDR_IP),
	TCLASS_MATCH_MASK_SRC_ADDR_IP6 = BIT(TCLASS_MATCH_SRC_ADDR_IP6),
	TCLASS_MATCH_MASK_DST_ADDR_IP6 = BIT(TCLASS_MATCH_DST_ADDR_IP6),
	TCLASS_MATCH_MASK_TCLASS = BIT(TCLASS_MATCH_TCLASS),
	TCLASS_MATCH_MASK_MAX = BIT(TCLASS_MATCH_MAX),
};

#define TCLASS_MAX_RULES 40
#define TCLASS_MAX_CMD 100

struct mlx5_tc_data {
	struct tclass_match rule[TCLASS_MAX_RULES];
	struct mutex lock;
	bool initialized;
	int val;
	struct kobject kobj;
	struct mlx5_ib_dev *ibdev;
};

#define MLX5_FS_MAX_TYPES	 6
#define MLX5_FS_LOG_MAX_ENTRIES	 16

struct mlx5_steering_data {
	unsigned int ingress_log_ft_size[MLX5_BY_PASS_NUM_PRIOS];
	struct kobject kobj;
	struct mlx5_ib_dev *ibdev;
	int initialized;
};

void tclass_get_tclass(struct mlx5_ib_dev *dev,
		       struct mlx5_tc_data *tcd,
		       const struct rdma_ah_attr *ah,
		       u8 port,
		       u8 *tclass);

struct mlx5_ttl_data {
	int val;
	struct kobject kobj;
};

struct mlx5_dc_tracer {
	struct page	*pg;
	dma_addr_t	dma;
	int		size;
	int		order;
};

struct mlx5_ib_dc_target {
	struct ib_dct		ibdct;
	struct mlx5_ib_qp    *qp;
};

struct mlx5_ib_exp_odp_stats {
	/* Debug statistics */
	struct dentry           *odp_debugfs;

	/* Number of ODP MRs currently in use */
	atomic_t                num_odp_mrs;
	/* Total size of ODP MRs in pages */
	atomic_t                num_odp_mr_pages;
	/* Number of instances when the MR couldn't be found during page fault
	 * handling
	 */
	atomic_t                num_mrs_not_found;
	/* Number of instances when the page fault encountered an error */
	atomic_t                num_failed_resolutions;
	/* Number of instances of timeout waiting for mmu notifier */
	atomic_t                num_timeout_mmu_notifier;
};

static inline struct mlx5_ib_dc_target *to_mdct(struct ib_dct *ibdct)
{
	return container_of(ibdct, struct mlx5_ib_dc_target, ibdct);
}

int init_steering_sysfs(struct mlx5_ib_dev *dev);
void cleanup_steering_sysfs(struct mlx5_ib_dev *dev);
int init_tc_sysfs(struct mlx5_ib_dev *dev);
void cleanup_tc_sysfs(struct mlx5_ib_dev *dev);
int init_ttl_sysfs(struct mlx5_ib_dev *dev);
void cleanup_ttl_sysfs(struct mlx5_ib_dev *dev);

struct ib_dct *mlx5_ib_create_dc_target(struct ib_pd *pd,
				  struct ib_dct_init_attr *attr,
				  struct ib_udata *udata);
int mlx5_ib_destroy_dc_target(struct ib_dct *dct, struct ib_udata *udata);
int mlx5_ib_query_dc_target(struct ib_dct *dct, struct ib_dct_attr *attr);
int mlx5_ib_arm_dc_target(struct ib_dct *dct, struct ib_udata *udata);

int mlx5_ib_exp_modify_cq(struct ib_cq *cq, struct ib_cq_attr *cq_attr,
			  int cq_attr_mask);

int mlx5_ib_exp_query_device(struct ib_device *ibdev,
			     struct ib_exp_device_attr *props,
			     struct ib_udata *uhw);

int mlx5_ib_exp_is_scat_cqe_dci(struct mlx5_ib_dev *dev,
				enum ib_sig_type sig_type,
				int scqe_sz);

int mlx5_ib_exp_max_inl_recv(struct ib_qp_init_attr *init_attr);

void mlx5_ib_exp_get_hash_parameters(struct ib_qp_init_attr *init_attr,
				     struct ib_rwq_ind_table **rwq_ind_tbl,
				     u64 *rx_hash_fields_mask,
				     u32 *ind_tbl_num,
				     u8 **rx_hash_key,
				     u8 *rx_hash_function,
				     u8 *rx_key_len);
bool mlx5_ib_exp_is_rss(struct ib_qp_init_attr *init_attr);

int mlx5_ib_exp_set_context_attr(struct ib_device *device,
				 struct ib_ucontext *context,
				 struct ib_exp_context_attr *attr);

enum {
	MLX5_MAX_SINGLE_STRIDE_LOG_NUM_BYTES	= 13,
	MLX5_MIN_SINGLE_STRIDE_LOG_NUM_BYTES	= 6,
	MLX5_MAX_SINGLE_WQE_LOG_NUM_STRIDES	= 16,
	MLX5_MIN_SINGLE_WQE_LOG_NUM_STRIDES	= 9,
	MLX5_EXT_MIN_SINGLE_WQE_LOG_NUM_STRIDES	= 3,
};

enum mlx5_ib_exp_mmap_cmd {
	MLX5_IB_MMAP_GET_CONTIGUOUS_PAGES		= 1,
	MLX5_IB_EXP_MMAP_CORE_CLOCK = 0xFB,
	MLX5_IB_EXP_MMAP_GET_CONTIGUOUS_PAGES_CPU_NUMA  = 0xFC,
	MLX5_IB_EXP_MMAP_GET_CONTIGUOUS_PAGES_DEV_NUMA  = 0xFD,
	MLX5_IB_EXP_ALLOC_N_MMAP_WC                     = 0xFE,
	MLX5_IB_EXP_MMAP_CLOCK_INFO			= 0xFF,
};

int get_pg_order(unsigned long offset);

static inline int is_exp_contig_command(unsigned long command)
{
	if (command == MLX5_IB_MMAP_GET_CONTIGUOUS_PAGES ||
	    command == MLX5_IB_EXP_MMAP_GET_CONTIGUOUS_PAGES_CPU_NUMA ||
	    command == MLX5_IB_EXP_MMAP_GET_CONTIGUOUS_PAGES_DEV_NUMA)
		return 1;

	return 0;
}

int mlx5_ib_exp_contig_mmap(struct ib_ucontext *ibcontext,
			    struct vm_area_struct *vma,
			    unsigned long  command);
struct ib_mr *mlx5_ib_phys_addr(struct ib_pd *pd, u64 length, u64 virt_addr,
				int access_flags);
int mlx5_ib_mmap_dc_info_page(struct mlx5_ib_dev *dev,
			      struct vm_area_struct *vma);
int mlx5_ib_init_dc_improvements(struct mlx5_ib_dev *dev);
void mlx5_ib_cleanup_dc_improvements(struct mlx5_ib_dev *dev);

void mlx5_ib_set_mlx_seg(struct mlx5_mlx_seg *seg, struct mlx5_mlx_wr *wr);

#ifdef CONFIG_INFINIBAND_ON_DEMAND_PAGING
int mlx5_ib_prefetch_mr(struct ib_mr *ibmr, u64 start, u64 length, u32 flags);
#endif

#ifdef CONFIG_INFINIBAND_ON_DEMAND_PAGING
struct mlx5_ib_dev;
int mlx5_ib_exp_odp_init_one(struct mlx5_ib_dev *ibdev);
#endif

void mlx5_ib_get_atomic_caps(struct mlx5_ib_dev *dev,
			     struct ib_device_attr *props,
			     int is_exp);
void mlx5_ib_config_atomic_responder(struct mlx5_ib_dev *dev,
				     struct ib_exp_device_attr *props);
u32 mlx5_ib_atomic_mode_qp(struct mlx5_ib_qp *qp);

int mlx5_ib_exp_query_mkey(struct ib_mr *mr, u64 mkey_attr_mask,
			   struct ib_mkey_attr *mkey_attr);

void mlx5_ib_exp_set_rqc(void *rqc, struct mlx5_ib_rwq *rwq);

void mlx5_ib_exp_set_rq_attr(struct mlx5_ib_create_wq_data *data,
			     struct mlx5_ib_rwq *rwq);

int mlx5_ib_exp_get_cmd_data(struct mlx5_ib_dev *dev,
			     struct ib_udata *udata,
			     struct mlx5_ib_create_wq_data *data);

int mlx5_ib_exp_create_srq_user(struct mlx5_ib_dev *dev,
				struct mlx5_srq_attr *in,
				struct ib_udata *udata,
				struct mlx5_ib_create_srq *ucmd);

struct ib_mr *mlx5_ib_get_dma_mr_ex(struct ib_pd *pd, int acc,
				    u64 start_addr, u64 length);

struct ib_mr *mlx5_ib_exp_alloc_mr(struct ib_pd *pd, struct ib_mr_init_attr *attr);

/* NVMEoF target offload */
void mlx5_ib_internal_fill_nvmf_caps(struct mlx5_ib_dev *dev);
int mlx5_ib_exp_set_nvmf_srq_attrs(struct mlx5_nvmf_attr *nvmf,
				   struct ib_srq_init_attr *init_attr);

struct mlx5_ib_nvmf_be_ctrl {
	struct ib_nvmf_ctrl		ibctrl;
	struct mlx5_core_nvmf_be_ctrl	mctrl;
};

struct mlx5_ib_nvmf_ns {
	struct ib_nvmf_ns		ibns;
	struct mlx5_core_nvmf_ns	mns;
};

static inline struct mlx5_ib_nvmf_be_ctrl *
to_mibctrl(struct mlx5_core_nvmf_be_ctrl *mctrl)
{
	return container_of(mctrl, struct mlx5_ib_nvmf_be_ctrl, mctrl);
}

static inline struct mlx5_ib_nvmf_be_ctrl *to_mctrl(struct ib_nvmf_ctrl *ibctrl)
{
	return container_of(ibctrl, struct mlx5_ib_nvmf_be_ctrl, ibctrl);
}

static inline struct mlx5_ib_nvmf_ns *to_mns(struct ib_nvmf_ns *ibns)
{
	return container_of(ibns, struct mlx5_ib_nvmf_ns, ibns);
}

struct ib_dm *mlx5_ib_exp_alloc_dm(struct ib_device *ibdev,
				   struct ib_ucontext *context,
				   u64 length, u64 uaddr,
				   struct ib_udata *uhw);

int mlx5_ib_exp_free_dm(struct ib_dm *dm,
			struct uverbs_attr_bundle *attrs);

struct ib_nvmf_ctrl *mlx5_ib_create_nvmf_backend_ctrl(struct ib_srq *srq,
		struct ib_nvmf_backend_ctrl_init_attr *init_attr);
int mlx5_ib_destroy_nvmf_backend_ctrl(struct ib_nvmf_ctrl *ctrl);
struct ib_nvmf_ns *mlx5_ib_attach_nvmf_ns(struct ib_nvmf_ctrl *ctrl,
		struct ib_nvmf_ns_init_attr *init_attr);
int mlx5_ib_detach_nvmf_ns(struct ib_nvmf_ns *ns);
int mlx5_ib_query_nvmf_ns(struct ib_nvmf_ns *ns,
			  struct ib_nvmf_ns_attr *ns_attr);

struct mlx5_ib_ucontext;
struct mlx5_ib_vma_private_data;

int alloc_and_map_wc(struct mlx5_ib_dev *dev,
		     struct mlx5_ib_ucontext *context, u32 indx,
		     struct vm_area_struct *vma);

int mlx5_ib_set_qp_offload_type(struct mlx5_qp_context *context, struct ib_qp *qp,
				enum ib_qp_offload_type offload_type);

int mlx5_ib_set_qp_srqn(struct mlx5_qp_context *context, struct ib_qp *qp,
			u32 srqn);

int mlx5_ib_set_vma_data(struct vm_area_struct *vma,
			 struct mlx5_ib_ucontext *ctx,
			 struct mlx5_ib_vma_private_data *vma_prv);

static inline pgprot_t mlx5_ib_pgprot_writecombine(pgprot_t prot)
{
#if defined(CONFIG_ARM64)
	/*
	 * Fix up arm64 braindamage of using NORMAL_NC for write
	 * combining when Device GRE exists specifically for the
	 * purpose. Needed on ThunderX2.
	 */
	switch (read_cpuid_id() & MIDR_CPU_MODEL_MASK) {
	case MIDR_CPU_MODEL(ARM_CPU_IMP_BRCM, BRCM_CPU_PART_VULCAN):
	case MIDR_CPU_MODEL(0x43, 0x0af):  /* Cavium ThunderX2 */
		prot = __pgprot_modify(prot, PTE_ATTRINDX_MASK,
				       PTE_ATTRINDX(MT_DEVICE_GRE) |
				       PTE_PXN | PTE_UXN);
	}
#endif
	return prot;
}

#endif
