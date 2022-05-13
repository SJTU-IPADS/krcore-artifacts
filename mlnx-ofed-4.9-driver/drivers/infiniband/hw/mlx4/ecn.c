/*
 * Copyright (c) 2012 Mellanox Technologies. All rights reserved.
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
 *
 */

#include <linux/mlx4/cmd.h>
#include <linux/debugfs.h>
#include <linux/export.h>
#include "mlx4_ib.h"
#include "ecn.h"

int mlx4_congestion_control(struct mlx4_ib_dev *dev,
			    enum congestion_control_opmod opmod,
			    u8 port, u8 priority, u8 algorithm, u8 clear,
			    union congestion_control_in_mailbox *in_mb,
			    union congestion_control_out_mailbox *out_mb,
			    u32 out_mb_size, u32 in_mb_size)
{
	struct mlx4_cmd_mailbox *mailbox_in = NULL;
	u64 mailbox_in_dma = 0;
	int err = 0;
	u32 inmod = (port + 1) | (priority << 8) |
		     (algorithm << 16) |
		     ((clear & 1) << 31);

	if (in_mb) {
		mailbox_in = mlx4_alloc_cmd_mailbox(dev->dev);
		if (IS_ERR(mailbox_in))
			return -1;
		mailbox_in_dma = mailbox_in->dma;
		memcpy(mailbox_in->buf, in_mb, in_mb_size);
	}

	if (out_mb) {
		struct mlx4_cmd_mailbox *mailbox_out;

		mailbox_out = mlx4_alloc_cmd_mailbox(dev->dev);

		if (IS_ERR(mailbox_out)) {
			err = -1;
			goto out1;
		}
		err = mlx4_cmd_box(dev->dev, mailbox_in_dma, mailbox_out->dma,
				   inmod, opmod,
				   MLX4_CMD_CONGESTION_CTRL_OPCODE,
				   MLX4_CMD_TIME_CLASS_C,
				   MLX4_CMD_WRAPPED);
		if (!err)
			memcpy(out_mb, mailbox_out->buf, out_mb_size);

		mlx4_free_cmd_mailbox(dev->dev, mailbox_out);

	} else {
		err = mlx4_cmd(dev->dev, mailbox_in_dma, inmod,
			       opmod, MLX4_CMD_CONGESTION_CTRL_OPCODE,
			       MLX4_CMD_TIME_CLASS_C,
			       MLX4_CMD_WRAPPED);
	}
out1:
	if (mailbox_in)
		mlx4_free_cmd_mailbox(dev->dev, mailbox_in);

	return err;
}

int mlx4_congestion_control_get_params(struct mlx4_ib_dev *dev,
				       u8 port, u8 priority, u8 algorithm,
				       u8 clear,
				       union
				       congestion_control_params_mailbox
				       *out_mb) {
	return mlx4_congestion_control(dev, CONGESTION_CONTROL_GET_PARAMS,
				       port, priority, algorithm, clear,
				       NULL,
				       (union congestion_control_out_mailbox *)
				       out_mb, sizeof(*out_mb), 0);
}

int mlx4_congestion_control_get_statistics(struct mlx4_ib_dev *dev,
					   u8 port, u8 algorithm,
					   u8 clear,
					   union
					   congestion_control_statistics_mailbox
					   *out_mb) {
	return mlx4_congestion_control(dev, CONGESTION_CONTROL_GET_STATISTICS,
				       port, 0, algorithm, clear,
				       NULL,
				       (union congestion_control_out_mailbox *)
				       out_mb, sizeof(*out_mb), 0);
}

int mlx4_congestion_control_set_params(struct mlx4_ib_dev *dev,
				       u8 port, u8 priority, u8 algorithm,
				       u8 clear,
				       union
				       congestion_control_params_mailbox
				       *in_mb) {
	return mlx4_congestion_control(dev, CONGESTION_CONTROL_SET_PARAMS,
				       port, priority, algorithm, clear,
				       (union congestion_control_in_mailbox *)
				       in_mb, NULL, 0, sizeof(*in_mb));
}

int mlx4_congestion_control_set_gen_params(struct mlx4_ib_dev *dev,
					   u8 algorithm,
					   union
					   congestion_control_gen_params_mailbox
					   *in_mb) {
	return mlx4_congestion_control(dev, CONGESTION_CONTROL_SET_GEN_PARAMS,
				       0, 0, algorithm, 0,
				       (union congestion_control_in_mailbox *)
				       in_mb, NULL, 0, sizeof(*in_mb));
}

int mlx4_congestion_control_get_gen_params(struct mlx4_ib_dev *dev,
					   u8 algorithm,
					   union
					   congestion_control_gen_params_mailbox
					   *out_mb) {
	return mlx4_congestion_control(dev, CONGESTION_CONTROL_GET_GEN_PARAMS,
				       0, 0, algorithm, 0,
				       NULL,
				       (union congestion_control_out_mailbox *)
				       out_mb, sizeof(*out_mb), 0);
}

