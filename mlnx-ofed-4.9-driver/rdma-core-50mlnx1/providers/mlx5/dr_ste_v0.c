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

#include "dr_ste.h"

#define DR_STE_ENABLE_FLOW_TAG (1 << 31)

#define DR_STE_IS_OUTER_MPLS_OVER_GRE_SET(_misc) (\
	(_misc)->outer_first_mpls_over_gre_label || \
	(_misc)->outer_first_mpls_over_gre_exp || \
	(_misc)->outer_first_mpls_over_gre_s_bos || \
	(_misc)->outer_first_mpls_over_gre_ttl)

#define DR_STE_IS_OUTER_MPLS_OVER_UDP_SET(_misc) (\
	(_misc)->outer_first_mpls_over_udp_label || \
	(_misc)->outer_first_mpls_over_udp_exp || \
	(_misc)->outer_first_mpls_over_udp_s_bos || \
	(_misc)->outer_first_mpls_over_udp_ttl)

enum dr_ste_hw_tunl_action {
	DR_STE_TUNL_ACTION_NONE		= 0,
	DR_STE_TUNL_ACTION_ENABLE	= 1,
	DR_STE_TUNL_ACTION_DECAP	= 2,
	DR_STE_TUNL_ACTION_L3_DECAP	= 3,
	DR_STE_TUNL_ACTION_POP_VLAN	= 4,
};

enum dr_ste_hw_action_type {
	DR_STE_ACTION_TYPE_PUSH_VLAN	= 1,
	DR_STE_ACTION_TYPE_ENCAP_L3	= 3,
	DR_STE_ACTION_TYPE_ENCAP	= 4,
};

enum {
	MLX5_DR_ACTION_MDFY_HW_OP_COPY		= 0x1,
	MLX5_DR_ACTION_MDFY_HW_OP_SET		= 0x2,
	MLX5_DR_ACTION_MDFY_HW_OP_ADD		= 0x3,
};

enum {
	MLX5_DR_ACTION_MDFY_HW_FLD_L2_0		= 0,
	MLX5_DR_ACTION_MDFY_HW_FLD_L2_1		= 1,
	MLX5_DR_ACTION_MDFY_HW_FLD_L2_2		= 2,
	MLX5_DR_ACTION_MDFY_HW_FLD_L3_0		= 3,
	MLX5_DR_ACTION_MDFY_HW_FLD_L3_1		= 4,
	MLX5_DR_ACTION_MDFY_HW_FLD_L3_2		= 5,
	MLX5_DR_ACTION_MDFY_HW_FLD_L3_3		= 6,
	MLX5_DR_ACTION_MDFY_HW_FLD_L3_4		= 7,
	MLX5_DR_ACTION_MDFY_HW_FLD_L4_0		= 8,
	MLX5_DR_ACTION_MDFY_HW_FLD_L4_1		= 9,
	MLX5_DR_ACTION_MDFY_HW_FLD_MPLS		= 10,
	MLX5_DR_ACTION_MDFY_HW_FLD_L2_TNL_0	= 11,
	MLX5_DR_ACTION_MDFY_HW_FLD_REG_0	= 12,
	MLX5_DR_ACTION_MDFY_HW_FLD_REG_1	= 13,
	MLX5_DR_ACTION_MDFY_HW_FLD_REG_2	= 14,
	MLX5_DR_ACTION_MDFY_HW_FLD_REG_3	= 15,
	MLX5_DR_ACTION_MDFY_HW_FLD_L4_2		= 16,
	MLX5_DR_ACTION_MDFY_HW_FLD_FLEX_0	= 17,
	MLX5_DR_ACTION_MDFY_HW_FLD_FLEX_1	= 18,
	MLX5_DR_ACTION_MDFY_HW_FLD_FLEX_2	= 19,
	MLX5_DR_ACTION_MDFY_HW_FLD_FLEX_3	= 20,
	MLX5_DR_ACTION_MDFY_HW_FLD_L2_TNL_1	= 21,
	MLX5_DR_ACTION_MDFY_HW_FLD_METADATA	= 22,
	MLX5_DR_ACTION_MDFY_HW_FLD_RESERVED	= 23,
};

static const struct dr_ste_modify_hdr_hw_field dr_ste_hw_modify_hdr_field_arr[] = {
	[MLX5_ACTION_IN_FIELD_OUT_SMAC_47_16] = {
		.hw_field = MLX5_DR_ACTION_MDFY_HW_FLD_L2_1, .start = 16, .end = 47,
	},
	[MLX5_ACTION_IN_FIELD_OUT_SMAC_15_0] = {
		.hw_field = MLX5_DR_ACTION_MDFY_HW_FLD_L2_1, .start = 0, .end = 15,
	},
	[MLX5_ACTION_IN_FIELD_OUT_ETHERTYPE] = {
		.hw_field = MLX5_DR_ACTION_MDFY_HW_FLD_L2_2, .start = 32, .end = 47,
	},
	[MLX5_ACTION_IN_FIELD_OUT_DMAC_47_16] = {
		.hw_field = MLX5_DR_ACTION_MDFY_HW_FLD_L2_0, .start = 16, .end = 47,
	},
	[MLX5_ACTION_IN_FIELD_OUT_DMAC_15_0] = {
		.hw_field = MLX5_DR_ACTION_MDFY_HW_FLD_L2_0, .start = 0, .end = 15,
	},
	[MLX5_ACTION_IN_FIELD_OUT_IP_DSCP] = {
		.hw_field = MLX5_DR_ACTION_MDFY_HW_FLD_L3_1, .start = 0, .end = 5,
	},
	[MLX5_ACTION_IN_FIELD_OUT_TCP_FLAGS] = {
		.hw_field = MLX5_DR_ACTION_MDFY_HW_FLD_L4_0, .start = 48, .end = 56,
		.l4_type = MLX5_DR_ACTION_MDFY_HW_HDR_L4_TCP,
	},
	[MLX5_ACTION_IN_FIELD_OUT_TCP_SPORT] = {
		.hw_field = MLX5_DR_ACTION_MDFY_HW_FLD_L4_0, .start = 0, .end = 15,
		.l4_type = MLX5_DR_ACTION_MDFY_HW_HDR_L4_TCP,
	},
	[MLX5_ACTION_IN_FIELD_OUT_TCP_DPORT] = {
		.hw_field = MLX5_DR_ACTION_MDFY_HW_FLD_L4_0, .start = 16, .end = 31,
		.l4_type = MLX5_DR_ACTION_MDFY_HW_HDR_L4_TCP,
	},
	[MLX5_ACTION_IN_FIELD_OUT_IP_TTL] = {
		.hw_field = MLX5_DR_ACTION_MDFY_HW_FLD_L3_1, .start = 8, .end = 15,
		.l3_type = MLX5_DR_ACTION_MDFY_HW_HDR_L3_IPV4,
	},
	[MLX5_ACTION_IN_FIELD_OUT_IPV6_HOPLIMIT] = {
		.hw_field = MLX5_DR_ACTION_MDFY_HW_FLD_L3_1, .start = 8, .end = 15,
		.l3_type = MLX5_DR_ACTION_MDFY_HW_HDR_L3_IPV6,
	},
	[MLX5_ACTION_IN_FIELD_OUT_UDP_SPORT] = {
		.hw_field = MLX5_DR_ACTION_MDFY_HW_FLD_L4_0, .start = 0, .end = 15,
		.l4_type = MLX5_DR_ACTION_MDFY_HW_HDR_L4_UDP,
	},
	[MLX5_ACTION_IN_FIELD_OUT_UDP_DPORT] = {
		.hw_field = MLX5_DR_ACTION_MDFY_HW_FLD_L4_0, .start = 16, .end = 31,
		.l4_type = MLX5_DR_ACTION_MDFY_HW_HDR_L4_UDP,
	},
	[MLX5_ACTION_IN_FIELD_OUT_SIPV6_127_96] = {
		.hw_field = MLX5_DR_ACTION_MDFY_HW_FLD_L3_3, .start = 32, .end = 63,
		.l3_type = MLX5_DR_ACTION_MDFY_HW_HDR_L3_IPV6,
	},
	[MLX5_ACTION_IN_FIELD_OUT_SIPV6_95_64] = {
		.hw_field = MLX5_DR_ACTION_MDFY_HW_FLD_L3_3, .start = 0, .end = 31,
		.l3_type = MLX5_DR_ACTION_MDFY_HW_HDR_L3_IPV6,
	},
	[MLX5_ACTION_IN_FIELD_OUT_SIPV6_63_32] = {
		.hw_field = MLX5_DR_ACTION_MDFY_HW_FLD_L3_4, .start = 32, .end = 63,
		.l3_type = MLX5_DR_ACTION_MDFY_HW_HDR_L3_IPV6,
	},
	[MLX5_ACTION_IN_FIELD_OUT_SIPV6_31_0] = {
		.hw_field = MLX5_DR_ACTION_MDFY_HW_FLD_L3_4, .start = 0, .end = 31,
		.l3_type = MLX5_DR_ACTION_MDFY_HW_HDR_L3_IPV6,
	},
	[MLX5_ACTION_IN_FIELD_OUT_DIPV6_127_96] = {
		.hw_field = MLX5_DR_ACTION_MDFY_HW_FLD_L3_0, .start = 32, .end = 63,
		.l3_type = MLX5_DR_ACTION_MDFY_HW_HDR_L3_IPV6,
	},
	[MLX5_ACTION_IN_FIELD_OUT_DIPV6_95_64] = {
		.hw_field = MLX5_DR_ACTION_MDFY_HW_FLD_L3_0, .start = 0, .end = 31,
		.l3_type = MLX5_DR_ACTION_MDFY_HW_HDR_L3_IPV6,
	},
	[MLX5_ACTION_IN_FIELD_OUT_DIPV6_63_32] = {
		.hw_field = MLX5_DR_ACTION_MDFY_HW_FLD_L3_2, .start = 32, .end = 63,
		.l3_type = MLX5_DR_ACTION_MDFY_HW_HDR_L3_IPV6,
	},
	[MLX5_ACTION_IN_FIELD_OUT_DIPV6_31_0] = {
		.hw_field = MLX5_DR_ACTION_MDFY_HW_FLD_L3_2, .start = 0, .end = 31,
		.l3_type = MLX5_DR_ACTION_MDFY_HW_HDR_L3_IPV6,
	},
	[MLX5_ACTION_IN_FIELD_OUT_SIPV4] = {
		.hw_field = MLX5_DR_ACTION_MDFY_HW_FLD_L3_0, .start = 0, .end = 31,
		.l3_type = MLX5_DR_ACTION_MDFY_HW_HDR_L3_IPV4,
	},
	[MLX5_ACTION_IN_FIELD_OUT_DIPV4] = {
		.hw_field = MLX5_DR_ACTION_MDFY_HW_FLD_L3_0, .start = 32, .end = 63,
		.l3_type = MLX5_DR_ACTION_MDFY_HW_HDR_L3_IPV4,
	},
	[MLX5_ACTION_IN_FIELD_OUT_METADATA_REGA] = {
		.hw_field = MLX5_DR_ACTION_MDFY_HW_FLD_METADATA, .start = 0, .end = 31,
	},
	[MLX5_ACTION_IN_FIELD_OUT_METADATA_REGB] = {
		.hw_field = MLX5_DR_ACTION_MDFY_HW_FLD_METADATA, .start = 32, .end = 63,
	},
	[MLX5_ACTION_IN_FIELD_OUT_METADATA_REGC_0] = {
		.hw_field = MLX5_DR_ACTION_MDFY_HW_FLD_REG_0, .start = 32, .end = 63,
	},
	[MLX5_ACTION_IN_FIELD_OUT_METADATA_REGC_1] = {
		.hw_field = MLX5_DR_ACTION_MDFY_HW_FLD_REG_0, .start = 0, .end = 31,
	},
	[MLX5_ACTION_IN_FIELD_OUT_METADATA_REGC_2] = {
		.hw_field = MLX5_DR_ACTION_MDFY_HW_FLD_REG_1, .start = 32, .end = 63,
	},
	[MLX5_ACTION_IN_FIELD_OUT_METADATA_REGC_3] = {
		.hw_field = MLX5_DR_ACTION_MDFY_HW_FLD_REG_1, .start = 0, .end = 31,
	},
	[MLX5_ACTION_IN_FIELD_OUT_METADATA_REGC_4] = {
		.hw_field = MLX5_DR_ACTION_MDFY_HW_FLD_REG_2, .start = 32, .end = 63,
	},
	[MLX5_ACTION_IN_FIELD_OUT_METADATA_REGC_5] = {
		.hw_field = MLX5_DR_ACTION_MDFY_HW_FLD_REG_2, .start = 0, .end = 31,
	},
	[MLX5_ACTION_IN_FIELD_OUT_TCP_SEQ_NUM] = {
		.hw_field = MLX5_DR_ACTION_MDFY_HW_FLD_L4_1, .start = 32, .end = 63,
	},
	[MLX5_ACTION_IN_FIELD_OUT_TCP_ACK_NUM] = {
		.hw_field = MLX5_DR_ACTION_MDFY_HW_FLD_L4_1, .start = 0, .end = 31,
	},
	[MLX5_ACTION_IN_FIELD_OUT_FIRST_VID] = {
		.hw_field = MLX5_DR_ACTION_MDFY_HW_FLD_L2_2, .start = 0, .end = 15,
	},\
};

