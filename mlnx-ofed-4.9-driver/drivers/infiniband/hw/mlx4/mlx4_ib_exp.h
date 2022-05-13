#ifndef MLX4_IB_EXP_H
#define MLX4_IB_EXP_H

#include <linux/compiler.h>
#include <linux/list.h>
#include <rdma/ib_verbs.h>
#include <rdma/ib_cmem.h>
#include <linux/mlx4/device.h>
#include <linux/mlx4/qp.h>
#include "user_exp.h"

struct mlx4_ib_qp;
struct ib_qp_init_attr;
struct mlx4_qp_context;
int qp_has_rq(struct ib_qp_init_attr *attr);

/****************************************/
/* ioctl codes */
/****************************************/
#define MLX4_IOC_MAGIC 'm'
#define MLX4_IOCHWCLOCKOFFSET _IOR(MLX4_IOC_MAGIC, 1, int)

#define MLX4_IB_EXP_MMAP_CMD_MASK 0xFF
#define MLX4_IB_EXP_MMAP_CMD_BITS 8
/*
 * Set MLX4_IB_MAX_CTX_UARS to 256 each UAR related to 8 BFs.
 * This provides maximum of 256 * 8 = 2048 BFs.
 * To gain performance we may need BF per core which means we can support
 * up to 2048 cores with dedicated BF per context.
 */
#define MLX4_IB_MAX_CTX_UARS 256

enum mlx4_ib_exp_mmap_cmd {
	MLX4_IB_EXP_MMAP_GET_CONTIGUOUS_PAGES = 2,

	/* Use EXP mmap commands until it is pushed to upstream */
	MLX4_IB_EXP_MMAP_GET_CONTIGUOUS_PAGES_CPU_NUMA	= 0xFC,
	MLX4_IB_EXP_MMAP_GET_CONTIGUOUS_PAGES_DEV_NUMA	= 0xFD,
	MLX4_IB_EXP_MMAP_EXT_UAR_PAGE			= 0xFE,
	MLX4_IB_EXP_MMAP_EXT_BLUE_FLAME_PAGE		= 0xFF,
};

struct mlx4_ib_qpg_data {
	unsigned long *tss_bitmap;
	unsigned long *rss_bitmap;
	struct mlx4_ib_qp *qpg_parent;
	int tss_qpn_base;
	int rss_qpn_base;
	u32 tss_child_count;
	u32 rss_child_count;
	u32 qpg_tss_mask_sz;
	struct mlx4_ib_dev *dev;
	unsigned long flags;
	struct kref refcount;
};

int mlx4_ib_exp_contig_mmap(struct ib_ucontext *context, struct vm_area_struct *vma,
			    unsigned long  command);
int mlx4_ib_exp_uar_mmap(struct ib_ucontext *context, struct vm_area_struct *vma,
			    unsigned long  command);
int mlx4_ib_exp_bf_mmap(struct ib_ucontext *context, struct vm_area_struct *vma,
			    unsigned long  command);

static inline int is_exp_contig_command(unsigned long  command)
{
	if (command == MLX4_IB_EXP_MMAP_GET_CONTIGUOUS_PAGES ||
	    command == MLX4_IB_EXP_MMAP_GET_CONTIGUOUS_PAGES_CPU_NUMA ||
	    command == MLX4_IB_EXP_MMAP_GET_CONTIGUOUS_PAGES_DEV_NUMA)
		return 1;

	return 0;
}

int mlx4_ib_exp_modify_cq(struct ib_cq *cq, struct ib_cq_attr *cq_attr, int cq_attr_mask);
struct ib_qp *mlx4_ib_exp_create_qp(struct ib_pd *pd,
				    struct ib_exp_qp_init_attr *init_attr,
				    struct ib_udata *udata);
void mlx4_ib_set_exp_qp_flags(struct mlx4_ib_qp *qp, struct ib_qp_init_attr *init_attr);
void mlx4_ib_set_exp_attr_flags(struct mlx4_ib_qp *qp, struct ib_qp_init_attr *init_attr);
int mlx4_ib_exp_query_device(struct ib_device *ibdev,
			     struct ib_exp_device_attr *props,
			     struct ib_udata *uhw);
int mlx4_ib_exp_ioctl(struct ib_ucontext *context, unsigned int cmd, unsigned long arg);
int mlx4_ib_alloc_qpn_common(struct mlx4_ib_dev *dev, struct mlx4_ib_qp *qp,
			     struct ib_qp_init_attr *attr, int *qpn, int is_exp);
struct mlx4_ib_ucontext;
void mlx4_ib_unalloc_qpn_common(struct mlx4_ib_dev *dev, struct mlx4_ib_qp *qp, int qpn,
				struct mlx4_ib_ucontext *context,
				enum mlx4_ib_source_type src,
				bool dirty_release);
void mlx4_ib_release_qpn_common(struct mlx4_ib_dev *dev, struct mlx4_ib_qp *qp,
				struct mlx4_ib_ucontext *context,
				enum mlx4_ib_source_type src,
				bool dirty_release);
int mlx4_ib_check_qpg_attr(struct ib_pd *pd, struct ib_qp_init_attr *attr);
struct ib_qp *mlx4_ib_create_qp_wrp(struct ib_pd *pd,
				    struct ib_qp_init_attr *init_attr,
				    struct ib_udata *udata);
void mlx4_ib_modify_qp_rss(struct mlx4_ib_dev *dev, struct mlx4_ib_qp *qp,
			   struct mlx4_qp_context *context);
int mlx4_ib_set_qp_user_uar(struct ib_pd *pd, struct mlx4_ib_qp *qp,
			  struct ib_udata *udata,
			  int is_exp);
struct ib_mr *mlx4_ib_phys_addr(struct ib_pd *pd, u64 length, u64 virt_addr,
				int access_flags);
#endif
