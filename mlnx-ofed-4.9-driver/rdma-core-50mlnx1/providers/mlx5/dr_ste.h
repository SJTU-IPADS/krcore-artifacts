/*
 * Copyright (c) 2020, Mellanox Technologies. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *	Redistribution and use in source and binary forms, with or
 *	without modification, are permitted provided that the following
 *	conditions are met:
 *
 *	- Redistributions of source code must retain the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer.
 *
 *	- Redistributions in binary form must reproduce the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer in the documentation and/or other materials
 *	  provided with the distribution.
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

#ifndef	_DR_STE_
#define	_DR_STE_

#include <ccan/array_size.h>
#include "mlx5dv_dr.h"

#define IPV4_ETHERTYPE    0x0800
#define IPV6_ETHERTYPE    0x86DD
#define STE_IPV4          0x1
#define STE_IPV6          0x2
#define STE_TCP           0x1
#define STE_UDP           0x2
#define STE_SPI           0x3
#define IP_VERSION_IPV4   0x4
#define IP_VERSION_IPV6   0x6
#define IP_PROTOCOL_UDP   0x11
#define IP_PROTOCOL_TCP   0x06
#define IP_PROTOCOL_IPSEC 0x33
#define TCP_PROTOCOL      0x6
#define UDP_PROTOCOL      0x11
#define IPSEC_PROTOCOL    0x33
#define HDR_LEN_L2_MACS   0xC
#define HDR_LEN_L2_VLAN   0x4
#define HDR_LEN_L2_ETHER  0x2
#define HDR_LEN_L2        (HDR_LEN_L2_MACS + HDR_LEN_L2_ETHER)
#define HDR_LEN_L2_W_VLAN (HDR_LEN_L2 + HDR_LEN_L2_VLAN)

/* Read from layout struct */
#define DR_STE_GET(typ, p, fld) DEVX_GET(ste_##typ, p, fld)

/* Write to layout a value */
#define DR_STE_SET(typ, p, fld, v) DEVX_SET(ste_##typ, p, fld, v)

#define DR_STE_SET_BOOL(typ, p, fld, v) DEVX_SET(ste_##typ, p, fld, !!(v))

/* Set to STE a specific value using DR_STE_SET */
#define DR_STE_SET_VAL(lookup_type, tag, t_fname, spec, s_fname, value) do { \
	if ((spec)->s_fname) { \
		DR_STE_SET(lookup_type, tag, t_fname, value); \
		(spec)->s_fname = 0; \
	} \
} while (0)

/* Set to STE spec->s_fname to tag->t_fname */
#define DR_STE_SET_TAG(lookup_type, tag, t_fname, spec, s_fname) \
	DR_STE_SET_VAL(lookup_type, tag, t_fname, spec, s_fname, (spec)->s_fname);

/* Set to STE -1 to bit_mask->bm_fname and set spec->s_fname as used */
#define DR_STE_SET_MASK(lookup_type, bit_mask, bm_fname, spec, s_fname) \
	DR_STE_SET_VAL(lookup_type, bit_mask, bm_fname, spec, s_fname, -1);

/* Set to STE spec->s_fname to bit_mask->bm_fname and set spec->s_fname as used */
#define DR_STE_SET_MASK_V(lookup_type, bit_mask, bm_fname, spec, s_fname) \
	DR_STE_SET_VAL(lookup_type, bit_mask, bm_fname, spec, s_fname, (spec)->s_fname);