static void dr_ste_hw_set_entry_type(uint8_t *hw_ste_p, uint8_t entry_type)
{
	DR_STE_SET(general, hw_ste_p, entry_type, entry_type);
}

static uint8_t dr_ste_hw_get_entry_type(uint8_t *hw_ste_p)
{
	return DR_STE_GET(general, hw_ste_p, entry_type);
}

static void dr_ste_hw_set_hit_gvmi(uint8_t *hw_ste_p, uint16_t gvmi)
{
	DR_STE_SET(general, hw_ste_p, next_table_base_63_48, gvmi);
}

static void dr_ste_hw_set_miss_addr(uint8_t *hw_ste_p, uint64_t miss_addr)
{
	uint64_t index = miss_addr >> 6;

	/* Miss address for TX and RX STEs located in the same offsets */
	DR_STE_SET(rx_steering_mult, hw_ste_p, miss_address_39_32, index >> 26);
	DR_STE_SET(rx_steering_mult, hw_ste_p, miss_address_31_6, index);
}

static uint64_t dr_ste_hw_get_miss_addr(uint8_t *hw_ste_p)
{
	uint64_t index =
		(DR_STE_GET(rx_steering_mult, hw_ste_p, miss_address_31_6) |
		 DR_STE_GET(rx_steering_mult, hw_ste_p, miss_address_39_32) << 26);

	return index << 6;
}

static void dr_ste_hw_set_byte_mask(uint8_t *hw_ste_p, uint16_t byte_mask)
{
	DR_STE_SET(general, hw_ste_p, byte_mask, byte_mask);
}

static uint16_t dr_ste_hw_get_byte_mask(uint8_t *hw_ste_p)
{
	return DR_STE_GET(general, hw_ste_p, byte_mask);
}

static void dr_ste_hw_set_lu_type(uint8_t *hw_ste_p, uint8_t lu_type)
{
	DR_STE_SET(general, hw_ste_p, entry_sub_type, lu_type);
}

static void dr_ste_hw_set_next_lu_type(uint8_t *hw_ste_p, uint8_t lu_type)
{
	DR_STE_SET(general, hw_ste_p, next_lu_type, lu_type);
}

static void dr_ste_hw_set_hit_addr(uint8_t *hw_ste_p, uint64_t icm_addr, uint32_t ht_size)
{
	uint64_t index = (icm_addr >> 5) | ht_size;

	DR_STE_SET(general, hw_ste_p, next_table_base_39_32_size, index >> 27);
	DR_STE_SET(general, hw_ste_p, next_table_base_31_5_size, index);
}

static void dr_ste_hw_init(uint8_t *hw_ste_p, uint8_t lu_type,
			   uint8_t entry_type, uint16_t gvmi)
{
	DR_STE_SET(general, hw_ste_p, entry_type, entry_type);
	dr_ste_hw_set_lu_type(hw_ste_p, lu_type);
	dr_ste_hw_set_next_lu_type(hw_ste_p, DR_STE_LU_TYPE_DONT_CARE);

	DR_STE_SET(rx_steering_mult, hw_ste_p, gvmi, gvmi);
	DR_STE_SET(rx_steering_mult, hw_ste_p, next_table_base_63_48, gvmi);
	DR_STE_SET(rx_steering_mult, hw_ste_p, miss_address_63_48, gvmi);
}

static void dr_ste_hw_arr_init_next(uint8_t **last_hw_ste,
				    uint32_t *added_stes,
				    enum dr_ste_entry_type entry_type,
				    uint16_t gvmi)
{
	(*added_stes)++;
	*last_hw_ste += DR_STE_SIZE;
	dr_ste_hw_init(*last_hw_ste, DR_STE_LU_TYPE_DONT_CARE, entry_type, gvmi);
}

static void dr_ste_hw_rx_set_flow_tag(uint8_t *hw_ste_p, uint32_t flow_tag)
{
	DR_STE_SET(rx_steering_mult, hw_ste_p, qp_list_pointer,
		   DR_STE_ENABLE_FLOW_TAG | flow_tag);
}

static void dr_ste_hw_set_counter_id(uint8_t *hw_ste_p, uint32_t ctr_id)
{
	/* This can be used for both rx_steering_mult and for sx_transmit */
	DR_STE_SET(rx_steering_mult, hw_ste_p, counter_trigger_15_0, ctr_id);
	DR_STE_SET(rx_steering_mult, hw_ste_p, counter_trigger_23_16, ctr_id >> 16);
}

static void dr_ste_hw_set_go_back_bit(uint8_t *hw_ste_p)
{
	DR_STE_SET(sx_transmit, hw_ste_p, go_back, 1);
}

static void dr_ste_hw_set_tx_push_vlan(uint8_t *hw_ste_p,
				       uint32_t vlan_hdr,
				       bool go_back)
{
	DR_STE_SET(sx_transmit, hw_ste_p, action_type,
		   DR_STE_ACTION_TYPE_PUSH_VLAN);
	DR_STE_SET(sx_transmit, hw_ste_p, encap_pointer_vlan_data, vlan_hdr);
	/* Due to HW limitation we need to set this bit, otherwise reformat +
	 * push vlan will not work.
	 */
	if (go_back)
		dr_ste_hw_set_go_back_bit(hw_ste_p);
}

static void dr_ste_hw_set_tx_encap(void *hw_ste_p,
				   uint32_t reformat_id,
				   int size, bool encap_l3)
{
	DR_STE_SET(sx_transmit, hw_ste_p, action_type,
		   encap_l3 ? DR_STE_ACTION_TYPE_ENCAP_L3 : DR_STE_ACTION_TYPE_ENCAP);
	/* The hardware expects here size in words (2 byte) */
	DR_STE_SET(sx_transmit, hw_ste_p, action_description, size / 2);
	DR_STE_SET(sx_transmit, hw_ste_p, encap_pointer_vlan_data, reformat_id);
}

static void dr_ste_hw_set_rx_decap(uint8_t *hw_ste_p)
{
	DR_STE_SET(rx_steering_mult, hw_ste_p, tunneling_action,
		   DR_STE_TUNL_ACTION_DECAP);
}

