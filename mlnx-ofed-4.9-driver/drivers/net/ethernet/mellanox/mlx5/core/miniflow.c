// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2019 Mellanox Technologies. */

#include <linux/atomic.h>
#include <net/tc_act/tc_pedit.h>

#include "lib/devcom.h"
#include "miniflow.h"
#include "eswitch.h"
#include "en_rep.h"
#include "en_tc.h"
#include "en.h"

#ifdef HAVE_MINIFLOW

static atomic64_t global_version = ATOMIC64_INIT(0);

#define CT_DEBUG_COUNTERS 1

#if CT_DEBUG_COUNTERS
#define inc_debug_counter(counter_name) atomic64_inc(counter_name)
#define dec_debug_counter(counter_name) atomic64_dec(counter_name)

static atomic64_t nr_of_total_mf_succ = ATOMIC64_INIT(0);
static atomic64_t nr_of_total_merge_mf_succ = ATOMIC64_INIT(0);
static atomic64_t nr_of_total_del_mf_succ = ATOMIC64_INIT(0);

static atomic64_t nr_of_total_mf_work_requests = ATOMIC64_INIT(0);
static atomic64_t nr_of_total_merge_mf_work_requests = ATOMIC64_INIT(0);
static atomic64_t nr_of_total_del_mf_work_requests = ATOMIC64_INIT(0);

static atomic64_t nr_of_total_mf_err = ATOMIC64_INIT(0);
static atomic64_t nr_of_total_mf_err_alloc_flow = ATOMIC64_INIT(0);
static atomic64_t nr_of_total_mf_err_resolve_path_flows = ATOMIC64_INIT(0);
static atomic64_t nr_of_total_mf_err_merge_mirred = ATOMIC64_INIT(0);
static atomic64_t nr_of_total_mf_err_merge_hdr = ATOMIC64_INIT(0);
static atomic64_t nr_of_total_mf_err_attach_dummy_counter = ATOMIC64_INIT(0);
static atomic64_t nr_of_total_mf_err_fdb_add = ATOMIC64_INIT(0);
static atomic64_t nr_of_total_mf_err_verify_path = ATOMIC64_INIT(0);
static atomic64_t nr_of_total_mf_err_register = ATOMIC64_INIT(0);
static atomic64_t nr_of_total_mf_err_version = ATOMIC64_INIT(0);

static atomic64_t nr_of_merge_mfe_in_queue = ATOMIC64_INIT(0);
static atomic64_t nr_of_del_mfe_in_queue = ATOMIC64_INIT(0);

static atomic64_t nr_of_inflight_mfe = ATOMIC64_INIT(0);
static atomic64_t nr_of_inflight_merge_mfe = ATOMIC64_INIT(0);
static atomic64_t nr_of_inflight_del_mfe = ATOMIC64_INIT(0);
#else
#define inc_debug_counter(counter_name)
#define dec_debug_counter(counter_name)
#endif /*CT_DEBUG_COUNTERS*/

static atomic_t nr_of_mfe_in_queue = ATOMIC_INIT(0);
static atomic_t currently_in_hw = ATOMIC_INIT(0);

static int enable_ct_ageing = 1; /* On by default */
module_param(enable_ct_ageing, int, 0644);

static int max_nr_mf = 1024*1024;
module_param(max_nr_mf, int, 0644);

static uint ct_merger_probability;
module_param(ct_merger_probability, uint, 0644);

/* Derived from current insertion rate (flows/s) */
#define MINIFLOW_WORKQUEUE_MAX_SIZE 40 * 1000

static struct workqueue_struct *miniflow_wq;
static atomic_t miniflow_wq_size = ATOMIC_INIT(0);

static atomic_t miniflow_cache_ref = ATOMIC_INIT(0);
static struct kmem_cache *miniflow_cache;

DEFINE_PER_CPU(struct mlx5e_miniflow *, current_miniflow) = NULL;

static DEFINE_SPINLOCK(miniflow_lock);

static const struct rhashtable_params mf_ht_params = {
	.head_offset = offsetof(struct mlx5e_miniflow, node),
	.key_offset = offsetof(struct mlx5e_miniflow, path.cookies),
	.key_len = sizeof(((struct mlx5e_miniflow *)0)->path.cookies),
	.automatic_shrinking = true,
};

