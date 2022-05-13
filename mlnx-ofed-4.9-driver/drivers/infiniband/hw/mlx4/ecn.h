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

#ifndef MLX4_ECN_H
#define MLX4_ECN_H

#define CONGESTION_NPRIOS 8
#define MLX4_CMD_CONGESTION_CTRL_OPCODE 0x68
#define N_LIST_INDEX            2
#define VAL_ERROR_VALUE		-1

#include <linux/debugfs.h>

struct mlx4_ib_dev;

enum congestion_control_list_index {
	LIST_INDEX_QCN,
	LIST_INDEX_ECN
};

enum congestion_control_opmod {
	CONGESTION_CONTROL_GET_PARAMS,
	CONGESTION_CONTROL_GET_STATISTICS,
	CONGESTION_CONTROL_GET_GEN_PARAMS,
	CONGESTION_CONTROL_GET_FLOW_STATE,
	CONGESTION_CONTROL_SET_PARAMS,
	CONGESTION_CONTROL_SET_GEN_PARAMS,
	CONGESTION_CONTROL_SET_FLOW_STATE,
	CONGESTION_CONTROL_SZ
};

enum congestion_control_algorithm {
	CTRL_ALGO_802_1_QAU_REACTION_POINT,
	CTRL_ALGO_R_ROCE_ECN_1_REACTION_POINT,
	CTRL_ALGO_R_ROCE_ECN_1_NOTIFICATION_POINT,
	CTRL_ALGO_SZ
};

struct congestion_control_inmod {
	char clear;
	char algorithm;
	char priority;
	char port;
} __packed;

enum congestion_control_r_roce_ecn_rp_modify_enable {
	CONG_CTRL_RROCE_RP_MODIFY_ENABLE_DCE_TCP_RTT = 12,
	CONG_CTRL_RROCE_RP_MODIFY_ENABLE_DCE_TCP_G,
	CONG_CTRL_RROCE_RP_MODIFY_ENABLE_RATE_TO_SET_ON_FIRST_CNP,
	CONG_CTRL_RROCE_RP_MODIFY_ENABLE_MARKED_RATIO_SHIFT,
	CONG_CTRL_RROCE_RP_MODIFY_ENABLE_MARKED_RATIO_MULTIPLIER,
	CONG_CTRL_RROCE_RP_MODIFY_ENABLE_ALPHA_TO_RATE_COEFF,
	CONG_CTRL_RROCE_RP_MODIFY_ENABLE_MAX_BYTE_RISE = 20,
	CONG_CTRL_RROCE_RP_MODIFY_ENABLE_MAX_TIME_RISE,
	CONG_CTRL_RROCE_RP_MODIFY_ENABLE_RPG_MIN_RATE,
	CONG_CTRL_RROCE_RP_MODIFY_ENABLE_RPG_MIN_DEC_FAC,
	CONG_CTRL_RROCE_RP_MODIFY_ENABLE_ALPHA_TO_RATE_SHIFT,
	CONG_CTRL_RROCE_RP_MODIFY_ENABLE_RPG_HAI_RATE,
	CONG_CTRL_RROCE_RP_MODIFY_ENABLE_RPG_AI_RATE,
	CONG_CTRL_RROCE_RP_MODIFY_ENABLE_RPG_MAX_RATE,
	CONG_CTRL_RROCE_RP_MODIFY_ENABLE_RPG_THRESHOLD,
	CONG_CTRL_RROCE_RP_MODIFY_ENABLE_RPG_BYTE_RESET,
	CONG_CTRL_RROCE_RP_MODIFY_ENABLE_RPG_TIME_RESET,
	CONG_CTRL_RROCE_RP_MODIFY_ENABLE_DISABLE_HAI_STAGE = 49,
	CONG_CTRL_RROCE_RP_MODIFY_ENABLE_CLAMP_TGT_RATE_AFTER_TIME_INC = 55,
	CONG_CTRL_RROCE_RP_MODIFY_ENABLE_FORCE_RC_TOS,
	CONG_CTRL_RROCE_RP_MODIFY_ENABLE_FORCE_UC_TOS,
	CONG_CTRL_RROCE_RP_MODIFY_ENABLE_FORCE_UD_TOS,
	CONG_CTRL_RROCE_RP_MODIFY_ENABLE_CLAMP_TGT_RATE = 59,
	CONG_CTRL_RROCE_RP_MODIFY_ENABLE_FAST_RISE = 61,
	CONG_CTRL_RROCE_RP_MODIFY_ENABLE_CNP_RECEIVE_ENABLE = 63,
};