static void dr_ste_hw_set_rx_pop_vlan(uint8_t *hw_ste_p)
{
	DR_STE_SET(rx_steering_mult, hw_ste_p, tunneling_action,
		   DR_STE_TUNL_ACTION_POP_VLAN);
}

static void dr_ste_hw_set_rx_decap_l3(uint8_t *hw_ste_p, bool vlan)
{
	DR_STE_SET(rx_steering_mult, hw_ste_p, tunneling_action,
		   DR_STE_TUNL_ACTION_L3_DECAP);
	DR_STE_SET(modify_packet, hw_ste_p, action_description, vlan ? 1 : 0);
}

static void dr_ste_hw_set_rewrite_actions(uint8_t *hw_ste_p,
					  uint16_t num_of_actions,
					  uint32_t re_write_index)
{
	DR_STE_SET(modify_packet, hw_ste_p, number_of_re_write_actions,
		   num_of_actions);
	DR_STE_SET(modify_packet, hw_ste_p, header_re_write_actions_pointer,
		   re_write_index);
}

static void dr_ste_hw_set_actions_tx(uint8_t *action_type_set,
				     uint8_t *hw_ste_arr,
				     struct dr_ste_actions_attr *attr,
				     uint32_t *added_stes)
{
	bool encap = action_type_set[DR_ACTION_TYP_L2_TO_TNL_L2] ||
		action_type_set[DR_ACTION_TYP_L2_TO_TNL_L3];

	/* We want to make sure the modify header comes before L2
	 * encapsulation. The reason for that is that we support
	 * modify headers for outer headers only
	 */
	if (action_type_set[DR_ACTION_TYP_MODIFY_HDR]) {
		dr_ste_hw_set_entry_type(hw_ste_arr, DR_STE_TYPE_MODIFY_PKT);
		dr_ste_hw_set_rewrite_actions(hw_ste_arr,
					      attr->modify_actions,
					      attr->modify_index);
	}

	if (action_type_set[DR_ACTION_TYP_PUSH_VLAN]) {
		int i;

		for (i = 0; i < attr->vlans.count; i++) {
			if (i || action_type_set[DR_ACTION_TYP_MODIFY_HDR])
				dr_ste_hw_arr_init_next(&hw_ste_arr,
							added_stes,
							DR_STE_TYPE_TX,
							attr->gvmi);

			dr_ste_hw_set_tx_push_vlan(hw_ste_arr,
						   attr->vlans.headers[i],
						   encap);
		}
	}

	if (encap) {
		/* Modify header and encapsulation require a different STEs.
		 * Since modify header STE format doesn't support encapsulation
		 * tunneling_action.
		 */
		if (action_type_set[DR_ACTION_TYP_MODIFY_HDR] ||
		    action_type_set[DR_ACTION_TYP_PUSH_VLAN])
			dr_ste_hw_arr_init_next(&hw_ste_arr,
						added_stes,
						DR_STE_TYPE_TX,
						attr->gvmi);

		dr_ste_hw_set_tx_encap(hw_ste_arr,
				       attr->reformat_id,
				       attr->reformat_size,
				       action_type_set[DR_ACTION_TYP_L2_TO_TNL_L3]);
		/* Whenever prio_tag_required enabled, we can be sure that the
		 * previous table (ACL) already push vlan to our packet,
		 * And due to HW limitation we need to set this bit, otherwise
		 * push vlan + reformat will not work.
		 */
		if (attr->prio_tag_required)
			dr_ste_hw_set_go_back_bit(hw_ste_arr);
	}

	if (action_type_set[DR_ACTION_TYP_CTR])
		dr_ste_hw_set_counter_id(hw_ste_arr, attr->ctr_id);

	dr_ste_hw_set_hit_gvmi(hw_ste_arr, attr->hit_gvmi);
	dr_ste_hw_set_hit_addr(hw_ste_arr, attr->final_icm_addr, 1);
}

static void dr_ste_hw_set_actions_rx(uint8_t *action_type_set,
				     uint8_t *hw_ste_arr,
				     struct dr_ste_actions_attr *attr,
				     uint32_t *added_stes)
{
	if (action_type_set[DR_ACTION_TYP_CTR])
		dr_ste_hw_set_counter_id(hw_ste_arr, attr->ctr_id);

	if (action_type_set[DR_ACTION_TYP_TNL_L3_TO_L2]) {
		dr_ste_hw_set_entry_type(hw_ste_arr, DR_STE_TYPE_MODIFY_PKT);
		dr_ste_hw_set_rx_decap_l3(hw_ste_arr, attr->decap_with_vlan);
		dr_ste_hw_set_rewrite_actions(hw_ste_arr,
					      attr->decap_actions,
					      attr->decap_index);
	}

	if (action_type_set[DR_ACTION_TYP_TNL_L2_TO_L2])
		dr_ste_hw_set_rx_decap(hw_ste_arr);

	if (action_type_set[DR_ACTION_TYP_POP_VLAN]) {
		int i;

		for (i = 0; i < attr->vlans.count; i++) {
			if (i ||
			    action_type_set[DR_ACTION_TYP_TNL_L2_TO_L2] ||
			    action_type_set[DR_ACTION_TYP_TNL_L3_TO_L2])
				dr_ste_hw_arr_init_next(&hw_ste_arr,
							added_stes,
							DR_STE_TYPE_RX,
							attr->gvmi);

			dr_ste_hw_set_rx_pop_vlan(hw_ste_arr);
		}
	}

	if (action_type_set[DR_ACTION_TYP_MODIFY_HDR]) {
		if (dr_ste_hw_get_entry_type(hw_ste_arr) == DR_STE_TYPE_MODIFY_PKT)
			dr_ste_hw_arr_init_next(&hw_ste_arr,
						added_stes,
						DR_STE_TYPE_MODIFY_PKT,
						attr->gvmi);
		else
			dr_ste_hw_set_entry_type(hw_ste_arr, DR_STE_TYPE_MODIFY_PKT);

		dr_ste_hw_set_rewrite_actions(hw_ste_arr,
					      attr->modify_actions,
					      attr->modify_index);
	}

	if (action_type_set[DR_ACTION_TYP_TAG]) {
		if (dr_ste_hw_get_entry_type(hw_ste_arr) == DR_STE_TYPE_MODIFY_PKT)
			dr_ste_hw_arr_init_next(&hw_ste_arr,
						added_stes,
						DR_STE_TYPE_RX,
						attr->gvmi);

		dr_ste_hw_rx_set_flow_tag(hw_ste_arr, attr->flow_tag);
	}

	dr_ste_hw_set_hit_gvmi(hw_ste_arr, attr->hit_gvmi);
	dr_ste_hw_set_hit_addr(hw_ste_arr, attr->final_icm_addr, 1);
}

static void dr_ste_hw_set_set_action(uint8_t *hw_action,
				     uint8_t hw_field,
				     uint8_t shifter,
				     uint8_t length,
				     uint32_t data)
{
	length = (length == 32) ? 0 : length;
	DEVX_SET(dr_action_hw_set, hw_action, opcode, MLX5_DR_ACTION_MDFY_HW_OP_SET);
	DEVX_SET(dr_action_hw_set, hw_action, destination_field_code, hw_field);
	DEVX_SET(dr_action_hw_set, hw_action, destination_left_shifter, shifter);
	DEVX_SET(dr_action_hw_set, hw_action, destination_length, length);
	DEVX_SET(dr_action_hw_set, hw_action, inline_data, data);
}

static void dr_ste_hw_set_add_action(uint8_t *hw_action,
				     uint8_t hw_field,
				     uint8_t shifter,
				     uint8_t length,
				     uint32_t data)
{
	length = (length == 32) ? 0 : length;
	DEVX_SET(dr_action_hw_set, hw_action, opcode, MLX5_DR_ACTION_MDFY_HW_OP_ADD);
	DEVX_SET(dr_action_hw_set, hw_action, destination_field_code, hw_field);
	DEVX_SET(dr_action_hw_set, hw_action, destination_left_shifter, shifter);
	DEVX_SET(dr_action_hw_set, hw_action, destination_length, length);
	DEVX_SET(dr_action_hw_set, hw_action, inline_data, data);
}

static void dr_ste_hw_set_copy_action(uint8_t *hw_action,
				      uint8_t dst_hw_field,
				      uint8_t dst_shifter,
				      uint8_t dst_len,
				      uint8_t src_hw_field,
				      uint8_t src_shifter)
{
	DEVX_SET(dr_action_hw_copy, hw_action, opcode, MLX5_DR_ACTION_MDFY_HW_OP_COPY);
	DEVX_SET(dr_action_hw_copy, hw_action, destination_field_code, dst_hw_field);
	DEVX_SET(dr_action_hw_copy, hw_action, destination_left_shifter, dst_shifter);
	DEVX_SET(dr_action_hw_copy, hw_action, destination_length, dst_len);
	DEVX_SET(dr_action_hw_copy, hw_action, source_field_code, src_hw_field);
	DEVX_SET(dr_action_hw_copy, hw_action, source_right_shifter, src_shifter);
}

#define DR_STE_DECAP_L3_MIN_ACTION_NUM	5