struct congestion_cmd_params {
	u32				opmod;
	struct congestion_control_inmod inmod;
	u64				data;
	struct mlx4_ib_dev		*dev;
};

struct congestion_dbgfs_entry {
	struct mlx4_ib_dev		*dev;
	const struct congestion_dbgfs_param_entry *desc;
	struct list_head list_enable;
	u8				port;
	u8				prio;
	u8				algo;
	u8				reserved[5];
};

struct congestion_dbgfs_param_entry {
	const char *name;
	u64 mask;
	u8 byte_offset;
	u8 bit_offset;
	u8 en_byte_offset;
	u8 en_bit_offset;
	u8 on;
};

#define ADD_CON_DBGFS_PARAM_ON_ENTRY(name_, byte_, offset_, mask_,	\
				  en_byte_, en_bit_, on_) {		\
	.name = name_, .byte_offset = byte_, .bit_offset = offset_,	\
	.mask = (u64)mask_, .en_bit_offset = en_bit_,			\
	.en_byte_offset = en_byte_, .on = on_}

#define ADD_CON_DBGFS_PARAM_ENTRY(name_, byte_, offset_, mask_, en_byte_,\
				  en_bit_)				 \
	ADD_CON_DBGFS_PARAM_ON_ENTRY(name_, byte_, offset_, mask_,	 \
				     en_byte_, en_bit_, 0)

#define ADD_CON_DBGFS_FILE_ENTRY(name_, byte_, offset_, mask_)		\
	ADD_CON_DBGFS_PARAM_ENTRY(name_, byte_, offset_, mask_, 0, 0)

static const struct congestion_dbgfs_param_entry params_ar_roce_rp[] = {
	ADD_CON_DBGFS_PARAM_ENTRY("disable_hai_stage", 3, 17, 1 << 17, 0, 17),
	ADD_CON_DBGFS_PARAM_ENTRY("clamp_tgt_rate_after_time_inc", 3, 23,
				  1 << 23, 0, 23),
	ADD_CON_DBGFS_PARAM_ON_ENTRY("force_rc_tos", 3, 24, 1 << 24, 0, 24, 1),
	ADD_CON_DBGFS_PARAM_ON_ENTRY("force_uc_tos", 3, 25, 1 << 25, 0, 25, 1),
	ADD_CON_DBGFS_PARAM_ENTRY("clamp_tgt_rate", 3, 27, 1 << 27, 0, 27),
	ADD_CON_DBGFS_PARAM_ENTRY("fast_rise", 3, 29, 1 << 29, 0, 29),
	ADD_CON_DBGFS_PARAM_ON_ENTRY("cnp_receive_enable", 3, 31, 1 << 31,
				     0, 31, 1),

	ADD_CON_DBGFS_PARAM_ENTRY("rpgTimeReset", 5, 0, 0xFFFFFFFF, 1, 30),
	ADD_CON_DBGFS_PARAM_ENTRY("rpgByteReset", 6, 0, 0xFFFFFFFF, 1, 29),
	ADD_CON_DBGFS_PARAM_ENTRY("rpgThreshold", 7, 0, 0xFFFFFFFF, 1, 28),
	ADD_CON_DBGFS_PARAM_ENTRY("rpgMaxRate", 8, 0, 0xFFFFFFFF, 1, 27),
	ADD_CON_DBGFS_PARAM_ENTRY("rpgAiRate", 9, 0, 0xFFFFFFFF, 1, 26),
	ADD_CON_DBGFS_PARAM_ENTRY("rpgHaiRate", 10, 0, 0xFFFFFFFF, 1, 25),
	ADD_CON_DBGFS_PARAM_ENTRY("alpha_to_rate_shift", 11, 0, 0xFFFFFFFF,
				  1, 24),
	ADD_CON_DBGFS_PARAM_ENTRY("rpgMinDecFac", 12, 0, 0xFFFFFFFF, 1, 23),
	ADD_CON_DBGFS_PARAM_ENTRY("rpgMinRate", 13, 0, 0xFFFFFFFF, 1, 22),
	ADD_CON_DBGFS_PARAM_ENTRY("max_time_rise", 14, 0, 0xFFFFFFFF, 1, 21),
	ADD_CON_DBGFS_PARAM_ENTRY("max_byte_rise", 15, 0, 0xFFFFFFFF, 1, 20),
	ADD_CON_DBGFS_PARAM_ENTRY("alpha_to_rate_coeff", 18, 0, 0xFFFFFFFF,
				  1, 17),
	ADD_CON_DBGFS_PARAM_ENTRY("marked_ratio_multiplier", 19, 0, 0xFFFFFFFF,
				  1, 16),
	ADD_CON_DBGFS_PARAM_ENTRY("marked_ratio_shift", 20, 0, 0xFFFFFFFF,
				  1, 15),
	ADD_CON_DBGFS_PARAM_ENTRY("rate_to_set_on_first_cnp", 21, 0,
				  0xFFFFFFFF, 1, 14),
	ADD_CON_DBGFS_PARAM_ENTRY("dceTcpG", 22, 0, 0xFFFFFFFF, 1, 13),
	ADD_CON_DBGFS_PARAM_ENTRY("dceTcpRtt", 23, 0, 0xFFFFFFFF, 1, 12),
};

