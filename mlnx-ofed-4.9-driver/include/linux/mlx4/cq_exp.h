#ifndef MLX4_CQ_EXP_H
#define MLX4_CQ_EXP_H

#include <linux/types.h>
#include <uapi/linux/if_ether.h>

#include <linux/mlx4/device.h>
#include <linux/mlx4/doorbell.h>

int mlx4_cq_ignore_overrun(struct mlx4_dev *dev, struct mlx4_cq *cq);

#endif