static int
dr_ste_hw_set_decap_l3_action_list(void *data, uint32_t data_sz,
				   uint8_t *hw_action, uint32_t hw_action_sz,
				   uint16_t *used_hw_action_num)
{
	struct mlx5_ifc_l2_hdr_bits *l2_hdr = data;
	uint32_t hw_action_num;
	int required_actions;
	uint32_t hdr_fld_4b;
	uint16_t hdr_fld_2b;
	uint16_t vlan_type;
	bool vlan;

	vlan = (data_sz != HDR_LEN_L2);
	hw_action_num = hw_action_sz / DEVX_ST_SZ_BYTES(dr_action_hw_set);
	required_actions = DR_STE_DECAP_L3_MIN_ACTION_NUM + !!vlan;

	if (hw_action_num < required_actions) {
		errno = ENOMEM;
		return errno;
	}

	/* dmac_47_16 */
	DEVX_SET(dr_action_hw_set, hw_action, opcode, MLX5_DR_ACTION_MDFY_HW_OP_SET);
	DEVX_SET(dr_action_hw_set, hw_action, destination_length, 0);
	DEVX_SET(dr_action_hw_set, hw_action, destination_field_code, MLX5_DR_ACTION_MDFY_HW_FLD_L2_0);
	DEVX_SET(dr_action_hw_set, hw_action, destination_left_shifter, 16);
	hdr_fld_4b = DEVX_GET(l2_hdr, l2_hdr, dmac_47_16);
	DEVX_SET(dr_action_hw_set, hw_action, inline_data, hdr_fld_4b);
	hw_action += DEVX_ST_SZ_BYTES(dr_action_hw_set);

	/* smac_47_16 */
	DEVX_SET(dr_action_hw_set, hw_action, opcode, MLX5_DR_ACTION_MDFY_HW_OP_SET);
	DEVX_SET(dr_action_hw_set, hw_action, destination_length, 0);
	DEVX_SET(dr_action_hw_set, hw_action, destination_field_code, MLX5_DR_ACTION_MDFY_HW_FLD_L2_1);
	DEVX_SET(dr_action_hw_set, hw_action, destination_left_shifter, 16);
	hdr_fld_4b = (DEVX_GET(l2_hdr, l2_hdr, smac_31_0) >> 16 |
		      DEVX_GET(l2_hdr, l2_hdr, smac_47_32) << 16);
	DEVX_SET(dr_action_hw_set, hw_action, inline_data, hdr_fld_4b);
	hw_action += DEVX_ST_SZ_BYTES(dr_action_hw_set);

	/* dmac_15_0 */
	DEVX_SET(dr_action_hw_set, hw_action, opcode, MLX5_DR_ACTION_MDFY_HW_OP_SET);
	DEVX_SET(dr_action_hw_set, hw_action, destination_length, 16);
	DEVX_SET(dr_action_hw_set, hw_action, destination_field_code, MLX5_DR_ACTION_MDFY_HW_FLD_L2_0);
	DEVX_SET(dr_action_hw_set, hw_action, destination_left_shifter, 0);
	hdr_fld_2b = DEVX_GET(l2_hdr, l2_hdr, dmac_15_0);
	DEVX_SET(dr_action_hw_set, hw_action, inline_data, hdr_fld_2b);
	hw_action += DEVX_ST_SZ_BYTES(dr_action_hw_set);

	/* ethertype + (optional) vlan */
	DEVX_SET(dr_action_hw_set, hw_action, opcode, MLX5_DR_ACTION_MDFY_HW_OP_SET);
	DEVX_SET(dr_action_hw_set, hw_action, destination_field_code, MLX5_DR_ACTION_MDFY_HW_FLD_L2_2);
	DEVX_SET(dr_action_hw_set, hw_action, destination_left_shifter, 32);
	if (!vlan) {
		hdr_fld_2b = DEVX_GET(l2_hdr, l2_hdr, ethertype);
		DEVX_SET(dr_action_hw_set, hw_action, inline_data, hdr_fld_2b);
		DEVX_SET(dr_action_hw_set, hw_action, destination_length, 16);
	} else {
		hdr_fld_2b = DEVX_GET(l2_hdr, l2_hdr, ethertype);
		vlan_type = hdr_fld_2b == SVLAN_ETHERTYPE ? DR_STE_SVLAN : DR_STE_CVLAN;
		hdr_fld_2b = DEVX_GET(l2_hdr, l2_hdr, vlan);
		hdr_fld_4b = (vlan_type << 16) | hdr_fld_2b;
		DEVX_SET(dr_action_hw_set, hw_action, inline_data, hdr_fld_4b);
		DEVX_SET(dr_action_hw_set, hw_action, destination_length, 18);
	}
	hw_action += DEVX_ST_SZ_BYTES(dr_action_hw_set);

	/* smac_15_0 */
	DEVX_SET(dr_action_hw_set, hw_action, opcode, MLX5_DR_ACTION_MDFY_HW_OP_SET);
	DEVX_SET(dr_action_hw_set, hw_action, destination_length, 16);
	DEVX_SET(dr_action_hw_set, hw_action, destination_field_code, MLX5_DR_ACTION_MDFY_HW_FLD_L2_1);
	DEVX_SET(dr_action_hw_set, hw_action, destination_left_shifter, 0);
	hdr_fld_2b = DEVX_GET(l2_hdr, l2_hdr, smac_31_0);
	DEVX_SET(dr_action_hw_set, hw_action, inline_data, hdr_fld_2b);
	hw_action += DEVX_ST_SZ_BYTES(dr_action_hw_set);

	if (vlan) {
		DEVX_SET(dr_action_hw_set, hw_action, opcode, MLX5_DR_ACTION_MDFY_HW_OP_SET);
		hdr_fld_2b = DEVX_GET(l2_hdr, l2_hdr, vlan_type);
		DEVX_SET(dr_action_hw_set, hw_action, inline_data, hdr_fld_2b);
		DEVX_SET(dr_action_hw_set, hw_action, destination_length, 16);
		DEVX_SET(dr_action_hw_set, hw_action, destination_field_code, MLX5_DR_ACTION_MDFY_HW_FLD_L2_2);
		DEVX_SET(dr_action_hw_set, hw_action, destination_left_shifter, 0);
	}

	*used_hw_action_num = required_actions;

	return 0;
}

static int dr_ste_build_eth_l2_src_des_bit_mask(struct dr_match_param *value,
						bool inner, uint8_t *bit_mask)
{
	struct dr_match_spec *mask = inner ? &value->inner : &value->outer;

	DR_STE_SET_MASK_V(eth_l2_src_dst, bit_mask, dmac_47_16, mask, dmac_47_16);
	DR_STE_SET_MASK_V(eth_l2_src_dst, bit_mask, dmac_15_0, mask, dmac_15_0);

	if (mask->smac_47_16 || mask->smac_15_0) {
		DR_STE_SET(eth_l2_src_dst, bit_mask, smac_47_32,
			   mask->smac_47_16 >> 16);
		DR_STE_SET(eth_l2_src_dst, bit_mask, smac_31_0,
			   mask->smac_47_16 << 16 | mask->smac_15_0);
		mask->smac_47_16 = 0;
		mask->smac_15_0 = 0;
	}

	DR_STE_SET_MASK_V(eth_l2_src_dst, bit_mask, first_vlan_id, mask, first_vid);
	DR_STE_SET_MASK_V(eth_l2_src_dst, bit_mask, first_cfi, mask, first_cfi);
	DR_STE_SET_MASK_V(eth_l2_src_dst, bit_mask, first_priority, mask, first_prio);
	DR_STE_SET_MASK(eth_l2_src_dst, bit_mask, l3_type, mask, ip_version);

	if (mask->cvlan_tag) {
		DR_STE_SET(eth_l2_src_dst, bit_mask, first_vlan_qualifier, -1);
		mask->cvlan_tag = 0;
	} else if (mask->svlan_tag) {
		DR_STE_SET(eth_l2_src_dst, bit_mask, first_vlan_qualifier, -1);
		mask->svlan_tag = 0;
	}

	if (mask->cvlan_tag || mask->svlan_tag) {
		errno = EINVAL;
		return errno;
	}

	return 0;
}

static int dr_ste_build_eth_l2_src_des_tag(struct dr_match_param *value,
					   struct dr_ste_build *sb,
					   uint8_t *tag)
{
	struct dr_match_spec *spec = sb->inner ? &value->inner : &value->outer;

	DR_STE_SET_TAG(eth_l2_src_dst, tag, dmac_47_16, spec, dmac_47_16);
	DR_STE_SET_TAG(eth_l2_src_dst, tag, dmac_15_0, spec, dmac_15_0);

	if (spec->smac_47_16 || spec->smac_15_0) {
		DR_STE_SET(eth_l2_src_dst, tag, smac_47_32,
			   spec->smac_47_16 >> 16);
		DR_STE_SET(eth_l2_src_dst, tag, smac_31_0,
			   spec->smac_47_16 << 16 | spec->smac_15_0);
		spec->smac_47_16 = 0;
		spec->smac_15_0 = 0;
	}

	if (spec->ip_version) {
		if (spec->ip_version == IP_VERSION_IPV4) {
			DR_STE_SET(eth_l2_src_dst, tag, l3_type, STE_IPV4);
			spec->ip_version = 0;
		} else if (spec->ip_version == IP_VERSION_IPV6) {
			DR_STE_SET(eth_l2_src_dst, tag, l3_type, STE_IPV6);
			spec->ip_version = 0;
		} else {
			errno = EINVAL;
			return errno;
		}
	}

	DR_STE_SET_TAG(eth_l2_src_dst, tag, first_vlan_id, spec, first_vid);
	DR_STE_SET_TAG(eth_l2_src_dst, tag, first_cfi, spec, first_cfi);
	DR_STE_SET_TAG(eth_l2_src_dst, tag, first_priority, spec, first_prio);

	if (spec->cvlan_tag) {
		DR_STE_SET(eth_l2_src_dst, tag, first_vlan_qualifier, DR_STE_CVLAN);
		spec->cvlan_tag = 0;
	} else if (spec->svlan_tag) {
		DR_STE_SET(eth_l2_src_dst, tag, first_vlan_qualifier, DR_STE_SVLAN);
		spec->svlan_tag = 0;
	}
	return 0;
}