#define N_PARAMS_ARROCE_RP (sizeof(params_ar_roce_rp) / \
			    sizeof(params_ar_roce_rp[0]))

static const struct congestion_dbgfs_param_entry params_ar_roce_np[] = {
	ADD_CON_DBGFS_PARAM_ENTRY("compn_ecn_rate_limit", 3, 30, 1 << 30, 0,
				  30),
	ADD_CON_DBGFS_PARAM_ON_ENTRY("ecn_receive_enable", 3, 31, 1 << 31, 0,
				     31, 1),
	ADD_CON_DBGFS_PARAM_ENTRY("num_injector", 4, 0, 0xFFFFFFFF, 1, 31),
	ADD_CON_DBGFS_PARAM_ENTRY("cnp_timer", 5, 0, 0xFFFFFFFF, 1, 30),
	ADD_CON_DBGFS_PARAM_ENTRY("num_congestion_cycles_to_keep", 7, 0,
				  0xFFFFFFFF, 1, 28),
	ADD_CON_DBGFS_PARAM_ENTRY("cnp_802p_prio", 6, 0, 0x7, 1, 27),
	ADD_CON_DBGFS_PARAM_ENTRY("cnp_dscp", 6, 8, 0x3F00, 1, 26),
};

#define N_PARAMS_ARROCE_NP (sizeof(params_ar_roce_np) / \
			    sizeof(params_ar_roce_np[0]))

static const struct congestion_dbgfs_param_entry statistics_ar_roce_rp[] = {
	ADD_CON_DBGFS_FILE_ENTRY("rpppRpCentiseconds", 0, 0,
				 0xFFFFFFFFFFFFFFFF),
	ADD_CON_DBGFS_FILE_ENTRY("ignored_cnp", 3, 0, 0xFFFFFFFF),
	ADD_CON_DBGFS_FILE_ENTRY("allocated_rate_limiter", 4, 0, 0xFFFFFFFF),
	ADD_CON_DBGFS_FILE_ENTRY("estimated_average_total_limiters_rate", 5,
				 0, 0xFFFFFFFF),
	ADD_CON_DBGFS_FILE_ENTRY("max_active_rate_limiter_index", 6, 0,
				 0xFFFFFFFF),
	ADD_CON_DBGFS_FILE_ENTRY("dropped_cnps_busy_fw", 7, 0, 0xFFFFFFFF),
	ADD_CON_DBGFS_FILE_ENTRY("current_total_limiters_rate", 8, 0,
				 0xFFFFFFFF),
	ADD_CON_DBGFS_FILE_ENTRY("cnps_handled", 9, 0, 0xFFFFFFFF),
	ADD_CON_DBGFS_FILE_ENTRY("min_total_limiters_rate", 10, 0, 0xFFFFFFFF),
	ADD_CON_DBGFS_FILE_ENTRY("max_total_limiters_rate", 11, 0, 0xFFFFFFFF),
};

#define N_STATISTICS_ARROCE_RP (sizeof(statistics_ar_roce_rp) / \
				sizeof(statistics_ar_roce_rp[0]))

static const struct congestion_dbgfs_param_entry statistics_ar_roce_np[] = {
	ADD_CON_DBGFS_FILE_ENTRY("ignored_ecn", 2, 0, 0xFFFFFFFFFFFFFFFF),
	ADD_CON_DBGFS_FILE_ENTRY("allocated_injector", 4, 0, 0xFFFFFFFF),
	ADD_CON_DBGFS_FILE_ENTRY("ecn_disables_due_to_low_buffer", 5, 0,
				 0xFFFFFFFF),
	ADD_CON_DBGFS_FILE_ENTRY
		("total_time_micro_seconds_ecn_disabled_due_to_low_buffer",
		 6, 0, 0xFFFFFFFFFFFFFFFF),
	ADD_CON_DBGFS_FILE_ENTRY("max_active_cnp_injector_index", 8, 0,
				 0xFFFFFFFF),
	ADD_CON_DBGFS_FILE_ENTRY("ecn_marked_packets_handled_successfully",
				 9, 0, 0xFFFFFFFF),
	ADD_CON_DBGFS_FILE_ENTRY("cnps_sent", 10, 0, 0xFFFFFFFF),
};

