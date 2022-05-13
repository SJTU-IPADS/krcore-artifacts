// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2020 Mellanox Technologies. */

#include <linux/proc_fs.h>
#include "dr_types.h"

#define PACKAGE_VERSION "1.0.3"
#define BUF_SIZE 512

struct dr_buffer {
	char buf[BUF_SIZE];
	int write_idx;
	int read_idx;
	struct list_head list;
};

struct dr_dump_ctx {
	struct mlx5dr_domain *dmn;
	struct list_head buf_list;
	struct dr_buffer *iter;
};

static int dr_copy_from_dump_buffer(char *dest, struct dr_buffer *src, int len)
{
	int copy_num = min(len, src->write_idx - src->read_idx);

	if (copy_to_user(dest, &src->buf[src->read_idx], copy_num))
		return -EFAULT;

	src->read_idx += copy_num;
	return copy_num;
}

static void dr_buff_memcopy(struct dr_buffer *dest, char *src, int len)
{
	if (!len) {
		dest->buf[dest->write_idx] = 0;
		return;
	}

	memcpy(&dest->buf[dest->write_idx], src, len);
	dest->write_idx += len;
	if (dest->write_idx >= BUF_SIZE - 1)
		dest->buf[dest->write_idx] = 0;
}

static int dr_copy_to_dump_buffer(struct dr_dump_ctx *ctx, char *src)
{
	struct dr_buffer *dest = ctx->iter, *next_buf;
	int src_len = strlen(src);

	int first_free_len = BUF_SIZE - dest->write_idx - 1;

	if (src_len > first_free_len) {
		dr_buff_memcopy(dest, src, first_free_len);
		next_buf = kvzalloc(sizeof(*next_buf), GFP_KERNEL);
		if (!next_buf)
			return -ENOMEM;

		list_add(&next_buf->list, &dest->list);
		dest = next_buf;
		ctx->iter = dest;

		dr_buff_memcopy(dest, &src[first_free_len],
				src_len - first_free_len);
	} else {
		dr_buff_memcopy(dest, src, src_len);
	}
	return 0;
}

enum dr_dump_rec_type {
	DR_DUMP_REC_TYPE_DOMAIN = 3000,
	DR_DUMP_REC_TYPE_DOMAIN_INFO_FLEX_PARSER = 3001,
	DR_DUMP_REC_TYPE_DOMAIN_INFO_DEV_ATTR = 3002,
	DR_DUMP_REC_TYPE_DOMAIN_INFO_VPORT = 3003,
	DR_DUMP_REC_TYPE_DOMAIN_INFO_CAPS = 3004,
	DR_DUMP_REC_TYPE_DOMAIN_SEND_RING = 3005,

	DR_DUMP_REC_TYPE_TABLE = 3100,
	DR_DUMP_REC_TYPE_TABLE_RX = 3101,
	DR_DUMP_REC_TYPE_TABLE_TX = 3102,

	DR_DUMP_REC_TYPE_MATCHER = 3200,
	DR_DUMP_REC_TYPE_MATCHER_MASK = 3201,
	DR_DUMP_REC_TYPE_MATCHER_RX = 3202,
	DR_DUMP_REC_TYPE_MATCHER_TX = 3203,
	DR_DUMP_REC_TYPE_MATCHER_BUILDER = 3204,

	DR_DUMP_REC_TYPE_RULE = 3300,
	DR_DUMP_REC_TYPE_RULE_RX_ENTRY = 3301,
	DR_DUMP_REC_TYPE_RULE_TX_ENTRY = 3302,
	DR_DUMP_REC_TYPE_CX6DX_RULE_RX_ENTRY = 3303,
	DR_DUMP_REC_TYPE_CX6DX_RULE_TX_ENTRY = 3304,

	DR_DUMP_REC_TYPE_ACTION_ENCAP_L2 = 3400,
	DR_DUMP_REC_TYPE_ACTION_ENCAP_L3 = 3401,
	DR_DUMP_REC_TYPE_ACTION_MODIFY_HDR = 3402,
	DR_DUMP_REC_TYPE_ACTION_DROP = 3403,
	DR_DUMP_REC_TYPE_ACTION_QP = 3404,
	DR_DUMP_REC_TYPE_ACTION_FT = 3405,
	DR_DUMP_REC_TYPE_ACTION_CTR = 3406,
	DR_DUMP_REC_TYPE_ACTION_TAG = 3407,
	DR_DUMP_REC_TYPE_ACTION_VPORT = 3408,
	DR_DUMP_REC_TYPE_ACTION_DECAP_L2 = 3409,
	DR_DUMP_REC_TYPE_ACTION_DECAP_L3 = 3410,
	DR_DUMP_REC_TYPE_ACTION_DEVX_TIR = 3411,
	DR_DUMP_REC_TYPE_ACTION_PUSH_VLAN = 3412,
	DR_DUMP_REC_TYPE_ACTION_POP_VLAN = 3413,
};