static void dr_ste_build_eth_l3_ipv4_5_tuple_bit_mask(struct dr_match_param *value,
						      bool inner,
						      uint8_t *bit_mask)
{
	struct dr_match_spec *mask = inner ? &value->inner : &value->outer;

	DR_STE_SET_MASK_V(eth_l3_ipv4_5_tuple, bit_mask, destination_address, mask, dst_ip_31_0);
	DR_STE_SET_MASK_V(eth_l3_ipv4_5_tuple, bit_mask, source_address, mask, src_ip_31_0);
	DR_STE_SET_MASK_V(eth_l3_ipv4_5_tuple, bit_mask, destination_port, mask, tcp_dport);
	DR_STE_SET_MASK_V(eth_l3_ipv4_5_tuple, bit_mask, destination_port, mask, udp_dport);
	DR_STE_SET_MASK_V(eth_l3_ipv4_5_tuple, bit_mask, source_port, mask, tcp_sport);
	DR_STE_SET_MASK_V(eth_l3_ipv4_5_tuple, bit_mask, source_port, mask, udp_sport);
	DR_STE_SET_MASK_V(eth_l3_ipv4_5_tuple, bit_mask, protocol, mask, ip_protocol);
	DR_STE_SET_MASK_V(eth_l3_ipv4_5_tuple, bit_mask, fragmented, mask, frag);
	DR_STE_SET_MASK_V(eth_l3_ipv4_5_tuple, bit_mask, dscp, mask, ip_dscp);
	DR_STE_SET_MASK_V(eth_l3_ipv4_5_tuple, bit_mask, ecn, mask, ip_ecn);

	if (mask->tcp_flags) {
		DR_STE_SET_TCP_FLAGS(eth_l3_ipv4_5_tuple, bit_mask, mask);
		mask->tcp_flags = 0;
	}
}

static int dr_ste_build_eth_l3_ipv4_5_tuple_tag(struct dr_match_param *value,
						struct dr_ste_build *sb,
						uint8_t *tag)
{
	struct dr_match_spec *spec = sb->inner ? &value->inner : &value->outer;

	DR_STE_SET_TAG(eth_l3_ipv4_5_tuple, tag, destination_address, spec, dst_ip_31_0);
	DR_STE_SET_TAG(eth_l3_ipv4_5_tuple, tag, source_address, spec, src_ip_31_0);
	DR_STE_SET_TAG(eth_l3_ipv4_5_tuple, tag, destination_port, spec, tcp_dport);
	DR_STE_SET_TAG(eth_l3_ipv4_5_tuple, tag, destination_port, spec, udp_dport);
	DR_STE_SET_TAG(eth_l3_ipv4_5_tuple, tag, source_port, spec, tcp_sport);
	DR_STE_SET_TAG(eth_l3_ipv4_5_tuple, tag, source_port, spec, udp_sport);
	DR_STE_SET_TAG(eth_l3_ipv4_5_tuple, tag, protocol, spec, ip_protocol);
	DR_STE_SET_TAG(eth_l3_ipv4_5_tuple, tag, fragmented, spec, frag);
	DR_STE_SET_TAG(eth_l3_ipv4_5_tuple, tag, dscp, spec, ip_dscp);
	DR_STE_SET_TAG(eth_l3_ipv4_5_tuple, tag, ecn, spec, ip_ecn);

	if (spec->tcp_flags) {
		DR_STE_SET_TCP_FLAGS(eth_l3_ipv4_5_tuple, tag, spec);
		spec->tcp_flags = 0;
	}

	return 0;
}

static void
dr_ste_build_eth_l2_src_or_dst_bit_mask(struct dr_match_param *value,
					bool inner, uint8_t *bit_mask)
{
	struct dr_match_spec *mask = inner ? &value->inner : &value->outer;
	struct dr_match_misc *misc_mask = &value->misc;

	DR_STE_SET_MASK_V(eth_l2_src, bit_mask, first_vlan_id, mask, first_vid);
	DR_STE_SET_MASK_V(eth_l2_src, bit_mask, first_cfi, mask, first_cfi);
	DR_STE_SET_MASK_V(eth_l2_src, bit_mask, first_priority, mask, first_prio);
	DR_STE_SET_MASK_V(eth_l2_src, bit_mask, ip_fragmented, mask, frag);
	DR_STE_SET_MASK_V(eth_l2_src, bit_mask, l3_ethertype, mask, ethertype);
	DR_STE_SET_MASK(eth_l2_src, bit_mask, l3_type, mask, ip_version);

	if (mask->svlan_tag || mask->cvlan_tag) {
		DR_STE_SET(eth_l2_src, bit_mask, first_vlan_qualifier, -1);
		mask->cvlan_tag = 0;
		mask->svlan_tag = 0;
	}

	if (inner) {
		if (misc_mask->inner_second_cvlan_tag ||
		    misc_mask->inner_second_svlan_tag) {
			DR_STE_SET(eth_l2_src, bit_mask, second_vlan_qualifier, -1);
			misc_mask->inner_second_cvlan_tag = 0;
			misc_mask->inner_second_svlan_tag = 0;
		}

		DR_STE_SET_MASK_V(eth_l2_src, bit_mask, second_vlan_id, misc_mask, inner_second_vid);
		DR_STE_SET_MASK_V(eth_l2_src, bit_mask, second_cfi, misc_mask, inner_second_cfi);
		DR_STE_SET_MASK_V(eth_l2_src, bit_mask, second_priority, misc_mask, inner_second_prio);
	} else {
		if (misc_mask->outer_second_cvlan_tag ||
		    misc_mask->outer_second_svlan_tag) {
			DR_STE_SET(eth_l2_src, bit_mask, second_vlan_qualifier, -1);
			misc_mask->outer_second_cvlan_tag = 0;
			misc_mask->outer_second_svlan_tag = 0;
		}

		DR_STE_SET_MASK_V(eth_l2_src, bit_mask, second_vlan_id, misc_mask, outer_second_vid);
		DR_STE_SET_MASK_V(eth_l2_src, bit_mask, second_cfi, misc_mask, outer_second_cfi);
		DR_STE_SET_MASK_V(eth_l2_src, bit_mask, second_priority, misc_mask, outer_second_prio);
	}
}

static int dr_ste_build_eth_l2_src_or_dst_tag(struct dr_match_param *value,
					      bool inner, uint8_t *tag)
{
	struct dr_match_spec *spec = inner ? &value->inner : &value->outer;
	struct dr_match_misc *misc_spec = &value->misc;

	DR_STE_SET_TAG(eth_l2_src, tag, first_vlan_id, spec, first_vid);
	DR_STE_SET_TAG(eth_l2_src, tag, first_cfi, spec, first_cfi);
	DR_STE_SET_TAG(eth_l2_src, tag, first_priority, spec, first_prio);
	DR_STE_SET_TAG(eth_l2_src, tag, ip_fragmented, spec, frag);
	DR_STE_SET_TAG(eth_l2_src, tag, l3_ethertype, spec, ethertype);

	if (spec->ip_version) {
		if (spec->ip_version == IP_VERSION_IPV4) {
			DR_STE_SET(eth_l2_src, tag, l3_type, STE_IPV4);
			spec->ip_version = 0;
		} else if (spec->ip_version == IP_VERSION_IPV6) {
			DR_STE_SET(eth_l2_src, tag, l3_type, STE_IPV6);
			spec->ip_version = 0;
		} else {
			errno = EINVAL;
			return errno;
		}
	}

	if (spec->cvlan_tag) {
		DR_STE_SET(eth_l2_src, tag, first_vlan_qualifier, DR_STE_CVLAN);
		spec->cvlan_tag = 0;
	} else if (spec->svlan_tag) {
		DR_STE_SET(eth_l2_src, tag, first_vlan_qualifier, DR_STE_SVLAN);
		spec->svlan_tag = 0;
	}

	if (inner) {
		if (misc_spec->inner_second_cvlan_tag) {
			DR_STE_SET(eth_l2_src, tag, second_vlan_qualifier, DR_STE_CVLAN);
			misc_spec->inner_second_cvlan_tag = 0;
		} else if (misc_spec->inner_second_svlan_tag) {
			DR_STE_SET(eth_l2_src, tag, second_vlan_qualifier, DR_STE_SVLAN);
			misc_spec->inner_second_svlan_tag = 0;
		}

		DR_STE_SET_TAG(eth_l2_src, tag, second_vlan_id, misc_spec, inner_second_vid);
		DR_STE_SET_TAG(eth_l2_src, tag, second_cfi, misc_spec, inner_second_cfi);
		DR_STE_SET_TAG(eth_l2_src, tag, second_priority, misc_spec, inner_second_prio);
	} else {
		if (misc_spec->outer_second_cvlan_tag) {
			DR_STE_SET(eth_l2_src, tag, second_vlan_qualifier, DR_STE_CVLAN);
			misc_spec->outer_second_cvlan_tag = 0;
		} else if (misc_spec->outer_second_svlan_tag) {
			DR_STE_SET(eth_l2_src, tag, second_vlan_qualifier, DR_STE_SVLAN);
			misc_spec->outer_second_svlan_tag = 0;
		}
		DR_STE_SET_TAG(eth_l2_src, tag, second_vlan_id, misc_spec, outer_second_vid);
		DR_STE_SET_TAG(eth_l2_src, tag, second_cfi, misc_spec, outer_second_cfi);
		DR_STE_SET_TAG(eth_l2_src, tag, second_priority, misc_spec, outer_second_prio);
	}

	return 0;
}

static void dr_ste_build_eth_l2_src_bit_mask(struct dr_match_param *value,
					     bool inner, uint8_t *bit_mask)
{
	struct dr_match_spec *mask = inner ? &value->inner : &value->outer;

	DR_STE_SET_MASK_V(eth_l2_src, bit_mask, smac_47_16, mask, smac_47_16);
	DR_STE_SET_MASK_V(eth_l2_src, bit_mask, smac_15_0, mask, smac_15_0);

	dr_ste_build_eth_l2_src_or_dst_bit_mask(value, inner, bit_mask);
}