#define DR_STE_SET_TCP_FLAGS(lookup_type, tag, spec) do { \
	DR_STE_SET_BOOL(lookup_type, tag, tcp_ns, (spec)->tcp_flags & (1 << 8)); \
	DR_STE_SET_BOOL(lookup_type, tag, tcp_cwr, (spec)->tcp_flags & (1 << 7)); \
	DR_STE_SET_BOOL(lookup_type, tag, tcp_ece, (spec)->tcp_flags & (1 << 6)); \
	DR_STE_SET_BOOL(lookup_type, tag, tcp_urg, (spec)->tcp_flags & (1 << 5)); \
	DR_STE_SET_BOOL(lookup_type, tag, tcp_ack, (spec)->tcp_flags & (1 << 4)); \
	DR_STE_SET_BOOL(lookup_type, tag, tcp_psh, (spec)->tcp_flags & (1 << 3)); \
	DR_STE_SET_BOOL(lookup_type, tag, tcp_rst, (spec)->tcp_flags & (1 << 2)); \
	DR_STE_SET_BOOL(lookup_type, tag, tcp_syn, (spec)->tcp_flags & (1 << 1)); \
	DR_STE_SET_BOOL(lookup_type, tag, tcp_fin, (spec)->tcp_flags & (1 << 0)); \
} while (0)