static u64 dr_dump_icm_to_idx(u64 icm_addr)
{
	return (icm_addr >> 6) & 0xffffffff;
}

static void dr_dump_hex_print(char *dest, u32 dest_size, char *src, u32 src_size)
{
	int i;

	BUG_ON(dest_size < 2 * src_size);

	if (dest_size < 2 * src_size)
		return;

	for (i = 0; i < src_size; i++)
		snprintf(&dest[2 * i], BUF_SIZE, "%02x", (u8)src[i]);
}

static int
dr_dump_rule_action_mem(struct dr_dump_ctx *ctx, const u64 rule_id,
			struct mlx5dr_rule_action_member *action_mem)
{
	struct mlx5dr_action *action = action_mem->action;
	const u64 action_id = (u64)(uintptr_t)action;
	int ret = 0;
	char tmp_buf[BUF_SIZE] = {};

	switch (action->action_type) {
	case DR_ACTION_TYP_DROP:
		ret = snprintf(tmp_buf, BUF_SIZE, "%d,0x%llx,0x%llx\n",
			       DR_DUMP_REC_TYPE_ACTION_DROP, action_id, rule_id);
		break;
	case DR_ACTION_TYP_FT:
		if (action->dest_tbl.is_fw_tbl)
			ret = snprintf(tmp_buf, BUF_SIZE, "%d,0x%llx,0x%llx,0x%x\n",
				       DR_DUMP_REC_TYPE_ACTION_FT, action_id,
				       rule_id, action->dest_tbl.fw_tbl.id);
		else
			ret = snprintf(tmp_buf, BUF_SIZE, "%d,0x%llx,0x%llx,0x%x\n",
				       DR_DUMP_REC_TYPE_ACTION_FT, action_id,
				       rule_id, action->dest_tbl.tbl->table_id);

		break;
	case DR_ACTION_TYP_CTR:
		ret = snprintf(tmp_buf, BUF_SIZE, "%d,0x%llx,0x%llx,0x%x\n",
			       DR_DUMP_REC_TYPE_ACTION_CTR, action_id, rule_id,
			       action->ctr.ctr_id +
			       action->ctr.offeset);
		break;
	case DR_ACTION_TYP_TAG:
		ret = snprintf(tmp_buf, BUF_SIZE, "%d,,0x%llx,0x%llx0x%x\n",
			       DR_DUMP_REC_TYPE_ACTION_TAG, action_id, rule_id,
			       action->flow_tag);
		break;
	case DR_ACTION_TYP_MODIFY_HDR:
		ret = snprintf(tmp_buf, BUF_SIZE, "%d,,0x%llx,0x%llx0x%x\n",
			       DR_DUMP_REC_TYPE_ACTION_MODIFY_HDR, action_id,
			       rule_id, action->rewrite.index);
		break;
	case DR_ACTION_TYP_VPORT:
		ret = snprintf(tmp_buf, BUF_SIZE, "%d,0x%llx,0x%llx,0x%x\n",
			       DR_DUMP_REC_TYPE_ACTION_VPORT, action_id, rule_id,
			       action->vport.caps->num);
		break;
	case DR_ACTION_TYP_TNL_L2_TO_L2:
		ret = snprintf(tmp_buf, BUF_SIZE, "%d,0x%llx,0x%llx\n",
			       DR_DUMP_REC_TYPE_ACTION_DECAP_L2, action_id,
			       rule_id);
		break;
	case DR_ACTION_TYP_TNL_L3_TO_L2:
		ret = snprintf(tmp_buf, BUF_SIZE, "%d,0x%llx,0x%llx,0x%x\n",
			       DR_DUMP_REC_TYPE_ACTION_DECAP_L3, action_id,
			       rule_id, action->rewrite.index);
		break;
	case DR_ACTION_TYP_L2_TO_TNL_L2:
		ret = snprintf(tmp_buf, BUF_SIZE, "%d,0x%llx,0x%llx,0x%x\n",
			       DR_DUMP_REC_TYPE_ACTION_ENCAP_L2, action_id,
			       rule_id, action->reformat.reformat_id);
		break;
	case DR_ACTION_TYP_L2_TO_TNL_L3:
		ret = snprintf(tmp_buf, BUF_SIZE, "%d,0x%llx,0x%llx,0x%x\n",
			       DR_DUMP_REC_TYPE_ACTION_ENCAP_L3, action_id,
			       rule_id, action->reformat.reformat_id);
		break;
	case DR_ACTION_TYP_POP_VLAN:
		ret = snprintf(tmp_buf, BUF_SIZE, "%d,0x%llx,0x%llx\n",
			       DR_DUMP_REC_TYPE_ACTION_POP_VLAN, action_id,
			       rule_id);
		break;
	case DR_ACTION_TYP_PUSH_VLAN:
		ret = snprintf(tmp_buf, BUF_SIZE, "%d,0x%llx,0x%llx,0x%x\n",
			       DR_DUMP_REC_TYPE_ACTION_PUSH_VLAN, action_id,
			       rule_id, action->push_vlan.vlan_hdr);
		break;
	default:
		return 0;
	}