enum congestion_control_r_roce_ecn_np_modify_enable {
	CONG_CTRL_RROCE_NP_MODIFY_ENABLE_CNP_DSCP = 26,
	CONG_CTRL_RROCE_NP_MODIFY_ENABLE_CNP_802P_PRIO,
	CONG_CTRL_RROCE_NP_MODIFY_ENABLE_NUM_CONGESTION_CYCLES_TO_KEEP,
	CONG_CTRL_RROCE_NP_MODIFY_ENABLE_CNP_TIMER,
	CONG_CTRL_RROCE_NP_MODIFY_ENABLE_NUM_INJECTOR,
	CONG_CTRL_RROCE_NP_MODIFY_ENABLE_COMPN_ECN_RATE_LIMIT = 62,
	CONG_CTRL_RROCE_NP_MODIFY_ENABLE_ECN_RECEIVE_ENABLE,
};

enum congestion_control_r_roce_ecn_np_gen_params_modify_enable {
	CONG_CTRL_RROCE_NP_GEN_PARAMS_MODIFY_ENABLE_CNP_OPCODE = 28,
	CONG_CTRL_RROCE_NP_GEN_PARAMS_MODIFY_ENABLE_MIN_LOSSY_BUF_CATCHES,
	CONG_CTRL_RROCE_NP_GEN_PARAMS_MODIFY_ENABLE_MIN_LOSSLESS_BUF_CATCHES,
	CONG_CTRL_RROCE_NP_GEN_PARAMS_MODIFY_ENABLE_MAX_TIME_BETWEEN_CATCHES,
	CONG_CTRL_RROCE_NP_GEN_PARAMS_MODIFY_ENABLE_ECN_EXPECT_IPV6 = 61,
	CONG_CTRL_RROCE_NP_GEN_PARAMS_MODIFY_ENABLE_ECN_EXPECT_VLAN_TAGGED,
	CONG_CTRL_RROCE_NP_GEN_PARAMS_MODIFY_ENABLE_ECN_CATCH_RATE_LIMIT_EN,
};

enum congestion_control_r_roce_ecn_rp_gen_params_modify_enable {
	CONG_CTRL_RROCE_RP_GEN_PARAMS_MODIFY_ENABLE_CNP_OPCODE = 28,
};

enum congestion_control_r_roce_rp_extended_enable {
	CONG_CTRL_R_ROCE_RP_EXTENDED_ENABLE_DISABLE_HAI_STAGE = 17,
	CONG_CTRL_R_ROCE_RP_EXTENDED_ENABLE_CLAMP_TGT_RATE_AFTER_TIME_INC = 23,
	CONG_CTRL_R_ROCE_RP_EXTENDED_ENABLE_FORCE_RC_TOS,
	CONG_CTRL_R_ROCE_RP_EXTENDED_ENABLE_FORCE_UC_TOS,
	CONG_CTRL_R_ROCE_RP_EXTENDED_ENABLE_FORCE_UD_TOS,
	CONG_CTRL_R_ROCE_RP_EXTENDED_ENABLE_CLAMP_TGT_RATE,
	CONG_CTRL_R_ROCE_RP_EXTENDED_ENABLE_FAST_RISE = 29,
	CONG_CTRL_R_ROCE_RP_EXTENDED_ENABLE_CNP_RECEIVE_ENABLE = 31
};

enum congestion_control_r_roce_np_extended_enable {
	CONG_CTRL_R_ROCE_NP_EXTENDED_ENABLE_COMP_ECN_RATE_LIMIT = 30,
	CONG_CTRL_R_ROCE_NP_EXTENDED_ENABLE_ECN_RECEIVE_ENABLE,
};

enum congestion_control_r_roce_np_gen_params_extended_enable {
	CONG_CTRL_R_ROCE_NP_GEN_PARAMS_EXTENDED_ENABLE_ECN_EXPECT_IPV6 = 29,
	CONG_CTRL_R_ROCE_NP_GEN_PARAMS_EXTENDED_ENABLE_ECN_EXPECT_VLAN_TAGGED,
	CONG_CTRL_R_ROCE_NP_GEN_PARAMS_EXTENDED_ENABLE_ECN_CATCH_RATE_LIMIT_EN,
};

struct congestion_control_mb_prio_r_roce_ecn_rp_params {
	__be64 modify_enable;
	__be32 reserved1;
	__be32 extended_enable;
	__be32 reserved2;
	__be32 rpg_time_reset;
	__be32 rpg_byte_reset;
	__be32 rpg_threshold;
	__be32 rpg_max_rate;
	__be32 rpg_ai_rate;
	__be32 rpg_hai_rate;
	__be32 alpha_to_rate_shift;
	__be32 rpg_min_dec_fac;
	__be32 rpg_min_rate;
	__be32 max_time_rise;
	__be32 max_byte_rise;
	__be32 reserved3[2];
	__be32 alpha_to_rate_coeff;
	__be32 marked_ratio_multiplier;
	__be32 marked_ratio_shift;
	__be32 rate_to_set_on_first_cnp;
	__be32 dce_tcp_g;
	__be32 dce_tcp_rtt;
	__be32 reserved4[42];
} __packed;