#define N_STATISTICS_ARROCE_NP (sizeof(statistics_ar_roce_np) / \
				sizeof(statistics_ar_roce_np[0]))

static const struct congestion_dbgfs_param_entry gen_params_ar_roce_np[] = {
	ADD_CON_DBGFS_PARAM_ENTRY("ecn_expect_ipv6", 3, 29, 1 << 29, 0, 29),
	ADD_CON_DBGFS_PARAM_ENTRY("ecn_expect_vlan_tagged", 3, 30, 1 << 30,
				  0, 30),
	ADD_CON_DBGFS_PARAM_ENTRY("ecn_catch_rate_limit_en", 3, 31, 1 << 31,
				  0, 31),
	ADD_CON_DBGFS_PARAM_ENTRY("max_time_between_ecn_catches", 4, 0,
				  0xFFFFFFFF, 1, 31),
	ADD_CON_DBGFS_PARAM_ENTRY("min_lossless_buffer_for_ecn_catches",
				  5, 0, 0xFFFFFFFF, 1, 30),
	ADD_CON_DBGFS_PARAM_ENTRY("min_lossy_buffer_for_ecn_catches", 7,
				  0, 0xFFFFFFFF, 1, 29),
};

#define N_GEN_PARAMS_ARROCE_NP (sizeof(gen_params_ar_roce_np) / \
				sizeof(gen_params_ar_roce_np[0]))

static const struct congestion_algo_desc {
	const char				  *name;
	const struct congestion_dbgfs_param_entry *params;
	u32					  nparams;
	const struct congestion_dbgfs_param_entry *statistics;
	u32					  nstatistics;
	const struct congestion_dbgfs_param_entry *gen_params;
	u32				          ngen_params;
	u8					  list_index;
} congestion_dbgfs_algo[] = {
	[CTRL_ALGO_R_ROCE_ECN_1_REACTION_POINT] = {
	 .name = "r_roce_ecn_rp", .params = params_ar_roce_rp,
	 .nparams = N_PARAMS_ARROCE_RP,
	 .statistics = statistics_ar_roce_rp,
	 .nstatistics = N_STATISTICS_ARROCE_RP,
	 .list_index = LIST_INDEX_ECN },
	[CTRL_ALGO_R_ROCE_ECN_1_NOTIFICATION_POINT] = {
	 .name = "r_roce_ecn_np", .params = params_ar_roce_np,
	 .nparams = N_PARAMS_ARROCE_NP,
	 .statistics = statistics_ar_roce_np,
	 .nstatistics = N_STATISTICS_ARROCE_NP,
	 .gen_params = gen_params_ar_roce_np,
	 .ngen_params = N_GEN_PARAMS_ARROCE_NP,
	 .list_index = LIST_INDEX_ECN },
};

static struct congestion_enable {
	u32 list_on;
	u32 list_off;
} congestion_algo_lists[] = {
	{.list_on = 0,
	 .list_off = 0xFFFFFFFF},
	{.list_on = 1 << LIST_INDEX_ECN,
	 .list_off = 0 },
};

#define DATA_READ(p, pdesc)					\
		({						\
			u64 data_read;				\
			if (pdesc->mask > (u64)0xFFFFFFFF)	\
				data_read = be64_to_cpu(*p);	\
			else					\
				data_read = be32_to_cpu(*p);	\
			data_read = data_read & pdesc->mask;	\
			data_read >> pdesc->bit_offset;		\
		})

#define DATA_WRITE(val, p, p_en, pdesc)					      \
		{							      \
			u64 data_write;					      \
			data_write = (val << pdesc->bit_offset) & pdesc->mask;\
			if ((u64)pdesc->mask > (u64)0xFFFFFFFF) {	      \
				*(__be64 *)p = cpu_to_be64(~pdesc->mask);     \
				*(__be64 *)p |= cpu_to_be64(data_write);      \
			} else {					      \
				*(__be32 *)p = cpu_to_be32(~pdesc->mask);     \
				*(__be32 *)p |= cpu_to_be32(data_write);      \
			}						      \
			*p_en = cpu_to_be32(1 << pdesc->en_bit_offset);       \
		}

static int mlx4_fops_read_params(void *data, u64 *val)
{
	struct congestion_dbgfs_entry *e = data;
	union congestion_control_params_mailbox *out_mb =
		(union congestion_control_params_mailbox *)
		 kmalloc(sizeof(*out_mb), GFP_KERNEL);
	u32 *p = ((u32 *)out_mb) + e->desc->byte_offset;
	*val = VAL_ERROR_VALUE;

	if (!out_mb)
		return -1;

	if (mlx4_congestion_control_get_params(e->dev, e->port, 1 << e->prio,
					       e->algo, 0, out_mb)) {
		pr_warn("q/ecn: failed to read [q/e]cn params");
		kfree(out_mb);
		return -1;
	}
	*val = DATA_READ(p, e->desc);
	kfree(out_mb);
	return 0;
}