#define DR_STE_SET_MPLS_MASK(lookup_type, mask, in_out, bit_mask) do { \
	DR_STE_SET_MASK_V(lookup_type, mask, mpls0_label, mask, \
			  in_out##_first_mpls_label);\
	DR_STE_SET_MASK_V(lookup_type, mask, mpls0_s_bos, mask, \
			  in_out##_first_mpls_s_bos); \
	DR_STE_SET_MASK_V(lookup_type, mask, mpls0_exp, mask, \
			  in_out##_first_mpls_exp); \
	DR_STE_SET_MASK_V(lookup_type, mask, mpls0_ttl, mask, \
			  in_out##_first_mpls_ttl); \
} while (0)

#define DR_STE_SET_MPLS_TAG(lookup_type, mask, in_out, tag) do { \
	DR_STE_SET_TAG(lookup_type, tag, mpls0_label, mask, \
		       in_out##_first_mpls_label);\
	DR_STE_SET_TAG(lookup_type, tag, mpls0_s_bos, mask, \
		       in_out##_first_mpls_s_bos); \
	DR_STE_SET_TAG(lookup_type, tag, mpls0_exp, mask, \
		       in_out##_first_mpls_exp); \
	DR_STE_SET_TAG(lookup_type, tag, mpls0_ttl, mask, \
		       in_out##_first_mpls_ttl); \
} while (0)

struct dr_hw_ste_format {
	uint8_t ctrl[DR_STE_SIZE_CTRL];
	uint8_t tag[DR_STE_SIZE_TAG];
	uint8_t mask[DR_STE_SIZE_MASK];
};

typedef int (*dr_ste_tag_func)(struct dr_match_param *value,
			       struct dr_ste_build *sb,
			       uint8_t *tag);
typedef void (*dr_ste_bitmask_func)(struct dr_match_param *value,
				    bool inner, uint8_t *bit_mask);
typedef int (*dr_ste_bitmask_int_func)(struct dr_match_param *value,
				       bool inner, uint8_t *bit_mask);
typedef void (*dr_ste_bitmask_no_inner_func)(struct dr_match_param *value,
					     uint8_t *bit_mask);
typedef int (*dr_ste_bitmask_no_inner_int_func)(struct dr_match_param *value,
						uint8_t *bit_mask);
typedef int (*dr_ste_bitmask_caps_func)(struct dr_match_param *mask,
					struct dr_devx_caps *caps,
					uint8_t *bit_mask);

struct dr_ste_ctx {
	dr_ste_bitmask_int_func build_eth_l2_src_des_bit_mask;
	dr_ste_tag_func build_eth_l2_src_des_tag;

	dr_ste_bitmask_func build_eth_l3_ipv4_5_tuple_bit_mask;
	dr_ste_tag_func build_eth_l3_ipv4_5_tuple_tag;

	dr_ste_bitmask_func build_eth_l2_src_bit_mask;
	dr_ste_tag_func build_eth_l2_src_tag;

	dr_ste_bitmask_func build_eth_l2_dst_bit_mask;
	dr_ste_tag_func build_eth_l2_dst_tag;

	dr_ste_bitmask_func build_eth_l2_tnl_bit_mask;
	dr_ste_tag_func build_eth_l2_tnl_tag;

	dr_ste_bitmask_func build_eth_l3_ipv4_misc_bit_mask;
	dr_ste_tag_func build_eth_l3_ipv4_misc_tag;

	dr_ste_bitmask_func build_ipv6_l3_l4_bit_mask;
	dr_ste_tag_func build_ipv6_l3_l4_tag;

	dr_ste_bitmask_func build_eth_l4_misc_bit_mask;
	dr_ste_tag_func build_eth_l4_misc_tag;

	dr_ste_bitmask_func build_mpls_bit_mask;
	dr_ste_tag_func build_mpls_tag;

	dr_ste_bitmask_func build_gre_bit_mask;
	dr_ste_tag_func build_gre_tag;

	dr_ste_bitmask_no_inner_func build_flex_parser_tnl_gtpu_bit_mask;
	dr_ste_tag_func build_flex_parser_tnl_gtpu_tag;

	dr_ste_bitmask_func build_flex_parser_0_bit_mask;
	dr_ste_tag_func build_flex_parser_0_tag;

	dr_ste_bitmask_caps_func build_flex_parser_1_bit_mask;
	dr_ste_tag_func build_flex_parser_1_tag;

	dr_ste_bitmask_no_inner_int_func build_src_gvmi_qpn_bit_mask;
	dr_ste_tag_func build_src_gvmi_qpn_tag;

	/* STE utilities */
	void (*ste_init)(uint8_t *hw_ste_p, uint8_t lu_type,
			 uint8_t entry_type, uint16_t gvmi);

	/* Getters and Setters */
	void (*set_next_lu_type)(uint8_t *hw_ste_p, uint8_t lu_type);

	void (*set_miss_addr)(uint8_t *hw_ste_p, uint64_t miss_addr);
	uint64_t (*get_miss_addr)(uint8_t *hw_ste_p);

	void (*set_hit_addr)(uint8_t *hw_ste_p, uint64_t icm_addr, uint32_t ht_size);

	void (*set_byte_mask)(uint8_t *hw_ste_p, uint16_t byte_mask);
	uint16_t (*get_byte_mask)(uint8_t *hw_ste_p);

	/* Action support */
	void (*set_actions_rx)(uint8_t *action_type_set,
			       uint8_t *hw_ste_arr,
			       struct dr_ste_actions_attr *attr,
			       uint32_t *added_stes);

	void (*set_actions_tx)(uint8_t *action_type_set,
			       uint8_t *hw_ste_arr,
			       struct dr_ste_actions_attr *attr,
			       uint32_t *added_stes);

	uint32_t modify_hdr_field_arr_sz;
	const struct dr_ste_modify_hdr_hw_field *modify_hdr_field_arr;

	void (*set_set_action)(uint8_t *hw_action,
			       uint8_t hw_field,
			       uint8_t shifter,
			       uint8_t length,
			       uint32_t data);
	void (*set_add_action)(uint8_t *hw_action,
			       uint8_t hw_field,
			       uint8_t shifter,
			       uint8_t length,
			       uint32_t data);
	void (*set_copy_action)(uint8_t *hw_action,
			        uint8_t dst_hw_field,
			        uint8_t dst_shifter,
			        uint8_t dst_len,
			        uint8_t src_hw_field,
			        uint8_t src_shifter);

	int (*set_decap_l3_action_list)(void *data, uint32_t data_sz,
					uint8_t *hw_action, uint32_t hw_action_sz,
					uint16_t *used_hw_action_num);

	/* Send support */
	void (*prepare_ste_info)(uint8_t *hw_ste, uint32_t size);
};

struct dr_ste_ctx *dr_ste_init_ctx_v0(void);
struct dr_ste_ctx *dr_ste_init_ctx_v1(void);

#endif
