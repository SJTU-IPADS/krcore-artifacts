#include <linux/hardirq.h>
#include <linux/export.h>

#include <linux/mlx4/cmd.h>

#include "mlx4.h"
#include "icm.h"

#define MLX4_CQ_FLAG_OI			(1 << 17)

int mlx4_cq_ignore_overrun(struct mlx4_dev *dev, struct mlx4_cq *cq)
{
	struct mlx4_cmd_mailbox *mailbox;
	struct mlx4_cq_context *cq_context;
	int err;

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);

	cq_context = mailbox->buf;
	memset(cq_context, 0, sizeof *cq_context);

	cq_context->flags |= cpu_to_be32(MLX4_CQ_FLAG_OI);

	err = mlx4_cmd(dev, mailbox->dma, cq->cqn, 3, MLX4_CMD_MODIFY_CQ,
		       MLX4_CMD_TIME_CLASS_A, MLX4_CMD_WRAPPED);

	mlx4_free_cmd_mailbox(dev, mailbox);
	return err;
}
EXPORT_SYMBOL_GPL(mlx4_cq_ignore_overrun);