static int mlx4_fops_write_params(void *data, u64 val)
{
	struct congestion_dbgfs_entry *e = data;
	union congestion_control_params_mailbox *mb =
		(union congestion_control_params_mailbox *)
		 kzalloc(sizeof(*mb), GFP_KERNEL);
	__be32 *p = ((__be32 *)mb) + e->desc->byte_offset;
	__be32 *p_en = (__be32 *)mb + e->desc->en_byte_offset;
	int err = 0;

	if (!mb)
		return -1;

	DATA_WRITE(val, p, p_en, e->desc);

	if (mlx4_congestion_control_set_params(e->dev, e->port, 1 << e->prio,
					       e->algo, 0, mb)) {
		pr_warn("q/ecn: failed to write [q/e]cn params");
		err = -1;
	}

	kfree(mb);
	return err;
}

static int mlx4_write_enable_list(u32 val, struct list_head *lists,
				  u32 list_bits)
{
	struct congestion_dbgfs_entry *e_enable = NULL;
	union congestion_control_params_mailbox *mb;
	int err = 0;
	__be32 *p;
	__be32 *p_en;
	int i;

	mb = (union congestion_control_params_mailbox *)
		kmalloc(sizeof(*mb), GFP_KERNEL);
	if (!mb)
		return -1;
	for (i = 0;
	     i < min((int)(sizeof(list_bits) * 8), N_LIST_INDEX) &&
		(!err); i++) {
		if (list_bits & (1 << i)) {
			list_for_each_entry(e_enable, &lists[i], list_enable) {
				memset(mb, 0, sizeof(*mb));
				p = ((__be32 *)mb) +
					e_enable->desc->byte_offset;
				p_en = (__be32 *)mb +
					e_enable->desc->en_byte_offset;

				DATA_WRITE(val, p, p_en, e_enable->desc);

				if (mlx4_congestion_control_set_params(
					e_enable->dev, e_enable->port,
					1 << e_enable->prio, e_enable->algo,
					0, mb)) {
					pr_warn("q/ecn: failed to write [q/e]cn params");
					err = -1;
					break;
				}
				e_enable->dev->cong.ecn[e_enable->port]
					[e_enable->algo][e_enable->prio] = val;
			}
		}
	}
	kfree(mb);
	return err;
}

static int mlx4_fops_write_enable(void *data, u64 val)
{
	struct congestion_dbgfs_entry *e = data;
	struct list_head *lists = e->dev->cong.ecn_list[e->port][e->prio];
	struct congestion_enable *cong_enable;
	int err;

	if (val >= sizeof(congestion_algo_lists) /
	    sizeof(congestion_algo_lists[0]))
		return 0;

	cong_enable = &congestion_algo_lists[val];

	err = mlx4_write_enable_list(1, lists, cong_enable->list_on);
	if (!err)
		err = mlx4_write_enable_list(0, lists, cong_enable->list_off);

	return err;
}

static int mlx4_fops_read_enable(void *data, u64 *val)
{
	struct congestion_dbgfs_entry *e = data;
	struct list_head *lists = e->dev->cong.ecn_list[e->port][e->prio];
	union congestion_control_params_mailbox *out_mb;
	struct congestion_dbgfs_entry *e_enable = NULL;
	u32 *p;
	int err = 0;
	*val = VAL_ERROR_VALUE;

	out_mb = (union congestion_control_params_mailbox *)
		kmalloc(sizeof(*out_mb), GFP_KERNEL);
	if (!out_mb)
		return -1;

	list_for_each_entry(e_enable,
			    &lists[congestion_dbgfs_algo[e->algo].list_index],
			    list_enable) {
		p = ((u32 *)out_mb) + e_enable->desc->byte_offset;

		if (mlx4_congestion_control_get_params(e_enable->dev,
						       e_enable->port,
						       1 << e_enable->prio,
						       e_enable->algo,
						       0, out_mb)) {
			pr_warn("q/ecn: failed to read [q/e]cn params");
			err = -1;
			*val = VAL_ERROR_VALUE;
			break;
		}
		*val = DATA_READ(p, e_enable->desc);
		if (!(*val))
			break;
	}
	kfree(out_mb);
	return err;
}

static int mlx4_fops_read_statistics(void *data, u64 *val)
{
	struct congestion_dbgfs_entry *e = data;
	union congestion_control_statistics_mailbox *out_mb =
		(union congestion_control_statistics_mailbox *)
		 kmalloc(sizeof(*out_mb), GFP_KERNEL);
	u32 *p = (u32 *)out_mb +
		   e->prio *
		   sizeof(((union congestion_control_statistics_mailbox *)0)
		   ->s.prio[0]) / sizeof(u32)
		   + e->desc->byte_offset;
	*val = VAL_ERROR_VALUE;

	if (!out_mb)
		return -1;

	if (mlx4_congestion_control_get_statistics(e->dev, e->port, e->algo,
						   0, out_mb)) {
		pr_warn("q/ecn: failed to read statistics");
		kfree(out_mb);
		return -1;
	}
	*val = DATA_READ(p, e->desc);
	kfree(out_mb);
	return 0;
}