	if (ret < 0)
		return ret;

	ret = dr_copy_to_dump_buffer(ctx, tmp_buf);
	if (ret < 0)
		return ret;

	return 0;
}

static int
dr_dump_rule_mem(struct dr_dump_ctx *ctx, struct mlx5dr_rule_member *rule_mem,
		 bool is_rx, const u64 rule_id)
{
	char hw_ste_dump[BUF_SIZE] = {};
	char tmp_buf[BUF_SIZE] = {};
	enum dr_dump_rec_type mem_rec_type;
	int ret;

	mem_rec_type = is_rx ? DR_DUMP_REC_TYPE_RULE_RX_ENTRY :
			       DR_DUMP_REC_TYPE_RULE_TX_ENTRY;

	dr_dump_hex_print(hw_ste_dump, BUF_SIZE, (char *)rule_mem->ste->hw_ste,
			  DR_STE_SIZE_REDUCED);
	ret = snprintf(tmp_buf, BUF_SIZE, "%d,0x%llx,0x%llx,%s\n",
		       mem_rec_type,
		       dr_dump_icm_to_idx(mlx5dr_ste_get_icm_addr(rule_mem->ste)),
		       rule_id,
		       hw_ste_dump);
	if (ret < 0)
		return ret;

	ret = dr_copy_to_dump_buffer(ctx, tmp_buf);
	if (ret < 0)
		return ret;

	return 0;
}