static int dr_ste_build_eth_l2_src_tag(struct dr_match_param *value,
				       struct dr_ste_build *sb,
				       uint8_t *tag)
{
	struct dr_match_spec *spec = sb->inner ? &value->inner : &value->outer;

	DR_STE_SET_TAG(eth_l2_src, tag, smac_47_16, spec, smac_47_16);
	DR_STE_SET_TAG(eth_l2_src, tag, smac_15_0, spec, smac_15_0);

	return dr_ste_build_eth_l2_src_or_dst_tag(value, sb->inner, tag);
}

static void dr_ste_build_eth_l2_dst_bit_mask(struct dr_match_param *value,
					     bool inner, uint8_t *bit_mask)
{
	struct dr_match_spec *mask = inner ? &value->inner : &value->outer;

	DR_STE_SET_MASK_V(eth_l2_dst, bit_mask, dmac_47_16, mask, dmac_47_16);
	DR_STE_SET_MASK_V(eth_l2_dst, bit_mask, dmac_15_0, mask, dmac_15_0);

	dr_ste_build_eth_l2_src_or_dst_bit_mask(value, inner, bit_mask);
}

static int dr_ste_build_eth_l2_dst_tag(struct dr_match_param *value,
				       struct dr_ste_build *sb,
				       uint8_t *tag)
{
	struct dr_match_spec *spec = sb->inner ? &value->inner : &value->outer;

	DR_STE_SET_TAG(eth_l2_dst, tag, dmac_47_16, spec, dmac_47_16);
	DR_STE_SET_TAG(eth_l2_dst, tag, dmac_15_0, spec, dmac_15_0);

	return dr_ste_build_eth_l2_src_or_dst_tag(value, sb->inner, tag);
}

static void dr_ste_build_eth_l2_tnl_bit_mask(struct dr_match_param *value,
					     bool inner, uint8_t *bit_mask)
{
	struct dr_match_spec *mask = inner ? &value->inner : &value->outer;
	struct dr_match_misc *misc = &value->misc;

	DR_STE_SET_MASK_V(eth_l2_tnl, bit_mask, dmac_47_16, mask, dmac_47_16);
	DR_STE_SET_MASK_V(eth_l2_tnl, bit_mask, dmac_15_0, mask, dmac_15_0);
	DR_STE_SET_MASK_V(eth_l2_tnl, bit_mask, first_vlan_id, mask, first_vid);
	DR_STE_SET_MASK_V(eth_l2_tnl, bit_mask, first_cfi, mask, first_cfi);
	DR_STE_SET_MASK_V(eth_l2_tnl, bit_mask, first_priority, mask, first_prio);
	DR_STE_SET_MASK_V(eth_l2_tnl, bit_mask, ip_fragmented, mask, frag);
	DR_STE_SET_MASK_V(eth_l2_tnl, bit_mask, l3_ethertype, mask, ethertype);
	DR_STE_SET_MASK(eth_l2_tnl, bit_mask, l3_type, mask, ip_version);

	if (misc->vxlan_vni) {
		DR_STE_SET(eth_l2_tnl, bit_mask, l2_tunneling_network_id, (misc->vxlan_vni << 8));
		misc->vxlan_vni = 0;
	}

	if (mask->svlan_tag || mask->cvlan_tag) {
		DR_STE_SET(eth_l2_tnl, bit_mask, first_vlan_qualifier, -1);
		mask->cvlan_tag = 0;
		mask->svlan_tag = 0;
	}
}

static int dr_ste_build_eth_l2_tnl_tag(struct dr_match_param *value,
				       struct dr_ste_build *sb,
				       uint8_t *tag)
{
	struct dr_match_spec *spec = sb->inner ? &value->inner : &value->outer;
	struct dr_match_misc *misc = &value->misc;

	DR_STE_SET_TAG(eth_l2_tnl, tag, dmac_47_16, spec, dmac_47_16);
	DR_STE_SET_TAG(eth_l2_tnl, tag, dmac_15_0, spec, dmac_15_0);
	DR_STE_SET_TAG(eth_l2_tnl, tag, first_vlan_id, spec, first_vid);
	DR_STE_SET_TAG(eth_l2_tnl, tag, first_cfi, spec, first_cfi);
	DR_STE_SET_TAG(eth_l2_tnl, tag, ip_fragmented, spec, frag);
	DR_STE_SET_TAG(eth_l2_tnl, tag, first_priority, spec, first_prio);
	DR_STE_SET_TAG(eth_l2_tnl, tag, l3_ethertype, spec, ethertype);

	if (misc->vxlan_vni) {
		DR_STE_SET(eth_l2_tnl, tag, l2_tunneling_network_id,
			   (misc->vxlan_vni << 8));
		misc->vxlan_vni = 0;
	}

	if (spec->cvlan_tag) {
		DR_STE_SET(eth_l2_tnl, tag, first_vlan_qualifier, DR_STE_CVLAN);
		spec->cvlan_tag = 0;
	} else if (spec->svlan_tag) {
		DR_STE_SET(eth_l2_tnl, tag, first_vlan_qualifier, DR_STE_SVLAN);
		spec->svlan_tag = 0;
	}

	if (spec->ip_version) {
		if (spec->ip_version == IP_VERSION_IPV4) {
			DR_STE_SET(eth_l2_tnl, tag, l3_type, STE_IPV4);
			spec->ip_version = 0;
		} else if (spec->ip_version == IP_VERSION_IPV6) {
			DR_STE_SET(eth_l2_tnl, tag, l3_type, STE_IPV6);
			spec->ip_version = 0;
		} else {
			errno = EINVAL;
			return errno;
		}
	}

	return 0;
}

static void dr_ste_build_eth_l3_ipv4_misc_bit_mask(struct dr_match_param *value,
						   bool inner, uint8_t *bit_mask)
{
	struct dr_match_spec *mask = inner ? &value->inner : &value->outer;

	DR_STE_SET_MASK_V(eth_l3_ipv4_misc, bit_mask, time_to_live, mask, ip_ttl_hoplimit);
}

static int dr_ste_build_eth_l3_ipv4_misc_tag(struct dr_match_param *value,
					     struct dr_ste_build *sb,
					     uint8_t *tag)
{
	struct dr_match_spec *spec = sb->inner ? &value->inner : &value->outer;

	DR_STE_SET_TAG(eth_l3_ipv4_misc, tag, time_to_live, spec, ip_ttl_hoplimit);

	return 0;
}

static void dr_ste_build_ipv6_l3_l4_bit_mask(struct dr_match_param *value,
					     bool inner, uint8_t *bit_mask)
{
	struct dr_match_spec *mask = inner ? &value->inner : &value->outer;

	DR_STE_SET_MASK_V(eth_l4, bit_mask, dst_port, mask, tcp_dport);
	DR_STE_SET_MASK_V(eth_l4, bit_mask, src_port, mask, tcp_sport);
	DR_STE_SET_MASK_V(eth_l4, bit_mask, dst_port, mask, udp_dport);
	DR_STE_SET_MASK_V(eth_l4, bit_mask, src_port, mask, udp_sport);
	DR_STE_SET_MASK_V(eth_l4, bit_mask, protocol, mask, ip_protocol);
	DR_STE_SET_MASK_V(eth_l4, bit_mask, fragmented, mask, frag);
	DR_STE_SET_MASK_V(eth_l4, bit_mask, dscp, mask, ip_dscp);
	DR_STE_SET_MASK_V(eth_l4, bit_mask, ecn, mask, ip_ecn);
	DR_STE_SET_MASK_V(eth_l4, bit_mask, ipv6_hop_limit, mask, ip_ttl_hoplimit);

	if (mask->tcp_flags) {
		DR_STE_SET_TCP_FLAGS(eth_l4, bit_mask, mask);
		mask->tcp_flags = 0;
	}
}

static int dr_ste_build_ipv6_l3_l4_tag(struct dr_match_param *value,
				       struct dr_ste_build *sb,
				       uint8_t *tag)
{
	struct dr_match_spec *spec = sb->inner ? &value->inner : &value->outer;

	DR_STE_SET_TAG(eth_l4, tag, dst_port, spec, tcp_dport);
	DR_STE_SET_TAG(eth_l4, tag, src_port, spec, tcp_sport);
	DR_STE_SET_TAG(eth_l4, tag, dst_port, spec, udp_dport);
	DR_STE_SET_TAG(eth_l4, tag, src_port, spec, udp_sport);
	DR_STE_SET_TAG(eth_l4, tag, protocol, spec, ip_protocol);
	DR_STE_SET_TAG(eth_l4, tag, fragmented, spec, frag);
	DR_STE_SET_TAG(eth_l4, tag, dscp, spec, ip_dscp);
	DR_STE_SET_TAG(eth_l4, tag, ecn, spec, ip_ecn);
	DR_STE_SET_TAG(eth_l4, tag, ipv6_hop_limit, spec, ip_ttl_hoplimit);

	if (spec->tcp_flags) {
		DR_STE_SET_TCP_FLAGS(eth_l4, tag, spec);
		spec->tcp_flags = 0;
	}

	return 0;
}

static void dr_ste_build_eth_l4_misc_bit_mask(struct dr_match_param *value,
					      bool inner, uint8_t *bit_mask)
{
	struct dr_match_misc3 *misc_3_mask = &value->misc3;

	if (inner) {
		DR_STE_SET_MASK_V(eth_l4_misc, bit_mask, seq_num, misc_3_mask,
				  inner_tcp_seq_num);
		DR_STE_SET_MASK_V(eth_l4_misc, bit_mask, ack_num, misc_3_mask,
				  inner_tcp_ack_num);
	} else {
		DR_STE_SET_MASK_V(eth_l4_misc, bit_mask, seq_num, misc_3_mask,
				  outer_tcp_seq_num);
		DR_STE_SET_MASK_V(eth_l4_misc, bit_mask, ack_num, misc_3_mask,
				  outer_tcp_ack_num);
	}
}