static int mlx4_fops_read_gen_params(void *data, u64 *val)
{
	struct congestion_dbgfs_entry *e = data;
	union congestion_control_gen_params_mailbox *out_mb =
		(union congestion_control_gen_params_mailbox *)
		 kzalloc(sizeof(*out_mb), GFP_KERNEL);
	u32 *p = ((u32 *)out_mb) + e->desc->byte_offset;
	*val = VAL_ERROR_VALUE;

	if (!out_mb)
		return -1;

	if (mlx4_congestion_control_get_gen_params(e->dev, e->algo, out_mb)) {
		pr_warn("q/ecn: failed to read gen params");
		kfree(out_mb);
		return -1;
	}
	*val = DATA_READ(p, e->desc);
	kfree(out_mb);
	return 0;
}

static int mlx4_fops_write_gen_params(void *data, u64 val)
{
	struct congestion_dbgfs_entry *e = data;
	union congestion_control_gen_params_mailbox *mb =
		(union congestion_control_gen_params_mailbox *)
		 kzalloc(sizeof(*mb), GFP_KERNEL);
	__be32 *p = ((__be32 *)mb) + e->desc->byte_offset;
	__be32 *p_en = ((__be32 *)mb) + e->desc->en_byte_offset;
	int err = 0;

	if (!mb)
		return -1;

	DATA_WRITE(val, p, p_en, e->desc);

	if (mlx4_congestion_control_set_gen_params(e->dev, e->algo, mb)) {
		pr_warn("q/ecn: failed to set general params");
		err = -1;
	}

	kfree(mb);
	return err;
}

static int mlx4_fops_write_clear(void *data, u64 val)
{
	struct congestion_dbgfs_entry *e = data;
	union congestion_control_in_mailbox *mailbox_in =
		kzalloc(sizeof(*mailbox_in), GFP_KERNEL);
	int err = 0;

	if (!mailbox_in)
		return -1;

	/* clear all */
	if (mlx4_congestion_control(e->dev,
				    (enum congestion_control_opmod)e->desc,
				    e->port, 1 << (e->prio & 7), e->algo,
				    val != 0, mailbox_in, NULL, 0,
				    sizeof(*mailbox_in))) {
		pr_warn("q/ecn: failed to clear");
		err = -1;
	}
	kfree(mailbox_in);
	return err;
}

DEFINE_SIMPLE_ATTRIBUTE(mlx4_fops_params,
			mlx4_fops_read_params, mlx4_fops_write_params,
			"%llu\n");
DEFINE_SIMPLE_ATTRIBUTE(mlx4_fops_params_enable,
			mlx4_fops_read_enable, mlx4_fops_write_enable,
			"%llu\n");
DEFINE_SIMPLE_ATTRIBUTE(mlx4_fops_statistics, mlx4_fops_read_statistics,
			NULL, "%llu\n");
DEFINE_SIMPLE_ATTRIBUTE(mlx4_fops_gen_params,
			mlx4_fops_read_gen_params, mlx4_fops_write_gen_params,
			"%llu\n");
DEFINE_SIMPLE_ATTRIBUTE(mlx4_fops_clear, NULL, mlx4_fops_write_clear,
			"%llu\n");

#define ALGO_DESC_NPARAMS(algo)	(congestion_dbgfs_algo[algo].nparams)
#define ALGO_DESC_NSTATISTICS(algo) (congestion_dbgfs_algo[algo].nstatistics)
#define ALGO_DESC_NGEN_PARAMS(algo) (congestion_dbgfs_algo[algo].ngen_params)
#define ALGO_DESC_PARAM(mem, algo, port, prio, param) \
	(mem + (port) * CONGESTION_NPRIOS * ALGO_DESC_NPARAMS(algo) +\
	 (prio) * ALGO_DESC_NPARAMS(algo) + param)
#define ALGO_DESC_PARAM_SZ(mem, algo, ports) \
	ALGO_DESC_PARAM(mem, algo, ports, 0, 0)
#define ALGO_DESC_STATISTICS(mem, algo, port, prio, statistic, ports) \
	(ALGO_DESC_PARAM_SZ(mem, algo, ports) +\
	 (port) * CONGESTION_NPRIOS * ALGO_DESC_NSTATISTICS(algo) +\
	 (prio) * ALGO_DESC_NSTATISTICS(algo) + statistic)
#define ALGO_DESC_STATISTICS_SZ(mem, algo, ports) \
	ALGO_DESC_STATISTICS(mem, algo, ports, 0, 0, ports)