static int
dr_dump_rule_rx_tx(struct dr_dump_ctx *ctx, struct mlx5dr_rule_rx_tx *rule_rx_tx,
		   bool is_rx, const u64 rule_id)
{
	struct mlx5dr_rule_member *rule_mem;
	int ret;

	list_for_each_entry(rule_mem, &rule_rx_tx->rule_members_list, list) {
		ret = dr_dump_rule_mem(ctx, rule_mem, is_rx, rule_id);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int dr_dump_rule(struct dr_dump_ctx *ctx, struct mlx5dr_rule *rule)
{
	const u64 rule_id = (u64)(uintptr_t)rule;
	struct mlx5dr_rule_action_member *action_mem;
	struct mlx5dr_rule_rx_tx *rx = &rule->rx;
	struct mlx5dr_rule_rx_tx *tx = &rule->tx;
	char tmp_buf[BUF_SIZE] = {};

	int ret;

	ret = snprintf(tmp_buf, BUF_SIZE, "%d,0x%llx,0x%llx\n",
		       DR_DUMP_REC_TYPE_RULE,
		       rule_id,
		       (u64)(uintptr_t)rule->matcher);
	if (ret < 0)
		return ret;

	ret = dr_copy_to_dump_buffer(ctx, tmp_buf);
	if (ret < 0)
		return ret;

	if (rx->nic_matcher) {
		ret = dr_dump_rule_rx_tx(ctx, rx, true, rule_id);
		if (ret < 0)
			return ret;
	}

	if (tx->nic_matcher) {
		ret = dr_dump_rule_rx_tx(ctx, tx, false, rule_id);
		if (ret < 0)
			return ret;
	}


	list_for_each_entry(action_mem, &rule->rule_actions_list, list) {
		ret = dr_dump_rule_action_mem(ctx, rule_id, action_mem);
		if (ret < 0)
			return ret;
	}

	return 0;
}

int mlx5dr_dump_dr_rule(struct dr_dump_ctx *ctx, struct mlx5dr_rule *rule)
{
	int ret;

	if (!ctx || !rule)
		return -EINVAL;

	mutex_lock(&rule->matcher->tbl->dmn->dbg_mutex);

	ret = dr_dump_rule(ctx, rule);

	mutex_unlock(&rule->matcher->tbl->dmn->dbg_mutex);

	return ret;
}

static int
dr_dump_matcher_mask(struct dr_dump_ctx *ctx, struct mlx5dr_match_param *mask,
		     u8 criteria, const u64 matcher_id)
{
	char dump[BUF_SIZE] = {};
	int ret;
	char tmp_buf[BUF_SIZE] = {};

	ret = snprintf(tmp_buf, BUF_SIZE, "%d,0x%llx,",
		       DR_DUMP_REC_TYPE_MATCHER_MASK, matcher_id);
	if (ret < 0)
		return ret;

	ret = dr_copy_to_dump_buffer(ctx, tmp_buf);
	if (ret < 0)
		return ret;

	if (criteria & DR_MATCHER_CRITERIA_OUTER) {
		dr_dump_hex_print(dump, BUF_SIZE, (char *)&mask->outer,
				  sizeof(mask->outer));
		ret = snprintf(tmp_buf, BUF_SIZE, "%s,", dump);
	} else {
		ret = snprintf(tmp_buf, BUF_SIZE, ",");
	}

	if (ret < 0)
		return ret;

	ret = dr_copy_to_dump_buffer(ctx, tmp_buf);
	if (ret < 0)
		return ret;

	if (criteria & DR_MATCHER_CRITERIA_INNER) {
		dr_dump_hex_print(dump, BUF_SIZE, (char *)&mask->inner,
				  sizeof(mask->inner));
		ret = snprintf(tmp_buf, BUF_SIZE, "%s,", dump);
	} else {
		ret = snprintf(tmp_buf, BUF_SIZE, ",");
	}

	if (ret < 0)
		return ret;

	ret = dr_copy_to_dump_buffer(ctx, tmp_buf);
	if (ret < 0)
		return ret;

	if (criteria & DR_MATCHER_CRITERIA_MISC) {
		dr_dump_hex_print(dump, BUF_SIZE, (char *)&mask->misc,
				  sizeof(mask->misc));
		ret = snprintf(tmp_buf, BUF_SIZE, "%s,", dump);
	} else {
		ret = snprintf(tmp_buf, BUF_SIZE, ",");
	}

	if (ret < 0)
		return ret;

	ret = dr_copy_to_dump_buffer(ctx, tmp_buf);
	if (ret < 0)
		return ret;

	if (criteria & DR_MATCHER_CRITERIA_MISC2) {
		dr_dump_hex_print(dump, BUF_SIZE, (char *)&mask->misc2,
				  sizeof(mask->misc2));
		ret = snprintf(tmp_buf, BUF_SIZE, "%s,", dump);
	} else {
		ret = snprintf(tmp_buf, BUF_SIZE, ",");
	}

	if (ret < 0)
		return ret;

	ret = dr_copy_to_dump_buffer(ctx, tmp_buf);
	if (ret < 0)
		return ret;

	if (criteria & DR_MATCHER_CRITERIA_MISC3) {
		dr_dump_hex_print(dump, BUF_SIZE, (char *)&mask->misc3,
				  sizeof(mask->misc3));
		ret = snprintf(tmp_buf, BUF_SIZE, "%s\n", dump);
	} else {
		ret = snprintf(tmp_buf, BUF_SIZE, ",\n");
	}

	if (ret < 0)
		return ret;

	ret = dr_copy_to_dump_buffer(ctx, tmp_buf);
	if (ret < 0)
		return ret;

	return 0;
}

static int
dr_dump_matcher_builder(struct dr_dump_ctx *ctx, struct mlx5dr_ste_build *builder,
			u32 index, bool is_rx, const u64 matcher_id)
{
	int ret;
	char tmp_buf[BUF_SIZE] = {};

	ret = snprintf(tmp_buf, BUF_SIZE, "%d,0x%llx%d,%d,0x%x\n",
		       DR_DUMP_REC_TYPE_MATCHER_BUILDER,
		       matcher_id,
		       index,
		       is_rx,
		       builder->lu_type);
	if (ret < 0)
		return ret;

	ret = dr_copy_to_dump_buffer(ctx, tmp_buf);
	if (ret < 0)
		return ret;

	return 0;
}

static int
dr_dump_matcher_rx_tx(struct dr_dump_ctx *ctx, bool is_rx,
		      struct mlx5dr_matcher_rx_tx *matcher_rx_tx,
		      const u64 matcher_id)
{
	enum dr_dump_rec_type rec_type;
	int i, ret;
	char tmp_buf[BUF_SIZE] = {};

	rec_type = is_rx ? DR_DUMP_REC_TYPE_MATCHER_RX :
			   DR_DUMP_REC_TYPE_MATCHER_TX;

	ret = snprintf(tmp_buf, BUF_SIZE, "%d,0x%llx,0x%llx,%d,0x%llx,0x%llx\n",
		       rec_type,
		       (u64)(uintptr_t)matcher_rx_tx,
		       matcher_id,
		       matcher_rx_tx->num_of_builders,
		       dr_dump_icm_to_idx(matcher_rx_tx->s_htbl->chunk->icm_addr),
		       dr_dump_icm_to_idx(matcher_rx_tx->e_anchor->chunk->icm_addr));
	if (ret < 0)
		return ret;

	ret = dr_copy_to_dump_buffer(ctx, tmp_buf);
	if (ret < 0)
		return ret;

	for (i = 0; i < matcher_rx_tx->num_of_builders; i++) {
		ret = dr_dump_matcher_builder(ctx, &matcher_rx_tx->ste_builder[i],
					      i, is_rx, matcher_id);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int dr_dump_matcher(struct dr_dump_ctx *ctx, struct mlx5dr_matcher *matcher)
{
	struct mlx5dr_matcher_rx_tx *rx = &matcher->rx;
	struct mlx5dr_matcher_rx_tx *tx = &matcher->tx;
	u64 matcher_id;
	int ret;

	char tmp_buf[BUF_SIZE] = {};

	matcher_id = (u64)(uintptr_t)matcher;

	ret = snprintf(tmp_buf, BUF_SIZE, "%d,0x%llx,0x%llx,%d\n",
		       DR_DUMP_REC_TYPE_MATCHER,
		       matcher_id,
		       (u64)(uintptr_t)matcher->tbl,
		       matcher->prio);
	if (ret < 0)
		return ret;

	ret = dr_copy_to_dump_buffer(ctx, tmp_buf);
	if (ret < 0)
		return ret;

	ret = dr_dump_matcher_mask(ctx, &matcher->mask,
				   matcher->match_criteria, matcher_id);
	if (ret < 0)
		return ret;

	if (rx->nic_tbl) {
		ret = dr_dump_matcher_rx_tx(ctx, true, rx, matcher_id);
		if (ret < 0)
			return ret;
	}

	if (tx->nic_tbl) {
		ret = dr_dump_matcher_rx_tx(ctx, false, tx, matcher_id);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int
dr_dump_matcher_all(struct dr_dump_ctx *ctx, struct mlx5dr_matcher *matcher)
{
	struct mlx5dr_rule *rule;
	int ret;

	ret = dr_dump_matcher(ctx, matcher);
	if (ret < 0)
		return ret;

	list_for_each_entry(rule, &matcher->rule_list, rule_list) {
		ret = dr_dump_rule(ctx, rule);
		if (ret < 0)
			return ret;
	}

	return 0;
}

int mlx5dr_dump_dr_matcher(struct dr_dump_ctx *ctx, struct mlx5dr_matcher *matcher)
{
	int ret;

	if (!ctx || !matcher)
		return -EINVAL;

	mutex_lock(&matcher->tbl->dmn->dbg_mutex);

	ret = dr_dump_matcher_all(ctx, matcher);

	mutex_unlock(&matcher->tbl->dmn->dbg_mutex);

	return ret;
}

static u64 dr_domain_id_calc(enum mlx5dr_domain_type type)
{
	pid_t pid = task_tgid_vnr(current);

	return (pid << 8) | (type & 0xff);
}

static int
dr_dump_table_rx_tx(struct dr_dump_ctx *ctx, bool is_rx,
		    struct mlx5dr_table_rx_tx *table_rx_tx,
		    const u64 table_id)
{
	enum dr_dump_rec_type rec_type;
	int ret;
	char tmp_buf[BUF_SIZE] = {};

	rec_type = is_rx ? DR_DUMP_REC_TYPE_TABLE_RX :
			   DR_DUMP_REC_TYPE_TABLE_TX;

	ret = snprintf(tmp_buf, BUF_SIZE, "%d,0x%llx,0x%llx\n",
		       rec_type,
		       table_id,
		       dr_dump_icm_to_idx(table_rx_tx->s_anchor->chunk->icm_addr));
	if (ret < 0)
		return ret;

	ret = dr_copy_to_dump_buffer(ctx, tmp_buf);
	if (ret < 0)
		return ret;

	return 0;
}

static int dr_dump_table(struct dr_dump_ctx *ctx, struct mlx5dr_table *table)
{
	struct mlx5dr_table_rx_tx *rx = &table->rx;
	struct mlx5dr_table_rx_tx *tx = &table->tx;
	int ret;
	char tmp_buf[BUF_SIZE] = {};

	ret = snprintf(tmp_buf, BUF_SIZE, "%d,0x%llx,0x%llx,%d,%d\n",
		       DR_DUMP_REC_TYPE_TABLE,
		       (u64)(uintptr_t)table,
		       dr_domain_id_calc(table->dmn->type),
		       table->table_type,
		       table->level);
	if (ret < 0)
		return ret;

	ret = dr_copy_to_dump_buffer(ctx, tmp_buf);
	if (ret < 0)
		return ret;

	if (rx->nic_dmn) {
		ret = dr_dump_table_rx_tx(ctx, true, rx,
					  (u64)(uintptr_t)table);
		if (ret < 0)
			return ret;
	}

	if (tx->nic_dmn) {
		ret = dr_dump_table_rx_tx(ctx, false, tx,
					  (u64)(uintptr_t)table);
		if (ret < 0)
			return ret;
	}
	return 0;
}

static int dr_dump_table_all(struct dr_dump_ctx *ctx, struct mlx5dr_table *tbl)
{
	struct mlx5dr_matcher *matcher;
	int ret;

	ret = dr_dump_table(ctx, tbl);
	if (ret < 0)
		return ret;

	list_for_each_entry(matcher, &tbl->matcher_list, matcher_list) {
		ret = dr_dump_matcher_all(ctx, matcher);
		if (ret < 0)
			return ret;
	}
	return 0;
}

int mlx5dr_dump_dr_table(struct dr_dump_ctx *ctx, struct mlx5dr_table *tbl)
{
	int ret;

	if (!ctx || !tbl)
		return -EINVAL;

	mutex_lock(&tbl->dmn->dbg_mutex);

	ret = dr_dump_table_all(ctx, tbl);

	mutex_unlock(&tbl->dmn->dbg_mutex);

	return ret;
}

static int
dr_dump_send_ring(struct dr_dump_ctx *ctx, struct mlx5dr_send_ring *ring,
		  const u64 domain_id)
{
	int ret;
	char tmp_buf[BUF_SIZE] = {};

	ret = snprintf(tmp_buf, BUF_SIZE, "%d,0x%llx,0x%llx,0x%x,0x%x\n",
		       DR_DUMP_REC_TYPE_DOMAIN_SEND_RING,
		       (u64)(uintptr_t)ring,
		       domain_id,
		       ring->cq->mcq.cqn,
		       ring->qp->mqp.qpn);
	if (ret < 0)
		return ret;

	ret = dr_copy_to_dump_buffer(ctx, tmp_buf);
	if (ret < 0)
		return ret;

	return 0;
}

static int
dr_dump_domain_info_flex_parser(struct dr_dump_ctx *ctx,
				const char *flex_parser_name,
				const u8 flex_parser_value,
				const u64 domain_id)
{
	int ret;
	char tmp_buf[BUF_SIZE] = {};

	ret = snprintf(tmp_buf, BUF_SIZE, "%d,0x%llx,%s,0x%x\n",
		       DR_DUMP_REC_TYPE_DOMAIN_INFO_FLEX_PARSER,
		       domain_id,
		       flex_parser_name,
		       flex_parser_value);
	if (ret < 0)
		return ret;

	ret = dr_copy_to_dump_buffer(ctx, tmp_buf);
	if (ret < 0)
		return ret;
	return 0;
}

static int
dr_dump_domain_info_caps(struct dr_dump_ctx *ctx, struct mlx5dr_cmd_caps *caps,
			 const u64 domain_id)
{
	char tmp_buf[BUF_SIZE] = {};
	int i, ret;

	ret = snprintf(tmp_buf, BUF_SIZE, "%d,0x%llx,0x%x,0x%llx,0x%llx,0x%x,%d,%d\n",
		       DR_DUMP_REC_TYPE_DOMAIN_INFO_CAPS,
		       domain_id,
		       caps->gvmi,
		       caps->nic_rx_drop_address,
		       caps->nic_tx_drop_address,
		       caps->flex_protocols,
		       caps->num_vf_vports,
		       caps->eswitch_manager);
	if (ret < 0)
		return ret;

	for (i = 0; i < caps->num_vf_vports; i++) {
		ret = snprintf(tmp_buf, BUF_SIZE, "%d,0x%llx,%d,0x%x,0x%llx,0x%llx\n",
			       DR_DUMP_REC_TYPE_DOMAIN_INFO_VPORT,
			       domain_id,
			       i,
			       caps->pf_vf_vports_caps[i].vport_gvmi,
			       caps->pf_vf_vports_caps[i].icm_address_rx,
			       caps->pf_vf_vports_caps[i].icm_address_tx);
		if (ret < 0)
			return ret;
	}
	return 0;
}

static int
dr_dump_domain_info(struct dr_dump_ctx *ctx, struct mlx5dr_domain_info *info,
		    const u64 domain_id)
{
	int ret;

	ret = dr_dump_domain_info_caps(ctx, &info->caps, domain_id);
	if (ret < 0)
		return ret;

	ret = dr_dump_domain_info_flex_parser(ctx, "icmp_dw0",
					      info->caps.flex_parser_id_icmp_dw0,
					      domain_id);
	if (ret < 0)
		return ret;

	ret = dr_dump_domain_info_flex_parser(ctx, "icmp_dw1",
					      info->caps.flex_parser_id_icmp_dw1,
					      domain_id);
	if (ret < 0)
		return ret;

	ret = dr_dump_domain_info_flex_parser(ctx, "icmpv6_dw0",
					      info->caps.flex_parser_id_icmpv6_dw0,
					      domain_id);
	if (ret < 0)
		return ret;

	ret = dr_dump_domain_info_flex_parser(ctx, "icmpv6_dw1",
					      info->caps.flex_parser_id_icmpv6_dw1,
					      domain_id);
	if (ret < 0)
		return ret;

	return 0;
}

static int dr_dump_domain(struct dr_dump_ctx *ctx)
{
	struct mlx5dr_domain *dmn = ctx->dmn;
	enum mlx5dr_domain_type dmn_type = dmn->type;
	char tmp_buf[BUF_SIZE] = {};
	u64 domain_id;
	int ret;

	domain_id = dr_domain_id_calc(dmn_type);

	ret = snprintf(tmp_buf, BUF_SIZE, "%d,0x%llx,%d,0%x,%d,%s,%s\n",
		       DR_DUMP_REC_TYPE_DOMAIN,
		       domain_id,
		       dmn_type,
		       dmn->info.caps.gvmi,
		       dmn->info.supp_sw_steering,
		       PACKAGE_VERSION,
		       pci_name(dmn->mdev->pdev));
	if (ret < 0)
		return ret;

	ret = dr_copy_to_dump_buffer(ctx, tmp_buf);
	if (ret < 0)
		return ret;

	ret = dr_dump_domain_info(ctx, &dmn->info, domain_id);
	if (ret < 0)
		return ret;

	if (dmn->info.supp_sw_steering) {
		ret = dr_dump_send_ring(ctx, dmn->send_ring, domain_id);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int dr_dump_domain_all(struct dr_dump_ctx *ctx)
{
	struct mlx5dr_domain *dmn = ctx->dmn;
	struct mlx5dr_table *tbl;
	int ret;

	ret = dr_dump_domain(ctx);
	if (ret < 0)
		return ret;

	list_for_each_entry(tbl, &dmn->tbl_list, tbl_list) {
		ret = dr_dump_table_all(ctx, tbl);
		if (ret < 0)
			return ret;
	}

	return 0;
}

int mlx5dr_dump_dr_domain(struct dr_dump_ctx *ctx)
{
	struct mlx5dr_domain *dmn = ctx->dmn;
	int ret;

	mutex_lock(&dmn->dbg_mutex);

	ret = dr_dump_domain_all(ctx);

	mutex_unlock(&dmn->dbg_mutex);

	return ret;
}

static ssize_t
dr_dump_proc_read(struct file *file, char *buf, size_t len, loff_t *off)
{
	struct dr_dump_ctx *ctx;
	int write_index = 0;
	int num_copied = 0;

	ctx = (struct dr_dump_ctx *)file->private_data;

	while (ctx->iter && (&ctx->iter->list != &ctx->buf_list)) {
		num_copied = dr_copy_from_dump_buffer(buf + write_index,
						      ctx->iter,
						      len - write_index);
		if (num_copied < 0) {
			mlx5_core_warn(ctx->dmn->mdev, "proc read failed at copy_to_user\n");
			return num_copied;
		}

		write_index += num_copied;
		if (write_index == len) {
			break;
		} else if (write_index > len) {
			mlx5_core_warn(ctx->dmn->mdev, "proc read exceeded buf len\n");
			break;
		}

		ctx->iter = list_next_entry(ctx->iter, list);
	}

	return write_index;
}

static void dr_dump_free_buffers_list(struct dr_dump_ctx *ctx)
{
	struct dr_buffer *buf, *tmp_buf;

	list_for_each_entry_safe(buf, tmp_buf, &ctx->buf_list, list) {
		list_del(&buf->list);
		kvfree(buf);
	}
	ctx->iter = NULL;
}

static int dr_dump_proc_open(struct inode *inode, struct file *file)
{
	struct mlx5dr_domain *dmn = PDE_DATA(inode);
	struct dr_dump_ctx *ctx;
	struct dr_buffer *buf;
	int ret;

	ctx = kvmalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx) {
		ret = -ENOMEM;
		goto exit_err;
	}

	INIT_LIST_HEAD(&ctx->buf_list);
	ctx->dmn = dmn;

	buf = kvzalloc(sizeof(*buf), GFP_KERNEL);
	if (!buf) {
		ret = -ENOMEM;
		goto free_ctx;
	}

	list_add(&buf->list, &ctx->buf_list);
	ctx->iter = buf;

	file->private_data = ctx;
	ret = mlx5dr_dump_dr_domain(ctx);
	if (ret < 0)
		goto free_buffers;

	ctx->iter = buf;
	return 0;

free_buffers:
	dr_dump_free_buffers_list(ctx);
free_ctx:
	kvfree(ctx);
exit_err:
	return ret;
}

static int dr_dump_proc_release(struct inode *inode, struct file *file)
{
	struct dr_dump_ctx *ctx;

	ctx = (struct dr_dump_ctx *)file->private_data;
	dr_dump_free_buffers_list(ctx);
	kvfree(ctx);

	return 0;
}

static const struct file_operations mlx5_crdump_fops = {
	.owner = THIS_MODULE,
	.read = dr_dump_proc_read,
	.open = dr_dump_proc_open,
	.release = dr_dump_proc_release
};

int mlx5dr_dbg_init_dump(struct mlx5dr_domain *dmn)
{
	struct proc_dir_entry *proc_entry;

	if (dmn->type != MLX5DR_DOMAIN_TYPE_FDB) {
		mlx5_core_warn(dmn->mdev, "SW steering dump supports FDB domain only\n");
		return 0;
	}

	if (mlx5_smfs_fdb_dump_dir) {
		proc_entry = proc_create_data(pci_name(dmn->mdev->pdev), 0444,
					      mlx5_smfs_fdb_dump_dir,
					      &mlx5_crdump_fops, dmn);
		if (!proc_entry)
			mlx5_core_warn(dmn->mdev, "failed to create dump proc file\n");
	}

	return 0;
}

void mlx5dr_dbg_cleanup_dump(struct mlx5dr_domain *dmn)
{
	if (mlx5_smfs_fdb_dump_dir)
		remove_proc_entry(pci_name(dmn->mdev->pdev), mlx5_smfs_fdb_dump_dir);
}