struct congestion_control_mb_prio_r_roce_ecn_np_params {
	__be64 modify_enable;
	__be32 reserved1;
	__be32 extended_enable;
	__be32 num_injector;
	__be32 cnp_timer;
	__be32 cnp_dscp_cnp_802p_prio;
	__be32 num_congestion_cycle_to_keep;
	__be32 reserved2[56];
} __packed;

struct congestion_control_r_roce_rp_prio_statistics {
	__be64 rppp_rp_centiseconds;
	__be32 reserved1;
	__be32 ignored_cnp;
	__be32 allocated_rate_limiter;
	__be32 estimated_average_total_limiters_rate;
	__be32 max_active_rate_limiter_index;
	__be32 dropped_cnps_busy_fw;
	__be32 current_total_limiters_rate;
	__be32 cnps_handled;
	__be32 min_total_limiters_rate;
	__be32 max_total_limiters_rate;
	__be32 reserved2[4];
} __packed;

struct congestion_control_r_roce_rp_statistics {
	struct congestion_control_r_roce_rp_prio_statistics
		prio[CONGESTION_NPRIOS];
} __packed;

struct congestion_control_r_roce_np_prio_statistics {
	__be32 reserved1[2];
	__be64 ignored_ecn;
	__be32 allocated_injector;
	__be32 ecn_disables_due_to_low_buffer;
	__be64 total_time_micro_seconds_ecn_disabled_due_to_low_buffer;
	__be32 max_active_cnp_injector_index;
	__be32 ecn_marked_packets_handled_successfully;
	__be32 cnps_sent;
	__be32 reserved2[5];
} __packed;

struct congestion_control_r_roce_np_statistics {
	struct congestion_control_r_roce_np_prio_statistics
		prio[CONGESTION_NPRIOS];
} __packed;

struct congestion_control_mb_r_roce_ecn_np_gen_params {
	__be64 modify_enable;
	__be32 reserved1;
	__be32 extended_enable;
	__be32 max_time_between_ecn_catches;
	__be32 min_lossless_buffer_for_ecn_catches;
	__be32 cnp_opcode;
	__be32 min_lossy_buffer_for_ecn_catches;
	__be32 reserved2[8];
} __packed;

struct congestion_control_statistics {
	struct {
		__be32 prio_space[0x10];
	} prio[CONGESTION_NPRIOS] __packed;
	__be32 gen[0x80];
} __packed;

union congestion_control_statistics_mailbox {
	struct congestion_control_r_roce_rp_statistics s_rroce_rp;
	struct congestion_control_r_roce_np_statistics s_rroce_np;
	struct congestion_control_statistics		    s;
};

union congestion_control_gen_params_mailbox  {
	__be64 modify_enable;
	struct congestion_control_mb_r_roce_ecn_np_gen_params p_rroce_np;
	__be32 mb_size[16];
};

union congestion_control_params_mailbox {
	__be64 modify_enable;
	struct congestion_control_mb_prio_r_roce_ecn_rp_params p_rroce_rp;
	struct congestion_control_mb_prio_r_roce_ecn_np_params p_rroce_np;
};

union congestion_control_out_mailbox {
	union congestion_control_statistics_mailbox statistics_mb;
	union congestion_control_params_mailbox params_mb;
	union congestion_control_gen_params_mailbox gen_params_mb;
};

union congestion_control_in_mailbox {
	union congestion_control_params_mailbox params_mb;
	union congestion_control_gen_params_mailbox gen_params_mb;
};

struct ecn_control {
	u8 ecn[MLX4_MAX_PORTS][CTRL_ALGO_SZ][CONGESTION_NPRIOS];
	struct list_head (*ecn_list)[CONGESTION_NPRIOS][N_LIST_INDEX];
};

void *con_ctrl_dbgfs_add_algo
		(struct dentry *parent, struct mlx4_ib_dev *dev,
		 enum congestion_control_algorithm algo);
int con_ctrl_dbgfs_init
		(struct dentry *parent, struct mlx4_ib_dev *dev);

void con_ctrl_dbgfs_free(struct mlx4_ib_dev *dev);

int ecn_enabled(struct mlx4_ib_dev *dev, u8 port, u8 prio);

#endif