static int dr_ste_build_eth_l4_misc_tag(struct dr_match_param *value,
					struct dr_ste_build *sb,
					uint8_t *tag)
{
	struct dr_match_misc3 *misc3 = &value->misc3;

	if (sb->inner) {
		DR_STE_SET_TAG(eth_l4_misc, tag, seq_num, misc3, inner_tcp_seq_num);
		DR_STE_SET_TAG(eth_l4_misc, tag, ack_num, misc3, inner_tcp_ack_num);
	} else {
		DR_STE_SET_TAG(eth_l4_misc, tag, seq_num, misc3, outer_tcp_seq_num);
		DR_STE_SET_TAG(eth_l4_misc, tag, ack_num, misc3, outer_tcp_ack_num);
	}

	return 0;
}

static void dr_ste_build_mpls_bit_mask(struct dr_match_param *value,
				       bool inner, uint8_t *bit_mask)
{
	struct dr_match_misc2 *misc2_mask = &value->misc2;

	if (inner)
		DR_STE_SET_MPLS_MASK(mpls, misc2_mask, inner, bit_mask);
	else
		DR_STE_SET_MPLS_MASK(mpls, misc2_mask, outer, bit_mask);
}

static int dr_ste_build_mpls_tag(struct dr_match_param *value,
				 struct dr_ste_build *sb,
				 uint8_t *tag)
{
	struct dr_match_misc2 *misc2_mask = &value->misc2;

	if (sb->inner)
		DR_STE_SET_MPLS_TAG(mpls, misc2_mask, inner, tag);
	else
		DR_STE_SET_MPLS_TAG(mpls, misc2_mask, outer, tag);

	return 0;
}

static void dr_ste_build_gre_bit_mask(struct dr_match_param *value,
				      bool inner, uint8_t *bit_mask)
{
	struct dr_match_misc *misc_mask = &value->misc;

	DR_STE_SET_MASK_V(gre, bit_mask, gre_protocol, misc_mask, gre_protocol);
	DR_STE_SET_MASK_V(gre, bit_mask, gre_k_present, misc_mask, gre_k_present);
	DR_STE_SET_MASK_V(gre, bit_mask, gre_key_h, misc_mask, gre_key_h);
	DR_STE_SET_MASK_V(gre, bit_mask, gre_key_l, misc_mask, gre_key_l);

	DR_STE_SET_MASK_V(gre, bit_mask, gre_c_present, misc_mask, gre_c_present);
	DR_STE_SET_MASK_V(gre, bit_mask, gre_s_present, misc_mask, gre_s_present);
}

static int dr_ste_build_gre_tag(struct dr_match_param *value,
				struct dr_ste_build *sb,
				uint8_t *tag)
{
	struct  dr_match_misc *misc = &value->misc;

	DR_STE_SET_TAG(gre, tag, gre_protocol, misc, gre_protocol);

	DR_STE_SET_TAG(gre, tag, gre_k_present, misc, gre_k_present);
	DR_STE_SET_TAG(gre, tag, gre_key_h, misc, gre_key_h);
	DR_STE_SET_TAG(gre, tag, gre_key_l, misc, gre_key_l);

	DR_STE_SET_TAG(gre, tag, gre_c_present, misc, gre_c_present);

	DR_STE_SET_TAG(gre, tag, gre_s_present, misc, gre_s_present);

	return 0;
}

static void
dr_ste_build_flex_parser_tnl_gtpu_bit_mask(struct dr_match_param *value,
					   uint8_t *bit_mask)
{
	struct dr_match_misc3 *misc3 = &value->misc3;

	DR_STE_SET_MASK_V(flex_parser_tnl_gtpu, bit_mask,
			  gtpu_flags, misc3,
			  gtpu_flags);
	DR_STE_SET_MASK_V(flex_parser_tnl_gtpu, bit_mask,
			  gtpu_msg_type, misc3,
			  gtpu_msg_type);
	DR_STE_SET_MASK_V(flex_parser_tnl_gtpu, bit_mask,
			  gtpu_teid, misc3,
			  gtpu_teid);
}

static int
dr_ste_build_flex_parser_tnl_gtpu_tag(struct dr_match_param *value,
				      struct dr_ste_build *sb,
				      uint8_t *tag)
{
	struct dr_match_misc3 *misc3 = &value->misc3;

	DR_STE_SET_TAG(flex_parser_tnl_gtpu, tag,
		       gtpu_flags, misc3,
		       gtpu_flags);
	DR_STE_SET_TAG(flex_parser_tnl_gtpu, tag,
		       gtpu_msg_type, misc3,
		       gtpu_msg_type);
	DR_STE_SET_TAG(flex_parser_tnl_gtpu, tag,
		       gtpu_teid, misc3,
		       gtpu_teid);

	return 0;
}

static void dr_ste_build_flex_parser_0_bit_mask(struct dr_match_param *value,
						bool inner, uint8_t *bit_mask)
{
	struct dr_match_misc2 *misc_2_mask = &value->misc2;

	if (DR_STE_IS_OUTER_MPLS_OVER_GRE_SET(misc_2_mask)) {
		DR_STE_SET_MASK_V(flex_parser_0, bit_mask, parser_3_label,
				  misc_2_mask, outer_first_mpls_over_gre_label);

		DR_STE_SET_MASK_V(flex_parser_0, bit_mask, parser_3_exp,
				  misc_2_mask, outer_first_mpls_over_gre_exp);

		DR_STE_SET_MASK_V(flex_parser_0, bit_mask, parser_3_s_bos,
				  misc_2_mask, outer_first_mpls_over_gre_s_bos);

		DR_STE_SET_MASK_V(flex_parser_0, bit_mask, parser_3_ttl,
				  misc_2_mask, outer_first_mpls_over_gre_ttl);
	} else {
		DR_STE_SET_MASK_V(flex_parser_0, bit_mask, parser_3_label,
				  misc_2_mask, outer_first_mpls_over_udp_label);

		DR_STE_SET_MASK_V(flex_parser_0, bit_mask, parser_3_exp,
				  misc_2_mask, outer_first_mpls_over_udp_exp);

		DR_STE_SET_MASK_V(flex_parser_0, bit_mask, parser_3_s_bos,
				  misc_2_mask, outer_first_mpls_over_udp_s_bos);

		DR_STE_SET_MASK_V(flex_parser_0, bit_mask, parser_3_ttl,
				  misc_2_mask, outer_first_mpls_over_udp_ttl);
	}
}

static int dr_ste_build_flex_parser_0_tag(struct dr_match_param *value,
					  struct dr_ste_build *sb,
					  uint8_t *tag)
{
	struct dr_match_misc2 *misc_2_mask = &value->misc2;

	if (DR_STE_IS_OUTER_MPLS_OVER_GRE_SET(misc_2_mask)) {
		DR_STE_SET_TAG(flex_parser_0, tag, parser_3_label,
			       misc_2_mask, outer_first_mpls_over_gre_label);

		DR_STE_SET_TAG(flex_parser_0, tag, parser_3_exp,
			       misc_2_mask, outer_first_mpls_over_gre_exp);

		DR_STE_SET_TAG(flex_parser_0, tag, parser_3_s_bos,
			       misc_2_mask, outer_first_mpls_over_gre_s_bos);

		DR_STE_SET_TAG(flex_parser_0, tag, parser_3_ttl,
			       misc_2_mask, outer_first_mpls_over_gre_ttl);
	} else {
		DR_STE_SET_TAG(flex_parser_0, tag, parser_3_label,
			       misc_2_mask, outer_first_mpls_over_udp_label);

		DR_STE_SET_TAG(flex_parser_0, tag, parser_3_exp,
			       misc_2_mask, outer_first_mpls_over_udp_exp);

		DR_STE_SET_TAG(flex_parser_0, tag, parser_3_s_bos,
			       misc_2_mask, outer_first_mpls_over_udp_s_bos);

		DR_STE_SET_TAG(flex_parser_0, tag, parser_3_ttl,
			       misc_2_mask, outer_first_mpls_over_udp_ttl);
	}
	return 0;
}

#define ICMP_TYPE_OFFSET_FIRST_DW		24
#define ICMP_CODE_OFFSET_FIRST_DW		16
#define ICMP_HEADER_DATA_OFFSET_SECOND_DW	0