#define ALGO_DESC_GEN_PARAMS(mem, algo, gen_param, ports) \
	(ALGO_DESC_STATISTICS_SZ(mem, algo, ports) + gen_param)
#define ALGO_DESC_GEN_PARAMS_SZ(mem, algo, ports) \
	ALGO_DESC_GEN_PARAMS(mem, algo, ALGO_DESC_NGEN_PARAMS(algo), ports)
#define ALGO_DESC_CLEAR_GEN_PARAMS(mem, algo, ports) \
	(ALGO_DESC_GEN_PARAMS_SZ(mem, algo, ports))
#define CLEAR_GEN_PARAM 1
#define ENABLE_PER_PRIO 1
#define CLEAR_PER_PRIO 2
#define ALGO_DESC_CLEAR(mem, algo, port, prio, clear_i, ports) \
	(ALGO_DESC_GEN_PARAMS_SZ(mem, algo, ports) + CLEAR_GEN_PARAM + \
	 (port) * CONGESTION_NPRIOS * \
	 CLEAR_PER_PRIO + (prio) * CLEAR_PER_PRIO + (clear_i))
#define ALGO_DESC_CLEAR_PARAM(mem, algo, port, prio, ports) \
	ALGO_DESC_CLEAR(mem, algo, port, prio, 0, ports)
#define ALGO_DESC_CLEAR_STATISTICS(mem, algo, port, prio, ports) \
	ALGO_DESC_CLEAR(mem, algo, port, prio, 1, ports)
#define ALGO_DESC_CLEAR_SZ(mem, algo, ports) \
	ALGO_DESC_CLEAR(mem, algo, ports, 0, 0, ports)
#define ALGO_DESC_ENABLE(mem, algo, port, prio, ports) \
	(ALGO_DESC_CLEAR_SZ(mem, algo, ports) + \
	 (port) * CONGESTION_NPRIOS * \
	 ENABLE_PER_PRIO + (prio) * ENABLE_PER_PRIO)
#define SET_CONG_ENTRY(mem, dev_, port_, prio_, algo_, desc_) \
	{mem->dev = dev_; mem->port = port_; \
	 mem->prio = prio_; mem->algo = algo_; mem->desc = desc_; }
