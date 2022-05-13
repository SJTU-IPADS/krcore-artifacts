#ifndef MLX4_IB_USER_EXP_H
#define MLX4_IB_USER_EXP_H

#include <rdma/mlx4-abi.h>

struct mlx4_exp_ib_create_qp {
	struct mlx4_ib_create_qp	base;
	__u64				uar_virt_add;
};

#endif