static int dr_ste_build_flex_parser_1_bit_mask(struct dr_match_param *mask,
					       struct dr_devx_caps *caps,
					       uint8_t *bit_mask)
{
	struct dr_match_misc3 *misc_3_mask = &mask->misc3;
	bool is_ipv4_mask = DR_MASK_IS_FLEX_PARSER_ICMPV4_SET(misc_3_mask);
	uint32_t icmp_header_data_mask;
	uint32_t icmp_type_mask;
	uint32_t icmp_code_mask;
	int dw0_location;
	int dw1_location;

	if (is_ipv4_mask) {
		icmp_header_data_mask	= misc_3_mask->icmpv4_header_data;
		icmp_type_mask		= misc_3_mask->icmpv4_type;
		icmp_code_mask		= misc_3_mask->icmpv4_code;
		dw0_location		= caps->flex_parser_id_icmp_dw0;
		dw1_location		= caps->flex_parser_id_icmp_dw1;
	} else {
		icmp_header_data_mask	= misc_3_mask->icmpv6_header_data;
		icmp_type_mask		= misc_3_mask->icmpv6_type;
		icmp_code_mask		= misc_3_mask->icmpv6_code;
		dw0_location		= caps->flex_parser_id_icmpv6_dw0;
		dw1_location		= caps->flex_parser_id_icmpv6_dw1;
	}

	switch (dw0_location) {
	case 4:
		if (icmp_type_mask) {
			DR_STE_SET(flex_parser_1, bit_mask, flex_parser_4,
				   (icmp_type_mask << ICMP_TYPE_OFFSET_FIRST_DW));
			if (is_ipv4_mask)
				misc_3_mask->icmpv4_type = 0;
			else
				misc_3_mask->icmpv6_type = 0;
		}
		if (icmp_code_mask) {
			uint32_t cur_val = DR_STE_GET(flex_parser_1, bit_mask,
						      flex_parser_4);
			DR_STE_SET(flex_parser_1, bit_mask, flex_parser_4,
				   cur_val | (icmp_code_mask << ICMP_CODE_OFFSET_FIRST_DW));
			if (is_ipv4_mask)
				misc_3_mask->icmpv4_code = 0;
			else
				misc_3_mask->icmpv6_code = 0;
		}
		break;
	default:
		errno = ENOTSUP;
		return errno;
	}

	switch (dw1_location) {
	case 5:
		if (icmp_header_data_mask) {
			DR_STE_SET(flex_parser_1, bit_mask, flex_parser_5,
				   (icmp_header_data_mask << ICMP_HEADER_DATA_OFFSET_SECOND_DW));
			if (is_ipv4_mask)
				misc_3_mask->icmpv4_header_data = 0;
			else
				misc_3_mask->icmpv6_header_data = 0;
		}
		break;
	default:
		errno = ENOTSUP;
		return errno;
	}

	return 0;
}

static int dr_ste_build_flex_parser_1_tag(struct dr_match_param *value,
					  struct dr_ste_build *sb,
					  uint8_t *tag)
{
	struct dr_match_misc3 *misc_3 = &value->misc3;
	bool is_ipv4 = DR_MASK_IS_FLEX_PARSER_ICMPV4_SET(misc_3);
	uint32_t icmp_header_data;
	uint32_t icmp_type;
	uint32_t icmp_code;
	int dw0_location;
	int dw1_location;

	if (is_ipv4) {
		icmp_header_data	= misc_3->icmpv4_header_data;
		icmp_type		= misc_3->icmpv4_type;
		icmp_code		= misc_3->icmpv4_code;
		dw0_location		= sb->caps->flex_parser_id_icmp_dw0;
		dw1_location		= sb->caps->flex_parser_id_icmp_dw1;
	} else {
		icmp_header_data	= misc_3->icmpv6_header_data;
		icmp_type		= misc_3->icmpv6_type;
		icmp_code		= misc_3->icmpv6_code;
		dw0_location		= sb->caps->flex_parser_id_icmpv6_dw0;
		dw1_location		= sb->caps->flex_parser_id_icmpv6_dw1;
	}

	switch (dw0_location) {
	case 4:
		if (icmp_type) {
			DR_STE_SET(flex_parser_1, tag, flex_parser_4,
				   (icmp_type << ICMP_TYPE_OFFSET_FIRST_DW));
			if (is_ipv4)
				misc_3->icmpv4_type = 0;
			else
				misc_3->icmpv6_type = 0;
		}

		if (icmp_code) {
			uint32_t cur_val = DR_STE_GET(flex_parser_1, tag,
						      flex_parser_4);
			DR_STE_SET(flex_parser_1, tag, flex_parser_4,
				   cur_val | (icmp_code << ICMP_CODE_OFFSET_FIRST_DW));
			if (is_ipv4)
				misc_3->icmpv4_code = 0;
			else
				misc_3->icmpv6_code = 0;
		}
		break;
	default:
		errno = ENOTSUP;
		return errno;
	}

	switch (dw1_location) {
	case 5:
		if (icmp_header_data) {
			DR_STE_SET(flex_parser_1, tag, flex_parser_5,
				   (icmp_header_data << ICMP_HEADER_DATA_OFFSET_SECOND_DW));
			if (is_ipv4)
				misc_3->icmpv4_header_data = 0;
			else
				misc_3->icmpv6_header_data = 0;
		}
		break;
	default:
		errno = ENOTSUP;
		return errno;
	}

	return 0;
}

static int dr_ste_build_src_gvmi_qpn_bit_mask(struct dr_match_param *value,
					      uint8_t *bit_mask)
{
	struct dr_match_misc *misc_mask = &value->misc;

	if (misc_mask->source_port && misc_mask->source_port != 0xffff) {
		errno = EINVAL;
		return errno;
	}
	DR_STE_SET_MASK(src_gvmi_qp, bit_mask, source_gvmi, misc_mask, source_port);
	DR_STE_SET_MASK(src_gvmi_qp, bit_mask, source_qp, misc_mask, source_sqn);

	return 0;
}

static int dr_ste_build_src_gvmi_qpn_tag(struct dr_match_param *value,
					 struct dr_ste_build *sb,
					 uint8_t *tag)
{
	struct dr_match_misc *misc = &value->misc;
	struct dr_devx_vport_cap *vport_cap;

	DR_STE_SET_TAG(src_gvmi_qp, tag, source_qp, misc, source_sqn);

	vport_cap = dr_get_vport_cap(sb->caps, misc->source_port);
	if (!vport_cap)
		return errno;

	if (vport_cap->vport_gvmi)
		DR_STE_SET(src_gvmi_qp, tag, source_gvmi, vport_cap->vport_gvmi);

	misc->source_port = 0;

	return 0;
}

static struct dr_ste_ctx ste_ctx_v0 = {
	.build_eth_l2_src_des_bit_mask = &dr_ste_build_eth_l2_src_des_bit_mask,
	.build_eth_l2_src_des_tag = &dr_ste_build_eth_l2_src_des_tag,

	.build_eth_l3_ipv4_5_tuple_bit_mask = &dr_ste_build_eth_l3_ipv4_5_tuple_bit_mask,
	.build_eth_l3_ipv4_5_tuple_tag = &dr_ste_build_eth_l3_ipv4_5_tuple_tag,

	.build_eth_l2_src_bit_mask = &dr_ste_build_eth_l2_src_bit_mask,
	.build_eth_l2_src_tag = &dr_ste_build_eth_l2_src_tag,

	.build_eth_l2_dst_bit_mask = &dr_ste_build_eth_l2_dst_bit_mask,
	.build_eth_l2_dst_tag = &dr_ste_build_eth_l2_dst_tag,

	.build_eth_l2_tnl_bit_mask = &dr_ste_build_eth_l2_tnl_bit_mask,
	.build_eth_l2_tnl_tag = &dr_ste_build_eth_l2_tnl_tag,

	.build_eth_l3_ipv4_misc_bit_mask = &dr_ste_build_eth_l3_ipv4_misc_bit_mask,
	.build_eth_l3_ipv4_misc_tag = &dr_ste_build_eth_l3_ipv4_misc_tag,

	.build_ipv6_l3_l4_bit_mask = &dr_ste_build_ipv6_l3_l4_bit_mask,
	.build_ipv6_l3_l4_tag = &dr_ste_build_ipv6_l3_l4_tag,

	.build_eth_l4_misc_bit_mask = &dr_ste_build_eth_l4_misc_bit_mask,
	.build_eth_l4_misc_tag = &dr_ste_build_eth_l4_misc_tag,

	.build_mpls_bit_mask = &dr_ste_build_mpls_bit_mask,
	.build_mpls_tag = &dr_ste_build_mpls_tag,

	.build_gre_bit_mask = &dr_ste_build_gre_bit_mask,
	.build_gre_tag = &dr_ste_build_gre_tag,

	.build_flex_parser_tnl_gtpu_bit_mask = &dr_ste_build_flex_parser_tnl_gtpu_bit_mask,
	.build_flex_parser_tnl_gtpu_tag = &dr_ste_build_flex_parser_tnl_gtpu_tag,

	.build_flex_parser_0_bit_mask = &dr_ste_build_flex_parser_0_bit_mask,
	.build_flex_parser_0_tag = &dr_ste_build_flex_parser_0_tag,

	.build_flex_parser_1_bit_mask = &dr_ste_build_flex_parser_1_bit_mask,
	.build_flex_parser_1_tag = &dr_ste_build_flex_parser_1_tag,

	.build_src_gvmi_qpn_bit_mask = &dr_ste_build_src_gvmi_qpn_bit_mask,
	.build_src_gvmi_qpn_tag = &dr_ste_build_src_gvmi_qpn_tag,

	.ste_init = &dr_ste_hw_init,

	.set_next_lu_type = &dr_ste_hw_set_next_lu_type,

	.set_miss_addr = &dr_ste_hw_set_miss_addr,
	.get_miss_addr = &dr_ste_hw_get_miss_addr,

	.set_hit_addr = &dr_ste_hw_set_hit_addr,

	.set_byte_mask = &dr_ste_hw_set_byte_mask,
	.get_byte_mask = &dr_ste_hw_get_byte_mask,

	.set_actions_rx = &dr_ste_hw_set_actions_rx,
	.set_actions_tx = &dr_ste_hw_set_actions_tx,

	.modify_hdr_field_arr_sz = ARRAY_SIZE(dr_ste_hw_modify_hdr_field_arr),
	.modify_hdr_field_arr = dr_ste_hw_modify_hdr_field_arr,

	.set_set_action = &dr_ste_hw_set_set_action,
	.set_add_action = &dr_ste_hw_set_add_action,
	.set_copy_action = &dr_ste_hw_set_copy_action,

	.set_decap_l3_action_list = &dr_ste_hw_set_decap_l3_action_list,
};

struct dr_ste_ctx *dr_ste_init_ctx_v0(void)
{
	return &ste_ctx_v0;
}