void *con_ctrl_dbgfs_add_algo
		(struct dentry *parent, struct mlx4_ib_dev *dev,
		 enum congestion_control_algorithm algo) {
	int i, j, k = 0;
	struct congestion_dbgfs_entry *algo_entries;
	struct dentry *algo_dir;
	struct dentry *ports_dir;
	struct dentry *gen_params_dir;
	void *algo_alloced;
	size_t algo_alloced_size;
	size_t param_size;
	size_t stats_size;
	size_t clear_enable_size;

	const struct congestion_algo_desc *algo_desc =
		&congestion_dbgfs_algo[algo];

	param_size = algo_desc->nparams * CONGESTION_NPRIOS;
	stats_size = algo_desc->nstatistics * CONGESTION_NPRIOS;
	clear_enable_size = CLEAR_PER_PRIO + ENABLE_PER_PRIO;
	clear_enable_size *= CONGESTION_NPRIOS;
	algo_alloced_size = param_size + stats_size + clear_enable_size;
	algo_alloced_size *= dev->num_ports;
	algo_alloced_size += algo_desc->ngen_params + CLEAR_GEN_PARAM;
	algo_alloced_size *= sizeof(*algo_entries);
	algo_alloced_size += sizeof(struct list_head);
	algo_alloced = kmalloc(algo_alloced_size, GFP_KERNEL);

	if (!algo_alloced)
		return NULL;

	algo_entries = (struct congestion_dbgfs_entry *)
			(((char *)algo_alloced) + sizeof(struct list_head));

	algo_dir = debugfs_create_dir(algo_desc->name, parent);

	if (algo_desc->gen_params) {
		struct congestion_dbgfs_entry *e =
			ALGO_DESC_CLEAR_GEN_PARAMS(algo_entries, algo,
						   dev->num_ports);

		/* add gen params */
		gen_params_dir = debugfs_create_dir("gen_params", algo_dir);

		SET_CONG_ENTRY(e, dev, 0, 0, algo,
			       (void *)CONGESTION_CONTROL_SET_GEN_PARAMS);

		debugfs_create_file("clear", 0222, gen_params_dir, e,
				    &mlx4_fops_clear);

		for (k = 0; k < ALGO_DESC_NGEN_PARAMS(algo); k++) {
			const struct congestion_dbgfs_param_entry *p =
				&algo_desc->gen_params[k];
			struct congestion_dbgfs_entry *e =
				ALGO_DESC_GEN_PARAMS(algo_entries,
						     algo, k,
						     dev->num_ports);
			SET_CONG_ENTRY(e, dev,
				       0, 0, algo, p);

			debugfs_create_file(p->name, 0666, gen_params_dir, e,
					    &mlx4_fops_gen_params);
		}
	}

	ports_dir = debugfs_create_dir("ports", algo_dir);

	for (i = 0; i < dev->num_ports; i++) {
		struct dentry *port;
		struct dentry *params;
		struct dentry *statistics;
		struct dentry *prios;
		char element_name[5];

		snprintf(element_name, sizeof(element_name), "%d", i + 1);
		port = debugfs_create_dir(element_name, ports_dir);
		params = debugfs_create_dir("params", port);
		statistics = debugfs_create_dir("statistics", port);

		/* add params */
		prios = debugfs_create_dir("prios", params);
		for (j = 0; j < CONGESTION_NPRIOS; j++) {
			struct dentry *prio;
			char element_name[5];
			struct congestion_dbgfs_entry *e =
				ALGO_DESC_CLEAR_PARAM(algo_entries,
						      algo, i, j,
						      dev->num_ports);
			SET_CONG_ENTRY(e, dev, i, j, algo,
				       (void *)CONGESTION_CONTROL_SET_PARAMS);

			snprintf(element_name, sizeof(element_name), "%d", j);
			prio = debugfs_create_dir(element_name, prios);

			debugfs_create_file("clear", 0222, prio, e,
					    &mlx4_fops_clear);

			for (k = 0; k < ALGO_DESC_NPARAMS(algo); k++) {
				const struct congestion_dbgfs_param_entry *p =
					&algo_desc->params[k];
				struct congestion_dbgfs_entry *e =
					ALGO_DESC_PARAM(algo_entries,
							algo, i, j, k);
				SET_CONG_ENTRY(e, dev,
					       i, j, algo, p);
				if (!p->on)
					debugfs_create_file(p->name, 0666,
							    prio, e,
							    &mlx4_fops_params);
				else {
					INIT_LIST_HEAD(&e->list_enable);
					list_add(&e->list_enable,
						 &dev->cong.ecn_list[i][j]
						[algo_desc->list_index]);
				}
			}
			e = ALGO_DESC_ENABLE(algo_entries, algo, i, j,
					     dev->num_ports);
			SET_CONG_ENTRY(e, dev,
				       i, j, algo, NULL);

			debugfs_create_file("enable", 0666, prio, e,
					    &mlx4_fops_params_enable);
		}

		/* add statistics */
		prios = debugfs_create_dir("prios", statistics);
		for (j = 0; j < CONGESTION_NPRIOS; j++) {
			struct dentry *prio;
			char element_name[5];
			struct congestion_dbgfs_entry *e =
				ALGO_DESC_CLEAR_STATISTICS(algo_entries,
							   algo, i, j,
							   dev->num_ports);

			SET_CONG_ENTRY(e, dev, i, j, algo,
				       (void *)
					CONGESTION_CONTROL_GET_STATISTICS);

			snprintf(element_name, sizeof(element_name), "%d", j);
			prio = debugfs_create_dir(element_name, prios);

			debugfs_create_file("clear", 0222, prio, e,
					    &mlx4_fops_clear);

			for (k = 0; k < ALGO_DESC_NSTATISTICS(algo); k++) {
				const struct congestion_dbgfs_param_entry *p =
					&algo_desc->statistics[k];
				struct congestion_dbgfs_entry *e =
					ALGO_DESC_STATISTICS(algo_entries,
							     algo, i, j, k,
							     dev->num_ports);
				SET_CONG_ENTRY(e, dev,
					       i, j, algo, p);

				debugfs_create_file(p->name, 0444, prio, e,
						    &mlx4_fops_statistics);
			}
		}
	}
	return algo_alloced;
}

int con_ctrl_dbgfs_init(struct dentry *parent,
			struct mlx4_ib_dev *dev)
{
	int i, j, k;

	dev->cong.ecn_list = kmalloc(sizeof(*dev->cong.ecn_list) *
				dev->num_ports, GFP_KERNEL);
	if (!dev->cong.ecn_list)
		return -1;
	for (i = 0; i < dev->num_ports; i++)
		for (j = 0; j < CONGESTION_NPRIOS; j++)
			for (k = 0; k < N_LIST_INDEX; k++)
				INIT_LIST_HEAD(
					&dev->cong.ecn_list[i][j][k]);
	return 0;
}

void con_ctrl_dbgfs_free(struct mlx4_ib_dev *dev)
{
	kfree(dev->cong.ecn_list);
	dev->cong.ecn_list = NULL;
}

int ecn_enabled(struct mlx4_ib_dev *dev, u8 port, u8 prio)
{
	int i;

	for (i = 1; i < CTRL_ALGO_SZ; i++) {
		if (dev->cong.ecn[port - 1][i][prio])
			return 1;
	}
	return 0;
}