#define _sprintf(p, buf, format, arg...)				\
	((PAGE_SIZE - (int)(p - buf)) <= 0 ? 0 :			\
	scnprintf(p, PAGE_SIZE - (int)(p - buf), format, ## arg))

ssize_t mlx5_show_counters_ct(char *buf)
{
	char *p = buf;

#if CT_DEBUG_COUNTERS
	p += _sprintf(p, buf, "nr_of_total_mf_work_requests            : %lld\n", atomic64_read(&nr_of_total_mf_work_requests));
	p += _sprintf(p, buf, "nr_of_total_merge_mf_work_requests      : %lld\n", atomic64_read(&nr_of_total_merge_mf_work_requests));
	p += _sprintf(p, buf, "nr_of_total_del_mf_work_requests        : %lld\n", atomic64_read(&nr_of_total_del_mf_work_requests));
	p += _sprintf(p, buf, "\n");
	p += _sprintf(p, buf, "nr_of_mfe_in_queue                      : %d\n", atomic_read(&nr_of_mfe_in_queue));
	p += _sprintf(p, buf, "nr_of_merge_mfe_in_queue                : %lld\n", atomic64_read(&nr_of_merge_mfe_in_queue));
	p += _sprintf(p, buf, "nr_of_del_mfe_in_queue                  : %lld\n", atomic64_read(&nr_of_del_mfe_in_queue));
	p += _sprintf(p, buf, "\n");
	p += _sprintf(p, buf, "nr_of_inflight_mfe                      : %lld\n", atomic64_read(&nr_of_inflight_mfe));
	p += _sprintf(p, buf, "nr_of_inflight_merge_mfe                : %lld\n", atomic64_read(&nr_of_inflight_merge_mfe));
	p += _sprintf(p, buf, "nr_of_inflight_del_mfe                  : %lld\n", atomic64_read(&nr_of_inflight_del_mfe));
	p += _sprintf(p, buf, "\n");
	p += _sprintf(p, buf, "nr_of_total_mf_succ                     : %lld\n", atomic64_read(&nr_of_total_mf_succ));
	p += _sprintf(p, buf, "nr_of_total_merge_mf_succ               : %lld\n", atomic64_read(&nr_of_total_merge_mf_succ));
	p += _sprintf(p, buf, "nr_of_total_del_mf_succ                 : %lld\n", atomic64_read(&nr_of_total_del_mf_succ));
	p += _sprintf(p, buf, "\n");
	p += _sprintf(p, buf, "currently_in_hw                         : %d\n", atomic_read(&currently_in_hw));
	p += _sprintf(p, buf, "offloaded_flow_cnt                      : %d\n", mlx5_ct_flow_offloaded_count());
	p += _sprintf(p, buf, "\n");
	p += _sprintf(p, buf, "nr_of_total_mf_err                      : %lld\n", atomic64_read(&nr_of_total_mf_err));
	p += _sprintf(p, buf, "nr_of_total_mf_err_alloc_flow           : %lld\n", atomic64_read(&nr_of_total_mf_err_alloc_flow));
	p += _sprintf(p, buf, "nr_of_total_mf_err_resolve_path_flows   : %lld\n", atomic64_read(&nr_of_total_mf_err_resolve_path_flows));
	p += _sprintf(p, buf, "nr_of_total_mf_err_merge_mirred         : %lld\n", atomic64_read(&nr_of_total_mf_err_merge_mirred));
	p += _sprintf(p, buf, "nr_of_total_mf_err_merge_hdr            : %lld\n", atomic64_read(&nr_of_total_mf_err_merge_hdr));
	p += _sprintf(p, buf, "nr_of_total_mf_err_attach_dummy_counter : %lld\n", atomic64_read(&nr_of_total_mf_err_attach_dummy_counter));
	p += _sprintf(p, buf, "nr_of_total_mf_err_fdb_add              : %lld\n", atomic64_read(&nr_of_total_mf_err_fdb_add));
	p += _sprintf(p, buf, "nr_of_total_mf_err_verify_path          : %lld\n", atomic64_read(&nr_of_total_mf_err_verify_path));
	p += _sprintf(p, buf, "nr_of_total_mf_err_register             : %lld\n", atomic64_read(&nr_of_total_mf_err_register));
	p += _sprintf(p, buf, "nr_of_total_mf_err_version              : %lld\n", atomic64_read(&nr_of_total_mf_err_version));
	p += _sprintf(p, buf, "\n");
	p += _sprintf(p, buf, "enable_ct_ageing                        : %d\n", enable_ct_ageing);
	p += _sprintf(p, buf, "max_nr_mf                               : %d\n", max_nr_mf);
#else
	p += _sprintf(p, buf, "CT_DEBUG_COUNTERS is off\n");
	p += _sprintf(p, buf, "currently_in_hw                         : %d\n", atomic_read(&currently_in_hw));
	p += _sprintf(p, buf, "nr_of_mfe_in_queue                      : %d\n", atomic_read(&nr_of_mfe_in_queue));
	p += _sprintf(p, buf, "\n");
	p += _sprintf(p, buf, "enable_ct_ageing                        : %d\n", enable_ct_ageing);
	p += _sprintf(p, buf, "max_nr_mf                               : %d\n", max_nr_mf);
#endif /*CT_DEBUG_COUNTERS*/

	return (ssize_t)(p - buf);
}

static ssize_t counters_ct_show(struct device *device,
				struct device_attribute *attr,
				char *buf)
{
	return mlx5_show_counters_ct(buf);
}

static ssize_t counters_ct_store(struct device *device,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	return -ENOTSUPP;
}

static DEVICE_ATTR(counters_tc_ct, 0644, counters_ct_show, counters_ct_store);
static struct device_attribute *counters_tc_ct_attrs = &dev_attr_counters_tc_ct;

u64 miniflow_version_inc(void)
{
	return atomic64_inc_return(&global_version);
}

static struct rhashtable *get_mf_ht(struct mlx5e_priv *priv)
{
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	struct mlx5e_rep_priv *uplink_rpriv;

	uplink_rpriv = mlx5_eswitch_get_uplink_priv(esw, REP_ETH);
	return &uplink_rpriv->uplink_priv.mf_ht;
}

static void miniflow_path_append_cookie(struct mlx5e_miniflow *miniflow,
					u64 cookie, u8 flags)
{
	WARN_ON(cookie & MFC_INFOMASK);
	miniflow->path.cookies[miniflow->nr_flows++] = cookie | flags;
}

static u8 miniflow_cookie_flags(u64 cookie)
{
	return (cookie & MFC_INFOMASK);
}

#define MINIFLOW_ABORT -1
static void miniflow_abort(struct mlx5e_miniflow *miniflow)
{
	miniflow->nr_flows = MINIFLOW_ABORT;
}

static void miniflow_cleanup(struct mlx5e_miniflow *miniflow)
{
	struct mlx5e_tc_flow *flow;
	int j;

	/* If return value is non-zero, the bit was set in
	 * ct_flow_offload_del(), so no need to cleanup again
	 */
	if (!test_and_set_bit(0, &miniflow->cleanup))
		for (j = 0; j < MINIFLOW_MAX_CT_TUPLES; j++) {
			flow = miniflow->ct_tuples[j].flow;
			if (flow) {
				mlx5e_flow_put(flow->priv, flow);
				miniflow->ct_tuples[j].flow = NULL;
			}
		}
}

static struct mlx5e_miniflow *miniflow_alloc(void)
{
	return kmem_cache_alloc(miniflow_cache, GFP_ATOMIC);
}

static void miniflow_free(struct mlx5e_miniflow *miniflow)
{
	if (miniflow)
		kmem_cache_free(miniflow_cache, miniflow);
}

static struct mlx5e_miniflow *miniflow_read(void)
{
	return this_cpu_read(current_miniflow);
}

static void miniflow_write(struct mlx5e_miniflow *miniflow)
{
	this_cpu_write(current_miniflow, miniflow);
}

static void miniflow_init(struct mlx5e_miniflow *miniflow,
			  struct mlx5e_priv *priv,
			  struct rhashtable *mf_ht)
{
	memset(miniflow, 0, sizeof(*miniflow));

	miniflow->priv = priv;
	miniflow->mf_ht = mf_ht;
}

static void miniflow_free_current_miniflow(void)
{
	int cpu;

	for_each_possible_cpu(cpu) {
		miniflow_free(per_cpu(current_miniflow, cpu));
		per_cpu(current_miniflow, cpu) = NULL;
	}
}

static void miniflow_attach(struct mlx5e_miniflow *miniflow)
{
	struct mlx5e_tc_flow *flow;
	int i;

	spin_lock_bh(&miniflow_lock);
	/* Attach to all parent flows */
	for (i=0; i<miniflow->nr_flows; i++) {
		flow = miniflow->path.flows[i];

		miniflow->mnodes[i].miniflow = miniflow;
		list_add(&miniflow->mnodes[i].node, &flow->miniflow_list);
	}
	spin_unlock_bh(&miniflow_lock);
}

static void miniflow_detach(struct mlx5e_miniflow *miniflow)
{
	int i;

	/* Detach from all parent flows */
	for (i = 0; i < miniflow->nr_flows; i++)
		list_del(&miniflow->mnodes[i].node);
}

static void miniflow_merge_match(struct mlx5e_tc_flow *mflow,
				 struct mlx5e_tc_flow *flow,
				 u32 *merge_mask)
{
	u32 *dst = (u32 *)&mflow->esw_attr->parse_attr->spec;
	u32 *src = (u32 *)&flow->esw_attr->parse_attr->spec;
	struct mlx5_flow_spec tmp_spec_mask;
	u32 *mask = (u32 *)&tmp_spec_mask;
	int i;

	memset(&tmp_spec_mask, 0, sizeof(tmp_spec_mask));
	memcpy(&tmp_spec_mask.match_criteria, merge_mask,
	       sizeof(tmp_spec_mask.match_criteria));
	memcpy(&tmp_spec_mask.match_value, merge_mask,
	       sizeof(tmp_spec_mask.match_value));

	for (i = 0; i < sizeof(struct mlx5_flow_spec) / sizeof(u32); i++)
		*dst++ |= (*src++ & (~*mask++));

	mflow->esw_attr->inner_match_level =
		max(flow->esw_attr->inner_match_level,
		    mflow->esw_attr->inner_match_level);

	mflow->esw_attr->outer_match_level =
		max(flow->esw_attr->outer_match_level,
		    mflow->esw_attr->outer_match_level);

	mflow->esw_attr->is_tunnel_flow |=
		flow->esw_attr->is_tunnel_flow;
}

static void miniflow_merge_action(struct mlx5e_tc_flow *mflow,
				   struct mlx5e_tc_flow *flow)
{
	mflow->esw_attr->action |= flow->esw_attr->action;
}

static int miniflow_merge_mirred(struct mlx5e_tc_flow *mflow,
				  struct mlx5e_tc_flow *flow)
{
	struct mlx5_esw_flow_attr *dst_attr = mflow->esw_attr;
	struct mlx5_esw_flow_attr *src_attr = flow->esw_attr;
	int out_count;
	int i, j;

	if (!(src_attr->action & MLX5_FLOW_CONTEXT_ACTION_FWD_DEST))
		return 0;

	out_count = dst_attr->out_count + src_attr->out_count;
	if (out_count > MLX5_MAX_FLOW_FWD_VPORTS)
		return -1;

	for (i = 0, j = dst_attr->out_count; j < out_count; i++, j++)
		dst_attr->dests[j] = src_attr->dests[i];

	dst_attr->out_count = out_count;
	dst_attr->split_count += src_attr->split_count;

	return 0;
}

struct mlx5_field2match {
	u8  bitsize;
	u32 dwoff;
	u32 dwbitoff;
	u32 dwmask;
	u32 vmask;
};

#define FIELD2MATCH(fw_field, match_field)                             \
	[MLX5_ACTION_IN_FIELD_OUT_ ## fw_field] = {                    \
		__mlx5_bit_sz(fte_match_set_lyr_2_4, match_field),     \
		__mlx5_dw_off(fte_match_set_lyr_2_4, match_field),     \
		__mlx5_dw_bit_off(fte_match_set_lyr_2_4, match_field), \
		__mlx5_dw_mask(fte_match_set_lyr_2_4, match_field),    \
		__mlx5_mask(fte_match_set_lyr_2_4, match_field)        \
	}

static struct mlx5_field2match field2match[] = {
	FIELD2MATCH(DMAC_47_16, dmac_47_16),
	FIELD2MATCH(DMAC_15_0,  dmac_15_0),
	FIELD2MATCH(SMAC_47_16, smac_47_16),
	FIELD2MATCH(SMAC_15_0,  smac_15_0),
	FIELD2MATCH(ETHERTYPE,  ethertype),

	FIELD2MATCH(IP_TTL, ttl_hoplimit),
	FIELD2MATCH(SIPV4,  src_ipv4_src_ipv6.ipv4_layout.ipv4),
	FIELD2MATCH(DIPV4,  dst_ipv4_dst_ipv6.ipv4_layout.ipv4),

	FIELD2MATCH(SIPV6_127_96, src_ipv4_src_ipv6.ipv6_layout.ipv6[0][0]),
	FIELD2MATCH(SIPV6_95_64,  src_ipv4_src_ipv6.ipv6_layout.ipv6[4][0]),
	FIELD2MATCH(SIPV6_63_32,  src_ipv4_src_ipv6.ipv6_layout.ipv6[8][0]),
	FIELD2MATCH(SIPV6_31_0,   src_ipv4_src_ipv6.ipv6_layout.ipv6[12][0]),
	FIELD2MATCH(DIPV6_127_96, dst_ipv4_dst_ipv6.ipv6_layout.ipv6[0][0]),
	FIELD2MATCH(DIPV6_95_64,  dst_ipv4_dst_ipv6.ipv6_layout.ipv6[4][0]),
	FIELD2MATCH(DIPV6_63_32,  dst_ipv4_dst_ipv6.ipv6_layout.ipv6[8][0]),
	FIELD2MATCH(DIPV6_31_0,   dst_ipv4_dst_ipv6.ipv6_layout.ipv6[12][0]),

	FIELD2MATCH(IPV6_HOPLIMIT, ttl_hoplimit),

	FIELD2MATCH(TCP_SPORT, tcp_sport),
	FIELD2MATCH(TCP_DPORT, tcp_dport),
	FIELD2MATCH(TCP_FLAGS, tcp_flags),

	FIELD2MATCH(UDP_SPORT, udp_sport),
	FIELD2MATCH(UDP_DPORT, udp_dport),
};

/**
 * Calculate the anti-mask usage in merge match
 **/
static void
miniflow_merge_calculate_mask(struct mlx5e_tc_flow_parse_attr *src_parse_attr,
			      __be32 *tmp_mask)
{
	uint action_size = MLX5_MH_ACT_SZ;
	uint action_num;
	void *action;
	uint loop;

	action = src_parse_attr->mod_hdr_actions;
	action_num = src_parse_attr->num_mod_hdr_actions;

	for (loop = 0; loop < action_num; ++loop) {
		u8  field;

		/* just zero all the field in match, not be precise in bit.
		   so if set field has mask, it will do no effective
		*/
		field = MLX5_GET(set_action_in, action, field);
		if (((field <= MLX5_ACTION_IN_FIELD_OUT_DIPV4) ||
		     (field == MLX5_ACTION_IN_FIELD_OUT_IPV6_HOPLIMIT)) &&
		    (field != MLX5_ACTION_IN_FIELD_OUT_IP_DSCP)) {
			struct mlx5_field2match *tmpf2m = &field2match[field];

			*((tmp_mask) + tmpf2m->dwoff) = cpu_to_be32(
					be32_to_cpu(
						*((tmp_mask) + tmpf2m->dwoff)
					) | tmpf2m->dwmask);
		}

		action += action_size;
	}
}

static int miniflow_merge_hdr(struct mlx5e_priv *priv,
			      struct mlx5e_tc_flow *mflow,
			      struct mlx5e_tc_flow *flow,
			      u32 *tmp_mask)
{
	struct mlx5e_tc_flow_parse_attr *dst_parse_attr;
	struct mlx5e_tc_flow_parse_attr *src_parse_attr;
	struct pedit_headers_action hdrs[2] = {};
	int max_actions, action_size;
	int err;

	if (!(flow->esw_attr->action & MLX5_FLOW_CONTEXT_ACTION_MOD_HDR))
		return 0;

	action_size = MLX5_MH_ACT_SZ;
	hdrs[TCA_PEDIT_KEY_EX_CMD_SET].pedits = 16; /* maximum */

	dst_parse_attr = mflow->esw_attr->parse_attr;
	if (!dst_parse_attr->mod_hdr_actions) {
		err = alloc_mod_hdr_actions(priv, hdrs,
					    MLX5_FLOW_NAMESPACE_FDB,
					    dst_parse_attr, GFP_ATOMIC);
		if (err) {
			mlx5_core_warn(priv->mdev, "alloc hdr actions failed\n");
			return -ENOMEM;
		}

		dst_parse_attr->num_mod_hdr_actions = 0;
	}

	max_actions = MLX5_CAP_ESW_FLOWTABLE_FDB(priv->mdev, max_modify_header_actions);
	src_parse_attr = flow->esw_attr->parse_attr;

	if (dst_parse_attr->num_mod_hdr_actions + src_parse_attr->num_mod_hdr_actions >= max_actions) {
		mlx5_core_warn(priv->mdev, "max hdr actions reached\n");
		kfree(dst_parse_attr->mod_hdr_actions);
		dst_parse_attr->mod_hdr_actions = NULL;
		return -E2BIG;
	}

	memcpy(dst_parse_attr->mod_hdr_actions + dst_parse_attr->num_mod_hdr_actions * action_size,
	       src_parse_attr->mod_hdr_actions,
	       src_parse_attr->num_mod_hdr_actions * action_size);

	dst_parse_attr->num_mod_hdr_actions += src_parse_attr->num_mod_hdr_actions;

	miniflow_merge_calculate_mask(src_parse_attr, tmp_mask);

	return 0;
}

static void miniflow_merge_vxlan(struct mlx5e_tc_flow *mflow,
				 struct mlx5e_tc_flow *flow)
{
	memcpy(mflow->esw_attr->parse_attr->mirred_ifindex,
	       flow->esw_attr->parse_attr->mirred_ifindex,
	       sizeof(flow->esw_attr->parse_attr->mirred_ifindex));

	memcpy(mflow->esw_attr->parse_attr->tun_info,
	       flow->esw_attr->parse_attr->tun_info,
	       sizeof(flow->esw_attr->parse_attr->tun_info));
}

static u8 mlx5e_etype_to_ipv(u16 ethertype)
{
	if (ethertype == ETH_P_IP)
		return 4;

	if (ethertype == ETH_P_IPV6)
		return 6;

	return 0;
}

static void miniflow_merge_tuple(struct mlx5e_tc_flow *mflow,
				  struct nf_conntrack_tuple *nf_tuple)
{
	struct mlx5_flow_spec *spec = &mflow->esw_attr->parse_attr->spec;
	void *headers_c, *headers_v;
	int match_ipv;
	u8 ipv;

	if (mflow->esw_attr->action & MLX5_FLOW_CONTEXT_ACTION_DECAP) {
		headers_c = MLX5_ADDR_OF(fte_match_param, spec->match_criteria,
					 inner_headers);
		headers_v = MLX5_ADDR_OF(fte_match_param, spec->match_value,
					 inner_headers);
		match_ipv = MLX5_CAP_FLOWTABLE_NIC_RX(mflow->priv->mdev,
					 ft_field_support.inner_ip_version);
	} else {
		headers_c = MLX5_ADDR_OF(fte_match_param, spec->match_criteria,
					 outer_headers);
		headers_v = MLX5_ADDR_OF(fte_match_param, spec->match_value,
					 outer_headers);
		match_ipv = MLX5_CAP_FLOWTABLE_NIC_RX(mflow->priv->mdev,
					 ft_field_support.outer_ip_version);
	}

	ipv = mlx5e_etype_to_ipv(ntohs(nf_tuple->src.l3num));
	if (match_ipv && ipv) {
		MLX5_SET_TO_ONES(fte_match_set_lyr_2_4, headers_c, ip_version);
		MLX5_SET(fte_match_set_lyr_2_4, headers_v, ip_version, ipv);
	} else {
		MLX5_SET_TO_ONES(fte_match_set_lyr_2_4, headers_c, ethertype);
		MLX5_SET(fte_match_set_lyr_2_4, headers_v, ethertype, nf_tuple->src.l3num);
	}

	if (nf_tuple->src.l3num == htons(ETH_P_IP)) {
		memcpy(MLX5_ADDR_OF(fte_match_set_lyr_2_4, headers_v,
					src_ipv4_src_ipv6.ipv4_layout.ipv4),
					&nf_tuple->src.u3.ip,
					4);
		memcpy(MLX5_ADDR_OF(fte_match_set_lyr_2_4, headers_v,
					dst_ipv4_dst_ipv6.ipv4_layout.ipv4),
					&nf_tuple->dst.u3.ip,
					4);

		MLX5_SET_TO_ONES(fte_match_set_lyr_2_4, headers_c,
					src_ipv4_src_ipv6.ipv4_layout.ipv4);
		MLX5_SET_TO_ONES(fte_match_set_lyr_2_4, headers_c,
					dst_ipv4_dst_ipv6.ipv4_layout.ipv4);
	}

	MLX5_SET_TO_ONES(fte_match_set_lyr_2_4, headers_c, ip_protocol);
	MLX5_SET(fte_match_set_lyr_2_4, headers_v, ip_protocol, nf_tuple->dst.protonum);

	switch (nf_tuple->dst.protonum) {
	case IPPROTO_UDP:
		MLX5_SET_TO_ONES(fte_match_set_lyr_2_4, headers_c, udp_dport);
		MLX5_SET(fte_match_set_lyr_2_4, headers_v, udp_dport, ntohs(nf_tuple->dst.u.udp.port));

		MLX5_SET_TO_ONES(fte_match_set_lyr_2_4, headers_c, udp_sport);
		MLX5_SET(fte_match_set_lyr_2_4, headers_v, udp_sport, ntohs(nf_tuple->src.u.udp.port));
	break;
	case IPPROTO_TCP:
		MLX5_SET_TO_ONES(fte_match_set_lyr_2_4, headers_c, tcp_dport);
		MLX5_SET(fte_match_set_lyr_2_4, headers_v, tcp_dport, ntohs(nf_tuple->dst.u.tcp.port));

		MLX5_SET_TO_ONES(fte_match_set_lyr_2_4, headers_c, tcp_sport);
		MLX5_SET(fte_match_set_lyr_2_4, headers_v, tcp_sport, ntohs(nf_tuple->src.u.tcp.port));

		// FIN=1 SYN=2 RST=4 PSH=8 ACK=16 URG=32
		MLX5_SET(fte_match_set_lyr_2_4, headers_c, tcp_flags, 0x17);
		MLX5_SET(fte_match_set_lyr_2_4, headers_v, tcp_flags, 0x10);
	break;
	}
}

static int miniflow_register_ct_tuple(struct mlx5e_ct_tuple *ct_tuple)
{
	struct nf_conntrack_tuple *tuple;
	struct nf_conntrack_zone *zone;
	struct net *net;

	net = (struct net *)ct_tuple->net;
	zone = &ct_tuple->zone;
	tuple = &ct_tuple->tuple;

	return mlx5_ct_flow_offload_add(net, zone, tuple, ct_tuple->flow);
}

static int miniflow_register_ct_flow(struct mlx5e_miniflow *miniflow)
{
	struct mlx5e_ct_tuple *ct_tuple;
	int err = 0;
	int i;

	if (!enable_ct_ageing)
		return 0;

	for (i = 0; i < miniflow->nr_ct_tuples; i++) {
		ct_tuple = &miniflow->ct_tuples[i];
		ct_tuple->flow->miniflow = miniflow;

		err = miniflow_register_ct_tuple(ct_tuple);
		if (err)
			break;
	}

	return err;
}

static int __miniflow_ct_parse_nat(struct mlx5e_priv *priv,
				   struct mlx5e_ct_tuple *ct_tuple,
				   struct mlx5e_tc_flow_parse_attr *parse_attr)
{
	struct pedit_headers_action hdrs[2] = {};
	int namespace = MLX5_FLOW_NAMESPACE_FDB;
	struct flow_action_entry acts[2] = {};
	int action_flags = 0;
	int i;

	acts[0].id = FLOW_ACTION_MANGLE;
	acts[0].mangle.htype = FLOW_ACT_MANGLE_HDR_TYPE_IP4;
	acts[0].mangle.mask = 0;
	acts[0].mangle.val = ct_tuple->ipv4;
	acts[0].mangle.offset = ct_tuple->nat & IPS_SRC_NAT ?
				offsetof(struct iphdr, saddr) :
				offsetof(struct iphdr, daddr);

	acts[1].id = FLOW_ACTION_MANGLE;
	acts[1].mangle.htype = FLOW_ACT_MANGLE_HDR_TYPE_IP4;
	acts[1].mangle.mask = 0xFFFF0000;
	acts[1].mangle.val = ct_tuple->port;

	switch (ct_tuple->proto) {
	case IPPROTO_UDP:
		acts[1].mangle.htype = FLOW_ACT_MANGLE_HDR_TYPE_UDP;
		acts[1].mangle.offset = ct_tuple->nat & IPS_SRC_NAT ?
					offsetof(struct udphdr, source) :
					offsetof(struct udphdr, dest);
	break;
	case IPPROTO_TCP:
		acts[1].mangle.htype = FLOW_ACT_MANGLE_HDR_TYPE_TCP;
		acts[1].mangle.offset = ct_tuple->nat & IPS_SRC_NAT ?
					offsetof(struct tcphdr, source) :
					offsetof(struct tcphdr, dest);
	break;
	}

	for (i = 0; i < 2; i++)
		parse_tc_pedit_action(priv, &acts[i], namespace, parse_attr,
				      hdrs, NULL);

	return alloc_tc_pedit_action(priv, namespace, parse_attr, hdrs,
				     &action_flags, NULL);
}

static int miniflow_ct_parse_nat(struct mlx5e_priv *priv,
				 struct mlx5e_tc_flow *flow,
				 struct mlx5e_ct_tuple *ct_tuple)
{
	int err;

	if (!ct_tuple->nat)
		return 0;

	err = __miniflow_ct_parse_nat(priv, ct_tuple,
				      flow->esw_attr->parse_attr);
	if (err)
		return err;

	flow->esw_attr->action |= MLX5_FLOW_CONTEXT_ACTION_MOD_HDR;
	return 0;
}

static struct mlx5e_ct_tuple *
miniflow_ct_tuple_alloc(struct mlx5e_miniflow *miniflow)
{
	if (miniflow->nr_ct_tuples >= MINIFLOW_MAX_CT_TUPLES) {
		mlx5_core_err(miniflow->priv->mdev,
			      "Failed to allocate ct_tuple, maximum (%d)",
			      MINIFLOW_MAX_CT_TUPLES);
		return NULL;
	}

	return &miniflow->ct_tuples[miniflow->nr_ct_tuples++];
}

static struct mlx5e_tc_flow *
miniflow_ct_flow_alloc(struct mlx5e_priv *priv,
		       struct mlx5e_ct_tuple *ct_tuple)
{
	struct mlx5e_tc_flow_parse_attr *parse_attr;
	struct mlx5e_tc_flow *flow;
	int flow_flags;
	int attr_size;
	int err;

	flow_flags = BIT(MLX5E_TC_FLOW_FLAG_ESWITCH) | BIT(MLX5E_TC_FLOW_FLAG_CT);

	if (ct_tuple->tuple.dst.dir == IP_CT_DIR_ORIGINAL)
		flow_flags |= BIT(MLX5E_TC_FLOW_FLAG_CT_ORIG);

	attr_size = sizeof(struct mlx5_esw_flow_attr);
	err = mlx5e_alloc_flow(priv, attr_size, 0 /* cookie */, flow_flags,
			       GFP_ATOMIC, &parse_attr, &flow);
	if (err)
		return NULL;

	flow->esw_attr->parse_attr = parse_attr;
	flow->esw_attr->action = MLX5_FLOW_CONTEXT_ACTION_COUNT;

	err = miniflow_ct_parse_nat(priv, flow, ct_tuple);
	if (err)
		goto err_free;

	ct_tuple->flow = flow;

	return flow;

err_free:
	mlx5e_flow_put(priv, flow);
	return NULL;
}

static int miniflow_resolve_path_flows(struct mlx5e_miniflow *miniflow)
{
	struct mlx5e_priv *priv = miniflow->priv;
	struct mlx5e_tc_flow *flow;
	unsigned long cookie;
	int i, j;

	for (i = 0, j = 0; i < miniflow->nr_flows; i++) {
		cookie = miniflow->path.cookies[i];

		if (miniflow_cookie_flags(cookie) & MFC_CT_FLOW)
			flow = miniflow_ct_flow_alloc(priv, &miniflow->ct_tuples[j++]);
		else
			flow = mlx5e_lookup_tc_ht(priv, &cookie, MLX5_TC_FLAG(ESW_OFFLOAD));

		if (!flow)
			return -1;

		if (miniflow->version < flow->version) {
			inc_debug_counter(&nr_of_total_mf_err_version);
			return -1;
		}

		miniflow->path.flows[i] = flow;
	}

	return 0;
}

static int miniflow_verify_path_flows(struct mlx5e_miniflow *miniflow)
{
	struct mlx5e_priv *priv = miniflow->priv;
	struct mlx5e_tc_flow *flow;
	unsigned long cookie;
	int i;

	for (i = 0; i < miniflow->nr_flows; i++) {
		cookie = miniflow->path.cookies[i];
		if (miniflow_cookie_flags(cookie) & MFC_CT_FLOW)
			continue;

		flow = mlx5e_lookup_tc_ht(priv, &cookie, MLX5_TC_FLAG(ESW_OFFLOAD));
		if (!flow)
			return -1;

		if (miniflow->version < flow->version) {
			inc_debug_counter(&nr_of_total_mf_err_version);
			return -1;
		}
	}

	return 0;
}

#define ESW_FLOW_COUNTER(flow) (flow->esw_attr->counter)

static void miniflow_link_dummy_counters(struct mlx5e_tc_flow *flow,
					 struct mlx5_fc **dummies,
					 int nr_dummies)
{
	struct mlx5_fc *counter;

	counter = ESW_FLOW_COUNTER(flow);
	if (!counter)
		return;

	mlx5_fc_link_dummies(counter, dummies, nr_dummies);
}

static void miniflow_unlink_dummy_counters(struct mlx5e_tc_flow *flow)
{
	struct mlx5_fc *counter;

	counter = ESW_FLOW_COUNTER(flow);
	if (!counter)
		return;

	mlx5_fc_unlink_dummies(counter);
}

static int miniflow_attach_dummy_counter(struct mlx5e_tc_flow *flow)
{
	struct mlx5_fc *counter;

	if (flow->dummy_counter)
		return 0;

	if (flow->esw_attr->action & MLX5_FLOW_CONTEXT_ACTION_COUNT) {
		counter = mlx5_fc_alloc_dummy_counter();
		if (!counter)
			return -ENOMEM;

		rcu_read_lock();
		if (flow->dummy_counter)
			mlx5_fc_free_dummy_counter(counter);
		else
			flow->dummy_counter = counter;
		rcu_read_unlock();
	}

	return 0;
}

static int miniflow_add_fdb_flow(struct mlx5e_priv *priv,
				 struct mlx5e_tc_flow *mflow)
{
	int err;

	err = mlx5e_tc_add_fdb_flow(priv, mflow, NULL);
	complete_all(&mflow->init_done);
	if (err) {
		inc_debug_counter(&nr_of_total_mf_err_fdb_add);
		return err;
	}

	return 0;
}

static int miniflow_alloc_flow(struct mlx5e_miniflow *miniflow,
			       struct mlx5e_priv *priv,
			       struct mlx5_eswitch_rep *in_rep,
			       struct mlx5_core_dev *in_mdev,
			       struct mlx5e_tc_flow **out_flow,
			       struct mlx5_fc **dummy_counters)
{
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	struct mlx5e_tc_flow_parse_attr *mparse_attr;
	struct mlx5e_rep_priv *rpriv = priv->ppriv;
	u32 tmp_mask[MLX5_ST_SZ_DW(fte_match_param)];
	struct net_device *filter_dev = NULL;
	unsigned long flow_flags;
	struct mlx5e_tc_flow *mflow;
	int attr_size, i;
	int err;

	flow_flags = BIT(MLX5E_TC_FLOW_FLAG_ESWITCH) | BIT(MLX5E_TC_FLOW_FLAG_SIMPLE);
	attr_size = sizeof(struct mlx5_esw_flow_attr);
	err = mlx5e_alloc_flow(priv, attr_size, 0 /* cookie */, flow_flags,
			       GFP_KERNEL, &mparse_attr, &mflow);
	if (err) {
		inc_debug_counter(&nr_of_total_mf_err_alloc_flow);
		return -1;
	}

	mflow->esw_attr->parse_attr = mparse_attr;

	rcu_read_lock();
	if (dummy_counters) {
		err = miniflow_resolve_path_flows(miniflow);
		if (err) {
			inc_debug_counter(&nr_of_total_mf_err_resolve_path_flows);
			goto err_rcu;
		}

		miniflow->flow = mflow;
		mflow->miniflow = miniflow;
	} else {
		err = miniflow_verify_path_flows(miniflow);
		if (err) {
			inc_debug_counter(&nr_of_total_mf_err_verify_path);
			goto err_rcu;
		}
	}

	mflow->esw_attr->in_rep = in_rep;
	mflow->esw_attr->in_mdev = in_mdev;

	if (MLX5_CAP_ESW(esw->dev, counter_eswitch_affinity) ==
	    MLX5_COUNTER_SOURCE_ESWITCH)
		mflow->esw_attr->counter_dev = in_mdev;
	else
		mflow->esw_attr->counter_dev = priv->mdev;

	/* Main merge loop */
	memset(tmp_mask, 0, sizeof(tmp_mask));

	for (i=0; i < miniflow->nr_flows; i++) {
		struct mlx5e_tc_flow *flow = miniflow->path.flows[i];

		flow_flags |= flow->flags;

		if (!filter_dev)
			filter_dev = flow->esw_attr->parse_attr->filter_dev;

		miniflow_merge_match(mflow, flow, tmp_mask);
		miniflow_merge_action(mflow, flow);
		err = miniflow_merge_mirred(mflow, flow);
		if (err) {
			inc_debug_counter(&nr_of_total_mf_err_merge_mirred);
			goto err_rcu;
		}
		err = miniflow_merge_hdr(priv, mflow, flow, tmp_mask);
		if (err) {
			inc_debug_counter(&nr_of_total_mf_err_merge_hdr);
			goto err_rcu;
		}
		miniflow_merge_vxlan(mflow, flow);
		/* TODO: vlan is not supported yet */

		if (dummy_counters) {
			err = miniflow_attach_dummy_counter(flow);
			if (err) {
				inc_debug_counter(&nr_of_total_mf_err_attach_dummy_counter);
				goto err_rcu;
			}
			dummy_counters[i] = flow->dummy_counter;
		}
	}
	rcu_read_unlock();

	mflow->esw_attr->parse_attr->filter_dev = filter_dev;
	mflow->flags = flow_flags;
	miniflow_merge_tuple(mflow, &miniflow->tuple);
	/* TODO: Workaround: crashes otherwise, should fix */
	mflow->esw_attr->action &= ~(MLX5_FLOW_CONTEXT_ACTION_CT |
				     MLX5_FLOW_CONTEXT_ACTION_GOTO);

	*out_flow = mflow;
	return 0;

err_rcu:
	rcu_read_unlock();
	mlx5e_flow_put(priv, mflow);
	return err;
}

static bool miniflow_is_peer_flow_needed(struct mlx5e_tc_flow *flow)
{
	struct mlx5_esw_flow_attr *attr = flow->esw_attr;

	return mlx5_lag_is_sriov(attr->in_mdev);
}

static int miniflow_add_peer_flow(struct mlx5e_miniflow *miniflow,
				  struct mlx5e_tc_flow *flow)
{
	struct mlx5e_priv *priv = flow->priv, *peer_priv;
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	struct mlx5_devcom *devcom = priv->mdev->priv.devcom;
	struct mlx5e_rep_priv *peer_urpriv;
	struct mlx5_eswitch_rep *in_rep;
	struct mlx5e_tc_flow *peer_flow;
	struct mlx5_core_dev *in_mdev;
	struct mlx5_eswitch *peer_esw;
	int err;

	peer_esw = mlx5_devcom_get_peer_data(devcom, MLX5_DEVCOM_ESW_OFFLOADS);
	if (!peer_esw)
		return -ENODEV;

	peer_urpriv = mlx5_eswitch_get_uplink_priv(peer_esw, REP_ETH);
	peer_priv = netdev_priv(peer_urpriv->netdev);

	if (flow->esw_attr->in_rep->vport == MLX5_VPORT_UPLINK) {
		in_mdev = peer_priv->mdev;
		in_rep = peer_urpriv->rep;
	} else {
		in_mdev = priv->mdev;
		in_rep = flow->esw_attr->in_rep;
	}

	err = miniflow_alloc_flow(miniflow, peer_priv, in_rep,
				  in_mdev, &peer_flow, NULL);
	if (err)
		goto out;

	err = miniflow_add_fdb_flow(peer_priv, peer_flow);
	if (err)
		goto err_add;

	flow->peer_flow = peer_flow;
	flow->flags |= BIT(MLX5E_TC_FLOW_FLAG_DUP);
	mutex_lock(&esw->offloads.peer_mutex);
	list_add_tail(&flow->peer, &esw->offloads.peer_flows);
	mutex_unlock(&esw->offloads.peer_mutex);
	goto out;

err_add:
	mlx5e_flow_put(peer_priv, peer_flow);
out:
	mlx5_devcom_release_peer_data(devcom, MLX5_DEVCOM_ESW_OFFLOADS);
	return err;
}

static int __miniflow_merge(struct mlx5e_miniflow *miniflow)
{
	struct mlx5_fc *dummy_counters[MINIFLOW_MAX_FLOWS];
	struct mlx5e_priv *priv = miniflow->priv;
	struct mlx5e_rep_priv *rpriv = priv->ppriv;
	struct mlx5_eswitch_rep *in_rep = rpriv->rep;
	struct mlx5_core_dev *in_mdev = priv->mdev;
	struct rhashtable *mf_ht = get_mf_ht(priv);
	struct mlx5e_tc_flow *mflow;
	int err;

	err = miniflow_alloc_flow(miniflow, priv, in_rep, in_mdev,
				  &mflow, dummy_counters);
	if (err)
		goto err_alloc;

	err = miniflow_add_fdb_flow(priv, mflow);
	if (err)
		goto err_verify;

	if (miniflow_is_peer_flow_needed(mflow)) {
		err = miniflow_add_peer_flow(miniflow, mflow);
		if (err)
			goto err_verify;
	}

	rcu_read_lock();
	err = miniflow_verify_path_flows(miniflow);
	if (err) {
		rcu_read_unlock();
		inc_debug_counter(&nr_of_total_mf_err_verify_path);
		goto err_verify;
	}

	miniflow_link_dummy_counters(mflow,
				     dummy_counters,
				     miniflow->nr_flows);

	if (mflow->flags & BIT(MLX5E_TC_FLOW_FLAG_DUP))
		miniflow_link_dummy_counters(mflow->peer_flow,
					     dummy_counters,
					     miniflow->nr_flows);

	miniflow_attach(miniflow);

	atomic_inc((atomic_t *)&currently_in_hw);

	err = miniflow_register_ct_flow(miniflow);
	if (err) {
		rcu_read_unlock();
		miniflow_cleanup(miniflow);
		inc_debug_counter(&nr_of_total_mf_err_register);
		inc_debug_counter(&nr_of_total_mf_err);
		return -1;
	}

	rcu_read_unlock();
	inc_debug_counter(&nr_of_total_mf_succ);
	inc_debug_counter(&nr_of_total_merge_mf_succ);
	return 0;

err_verify:
	mlx5e_flow_put(priv, mflow);
err_alloc:
	rhashtable_remove_fast(mf_ht, &miniflow->node, mf_ht_params);
	miniflow_cleanup(miniflow);
	miniflow_free(miniflow);
	inc_debug_counter(&nr_of_total_mf_err);
	return -1;
}

static bool miniflow_workqueue_busy(void)
{
	return (atomic_read(&miniflow_wq_size) > MINIFLOW_WORKQUEUE_MAX_SIZE);
}

static void miniflow_merge_work(struct work_struct *work)
{
	struct mlx5e_miniflow *miniflow = container_of(work, struct mlx5e_miniflow, work);

	inc_debug_counter(&nr_of_inflight_mfe);
	dec_debug_counter(&nr_of_merge_mfe_in_queue);
	inc_debug_counter(&nr_of_inflight_merge_mfe);

	__miniflow_merge(miniflow);

	dec_debug_counter(&nr_of_inflight_mfe);
	dec_debug_counter(&nr_of_inflight_merge_mfe);
	atomic_dec(&nr_of_mfe_in_queue);
	atomic_dec(&miniflow_wq_size);
}

static int miniflow_merge(struct mlx5e_miniflow *miniflow)
{
	atomic_inc(&nr_of_mfe_in_queue);
	atomic_inc(&miniflow_wq_size);
	inc_debug_counter(&nr_of_merge_mfe_in_queue);
	inc_debug_counter(&nr_of_total_mf_work_requests);
	inc_debug_counter(&nr_of_total_merge_mf_work_requests);

	miniflow->version = miniflow_version_inc();
	INIT_WORK(&miniflow->work, miniflow_merge_work);
	if (queue_work(miniflow_wq, &miniflow->work))
		return 0;

	return -1;
}

static void mlx5e_del_miniflow(struct mlx5e_miniflow *miniflow)
{
	struct rhashtable *mf_ht = get_mf_ht(miniflow->priv);

	mlx5e_flow_put(miniflow->priv, miniflow->flow);
	rhashtable_remove_fast(mf_ht, &miniflow->node, mf_ht_params);
	miniflow_free(miniflow);

	atomic_dec(&currently_in_hw);
	inc_debug_counter(&nr_of_total_del_mf_succ);
	inc_debug_counter(&nr_of_total_mf_succ);
}

static void mlx5e_del_miniflow_work(struct work_struct *work)
{
	struct mlx5e_miniflow *miniflow = container_of(work,
						       struct mlx5e_miniflow,
						       work);

	atomic_dec(&nr_of_mfe_in_queue);
	inc_debug_counter(&nr_of_inflight_mfe);
	dec_debug_counter(&nr_of_del_mfe_in_queue);
	inc_debug_counter(&nr_of_inflight_del_mfe);

	miniflow_cleanup(miniflow);
	mlx5e_del_miniflow(miniflow);

	dec_debug_counter(&nr_of_inflight_del_mfe);
	dec_debug_counter(&nr_of_inflight_mfe);
}

void mlx5e_del_miniflow_list(struct mlx5e_tc_flow *flow)
{
	struct mlx5e_miniflow_node *mnode, *n;

	spin_lock_bh(&miniflow_lock);
	list_for_each_entry_safe(mnode, n, &flow->miniflow_list, node) {
		struct mlx5e_miniflow *miniflow = mnode->miniflow;

		if (miniflow->flow->flags & BIT(MLX5E_TC_FLOW_FLAG_DUP))
			miniflow_unlink_dummy_counters(miniflow->flow->peer_flow);

		miniflow_unlink_dummy_counters(miniflow->flow);
		miniflow_detach(miniflow);

		atomic_inc(&nr_of_mfe_in_queue);
		inc_debug_counter(&nr_of_del_mfe_in_queue);
		inc_debug_counter(&nr_of_total_mf_work_requests);
		inc_debug_counter(&nr_of_total_del_mf_work_requests);

		INIT_WORK(&miniflow->work, mlx5e_del_miniflow_work);
		queue_work(miniflow_wq, &miniflow->work);
	}
	spin_unlock_bh(&miniflow_lock);
}

static int miniflow_cache_get(void)
{
	atomic_inc(&miniflow_cache_ref);

	if (atomic_read(&miniflow_cache_ref) > 1)
		return 0;

	miniflow_cache = kmem_cache_create("mlx5_miniflow_cache",
					    sizeof(struct mlx5e_miniflow),
					    0, SLAB_HWCACHE_ALIGN,
					    NULL);
	if (!miniflow_cache)
		return -ENOMEM;

	miniflow_wq = alloc_workqueue("miniflow",
				      __WQ_LEGACY | WQ_MEM_RECLAIM |
				      WQ_UNBOUND | WQ_HIGHPRI | WQ_SYSFS, 16);
	if (!miniflow_wq)
		goto err_wq;

	if (mlx5_ct_flow_offload_table_init())
		goto err_offload_table;

	return 0;

err_offload_table:
	destroy_workqueue(miniflow_wq);
err_wq:
	kmem_cache_destroy(miniflow_cache);
	atomic_dec(&miniflow_cache_ref);
	return -ENOMEM;
}

static void miniflow_cache_put(void)
{
	if (atomic_dec_and_test(&miniflow_cache_ref)) {
		mlx5_ct_flow_offload_table_destroy();
		destroy_workqueue(miniflow_wq);
		kmem_cache_destroy(miniflow_cache);
	}
}

int miniflow_cache_init(struct mlx5e_priv *priv)
{
	struct rhashtable *mf_ht = get_mf_ht(priv);
	int err;

	err = device_create_file(&priv->mdev->pdev->dev, counters_tc_ct_attrs);
	if (err) {
		mlx5_core_err(priv->mdev, "Failed to create miniflow sysfs\n");
		return err;
	}

	err = miniflow_cache_get();
	if (err)
		goto err_cache;

	err = rhashtable_init(mf_ht, &mf_ht_params);
	if (err)
		goto err_mf_ht;

	return 0;

err_mf_ht:
	miniflow_cache_put();
err_cache:
	device_remove_file(&priv->mdev->pdev->dev, counters_tc_ct_attrs);
	return -ENOMEM;
}

void miniflow_cache_destroy(struct mlx5e_priv *priv)
{
	struct rhashtable *mf_ht = get_mf_ht(priv);

	device_remove_file(&priv->mdev->pdev->dev, counters_tc_ct_attrs);
	/* TODO: it does not make sense to process the remaining miniflows? */
	flush_workqueue(miniflow_wq);
	rhashtable_destroy(mf_ht);
	miniflow_free_current_miniflow();
	miniflow_cache_put();
}

static int miniflow_extract_tuple(struct mlx5e_miniflow *miniflow,
				  struct sk_buff *skb)
{
	struct nf_conntrack_tuple *nf_tuple = &miniflow->tuple;
	struct mlx5_core_dev *mdev = miniflow->priv->mdev;
	struct iphdr *iph, _iph;
	struct udphdr *udph, _udph;
	struct tcphdr *tcph, _tcph;
	int ihl;

	if (skb->protocol != htons(ETH_P_IP) &&
	    skb->protocol != htons(ETH_P_IPV6))
		goto err;

	if (skb->protocol == htons(ETH_P_IPV6)) {
		mlx5_core_warn_once(mdev, "IPv6 is not supported\n");
		goto err;
	}

	iph = skb_header_pointer(skb, skb_network_offset(skb), sizeof(_iph), &_iph);
	if (iph == NULL)
		goto err;

	ihl = ip_hdrlen(skb);
	if (ihl > sizeof(struct iphdr)) {
		mlx5_core_warn_once(mdev, "IPv4 options are not supported\n");
		goto err;
	}

	if (iph->frag_off & htons(IP_MF | IP_OFFSET)) {
		mlx5_core_warn_once(mdev, "IP fragments are not supported\n");
		goto err;
	}

	nf_tuple->src.l3num = skb->protocol;
	nf_tuple->dst.protonum = iph->protocol;
	nf_tuple->src.u3.ip = iph->saddr;
	nf_tuple->dst.u3.ip = iph->daddr;

	switch (nf_tuple->dst.protonum) {
	case IPPROTO_TCP:
		tcph = skb_header_pointer(skb, skb_network_offset(skb) + ihl,
					  sizeof(_tcph), &_tcph);

		if (!tcph || tcph->fin || tcph->syn || tcph->rst)
			goto err;

		nf_tuple->src.u.all = tcph->source;
		nf_tuple->dst.u.all = tcph->dest;
	break;
	case IPPROTO_UDP:
		udph = skb_header_pointer(skb, skb_network_offset(skb) + ihl,
					  sizeof(_udph), &_udph);

		if (!udph)
			goto err;

		nf_tuple->src.u.all = udph->source;
		nf_tuple->dst.u.all = udph->dest;
	break;
	case IPPROTO_ICMP:
		mlx5_core_warn_once(mdev, "ICMP is not supported\n");
		goto err;
	default:
		mlx5_core_warn_once(mdev, "Proto %d is not supported\n",
				    nf_tuple->dst.protonum);
		goto err;
	}

	return 0;

err:
	return -1;
}

int miniflow_configure_ct(struct mlx5e_priv *priv,
			  struct tc_ct_offload *cto)
{
	struct mlx5e_miniflow *miniflow;
	struct mlx5e_ct_tuple *ct_tuple;
	unsigned long cookie;

	if (!tc_can_offload(priv->netdev))
		return -EOPNOTSUPP;

	cookie = (unsigned long) cto->tuple;

	miniflow = miniflow_read();
	if (!miniflow)
		return -1;

	if (miniflow->mf_ht != get_mf_ht(priv))
		return -1;

	if (miniflow->nr_flows == MINIFLOW_ABORT ||
	    unlikely(miniflow->nr_flows >= MINIFLOW_MAX_FLOWS) ||
	    !cookie)
		goto err;

	ct_tuple = miniflow_ct_tuple_alloc(miniflow);
	if (!ct_tuple)
		goto err;

	ct_tuple->net = cto->net;
	ct_tuple->zone = *cto->zone;
	ct_tuple->tuple = *cto->tuple;

	ct_tuple->nat = cto->nat;
	ct_tuple->ipv4 = cto->ipv4;
	ct_tuple->port = cto->port;
	ct_tuple->proto = cto->proto;

	ct_tuple->flow = NULL;

	miniflow_path_append_cookie(miniflow, cookie, MFC_CT_FLOW);
	return 0;

err:
	miniflow_abort(miniflow);
	return -1;
}

static int miniflow_merge_rand_check(void)
{
	unsigned int rand;
	unsigned int probability = atomic_read((atomic_t *)&ct_merger_probability);

	if (probability == 0)
		return 0;

	get_random_bytes(&rand, sizeof(unsigned int));

	if (rand < UINT_MAX / probability)
		return 0;

	return -1;
}

int miniflow_configure(struct mlx5e_priv *priv,
		       struct tc_miniflow_offload *mf)
{
	struct rhashtable *mf_ht = get_mf_ht(priv);
	struct sk_buff *skb = mf->skb;
	struct mlx5e_miniflow *miniflow = NULL;
	int err;

	if (!tc_can_offload(priv->netdev))
		return -EOPNOTSUPP;

	miniflow = miniflow_read();
	if (!miniflow) {
		miniflow = miniflow_alloc();
		if (!miniflow)
			return -ENOMEM;
		miniflow_init(miniflow, priv, mf_ht);
		miniflow_write(miniflow);
	}

	if (mf->chain_index == 0)
		miniflow_init(miniflow, priv, mf_ht);

	if (miniflow->nr_flows == MINIFLOW_ABORT)
		goto err;

	/* Last tunnel unset flow gives the correct priv so reset it */
	if (mf->last_flow && miniflow->priv != priv) {
		mf_ht = get_mf_ht(priv);
		miniflow->priv = priv;
		miniflow->mf_ht = mf_ht;
	}

	if (miniflow->mf_ht != mf_ht)
		return -1;

	/**
	 * In some conditions merged rule could have another action with drop.
	 * i.e. header rewrite + drop.
	 * Such rule doesn't make sense and also not supported.
	 * For simplicty we will not offload drop rules that are merged rules.
	 */
	if (mf->is_drop)
		goto err;

	/* "Simple" rules should be handled by the normal routines */
	if (miniflow->nr_flows == 0 && mf->last_flow)
		goto err;

	if (unlikely(miniflow->nr_flows >= MINIFLOW_MAX_FLOWS))
		goto err;

	if (!mf->cookie)
		goto err;

	if (miniflow->nr_flows == 0) {
		err = miniflow_extract_tuple(miniflow, skb);
		if (err)
			goto err;
	}

	miniflow_path_append_cookie(miniflow, mf->cookie, 0);

	if (!mf->last_flow)
		return 0;

	err = miniflow_merge_rand_check();
	if (err)
		goto err;

	if (miniflow_workqueue_busy())
		goto err;

	/* If rules in HW + rules in queue exceed the max value, then igore new one.
	 * Note the rules in queue could be the to_be_deleted rules. */
	if ((atomic_read(&currently_in_hw) +
	     atomic_read(&nr_of_mfe_in_queue)) >=
	    atomic_read((atomic_t *)&max_nr_mf))
		goto err;

	err = rhashtable_lookup_insert_fast(mf_ht, &miniflow->node, mf_ht_params);
	if (err)
		goto err;

	err = miniflow_merge(miniflow);
	if (err)
		goto err_work;

	miniflow_write(NULL);

	return 0;

err_work:
	rhashtable_remove_fast(mf_ht, &miniflow->node, mf_ht_params);
err:
	miniflow_abort(miniflow);
	return -1;
}

int ct_flow_offload_add(void *arg, struct list_head *head)
{
	struct mlx5e_tc_flow *flow = arg;

	list_add(&flow->nft_node, head);
	return 0;
}

void ct_flow_offload_get_stats(struct list_head *head, u64 *lastuse)
{
	struct mlx5e_tc_flow *flow, *tmp;

	*lastuse = 0;
	list_for_each_entry_safe(flow, tmp, head, nft_node) {
		struct mlx5_fc *counter = flow->dummy_counter;
		u64 bytes, packets, lastuse1;

		if (counter) {
			mlx5_fc_query_cached(counter, &bytes, &packets,
					     &lastuse1);
			*lastuse = max(*lastuse, lastuse1);
		}
	}
}

static void ct_flow_offload_del(struct mlx5e_tc_flow *flow)
{
	if (!test_and_set_bit(0, &flow->miniflow->cleanup))
		/* We already hold dep_lock, set flag to flase */
		mlx5e_flow_put_lock(flow->priv, flow, false);
}

int ct_flow_offload_destroy(struct list_head *head)
{
	struct mlx5e_tc_flow *flow, *n;

	list_for_each_entry_safe(flow, n, head, nft_node) {
		list_del_init(&flow->nft_node);
		ct_flow_offload_del(flow);
	}

	return 0;
}

#endif /* HAVE_MINIFLOW */
