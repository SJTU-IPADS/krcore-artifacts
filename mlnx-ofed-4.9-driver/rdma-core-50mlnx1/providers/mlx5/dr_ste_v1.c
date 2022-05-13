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

#include <ccan/array_size.h>
#include "dr_ste.h"

enum dr_ste_action_size {
	DR_STE_ACTION_SINGLE_SZ = 4,
	DR_STE_ACTION_DOUBLE_SZ = 8,
};

enum dr_ste_header_anchors {
	DR_STE_HEADER_ANCHOR_START_OUTER		= 0x00,
	DR_STE_HEADER_ANCHOR_1ST_VLAN			= 0x02,
	DR_STE_HEADER_ANCHOR_IPV6_IPV4			= 0x07,
	DR_STE_HEADER_ANCHOR_INNER_MAC			= 0x13,
	DR_STE_HEADER_ANCHOR_INNER_IPV6_IPV4		= 0x19,
};

enum dr_ste_action_id {
	DR_STE_ACTION_ID_NOP				= 0x00,
	DR_STE_ACTION_ID_COPY				= 0x05,
	DR_STE_ACTION_ID_SET				= 0x06,
	DR_STE_ACTION_ID_ADD				= 0x07,
	DR_STE_ACTION_ID_REMOVE_BY_SIZE			= 0x08,
	DR_STE_ACTION_ID_REMOVE_HEADER_TO_HEADER	= 0x09,
	DR_STE_ACTION_ID_INSERT_INLINE			= 0x0a,
	DR_STE_ACTION_ID_INSERT_POINTER			= 0x0b,
	DR_STE_ACTION_ID_FLOW_TAG			= 0x0c,
	DR_STE_ACTION_ID_QUEUE_ID_SEL			= 0x0d,
	DR_STE_ACTION_ID_ACCELERATED_LIST		= 0x0e,
	DR_STE_ACTION_ID_MODIFY_LIST			= 0x0f,
	DR_STE_ACTION_ID_TRAILER			= 0x13,
	DR_STE_ACTION_ID_COUNTER_ID			= 0x14,
	DR_STE_ACTION_ID_MAX				= 0x21,
	/* use for special cases */
	DR_STE_ACTION_ID_SPECIAL_ENCAP_L3		= 0x22,
};

enum dr_ste_entry_format {
	DR_STE_TYPE_BWC_BYTE		= 0x0,
	DR_STE_TYPE_BWC_DW		= 0x1,
	DR_STE_TYPE_MATCH		= 0x2,
};

enum dr_ste_definer_type {
	DR_STE_DEFINER_TYPE_NOP						= 0x00,
	DR_STE_DEFINER_TYPE_IBL3_EXT__ETHL2_TNL				= 0x02,
	DR_STE_DEFINER_TYPE_IBL4__ETHL2_O				= 0x03,
	DR_STE_DEFINER_TYPE_SRC_QP_GVMI__ETHL2_I			= 0x04,
	DR_STE_DEFINER_TYPE_ETHL2_HEADERS_O__ETHL2_SRC_O		= 0x05,
	DR_STE_DEFINER_TYPE_ETHL2_HEADERS_I__ETHL2_SRC_I		= 0x06,
	DR_STE_DEFINER_TYPE_ETHL3_IPV6_DES_O__ETHL3_IPV4_5_TUPLE_O	= 0x07,
	DR_STE_DEFINER_TYPE_ETHL3_IPV6_DES_I__ETHL3_IPV4_5_TUPLE_I	= 0x08,
	DR_STE_DEFINER_TYPE_ETHL3_IPV6_SRC_O__ETHL4_O			= 0x09,
	DR_STE_DEFINER_TYPE_ETHL3_IPV6_SRC_I__ETHL4_I			= 0x0a,
	DR_STE_DEFINER_TYPE_MPLS_O__ETHL2_SRC_DST_O			= 0x0b,
	DR_STE_DEFINER_TYPE_MPLS_I__ETHL2_SRC_DST_I			= 0x0c,
	DR_STE_DEFINER_TYPE_GRE__ETHL3_IPV4_MISC_O			= 0x0d,
	DR_STE_DEFINER_TYPE_GENERAL_PURPOSE__FLEX_PARSER_TNL_HEADER	= 0x0e,
	DR_STE_DEFINER_TYPE_STEERING_REGISTERS_0__ETHL3_IPV4_MISC_I	= 0x0f,
	DR_STE_DEFINER_TYPE_STEERING_REGISTERS_1			= 0x10,
	DR_STE_DEFINER_TYPE_FLEX_PARSER_0				= 0x11,
	DR_STE_DEFINER_TYPE_FLEX_PARSER_1				= 0x12,
	DR_STE_DEFINER_TYPE_ETHL4_MISC_O				= 0x13,
	DR_STE_DEFINER_TYPE_ETHL4_MISC_I				= 0x14,
	DR_STE_DEFINER_TYPE_DONT_CARE					= 0x0f,
	DR_STE_DEFINER_TYPE_INVALID					= 0xff,
};

struct dr_ste_lu_type_info {
	uint8_t def_mode;
	uint8_t def_type;
};

static const struct dr_ste_lu_type_info dr_ste_lu_type_conv_arr[] = {
	[DR_STE_LU_TYPE_ETHL2_DST_O] = {
		.def_mode = DR_STE_TYPE_BWC_BYTE,
		.def_type = DR_STE_DEFINER_TYPE_IBL4__ETHL2_O,
	},
	[DR_STE_LU_TYPE_ETHL2_DST_D] = {
		.def_mode = DR_STE_TYPE_BWC_BYTE,
		.def_type = DR_STE_DEFINER_TYPE_IBL4__ETHL2_O,
	},
	[DR_STE_LU_TYPE_ETHL2_DST_I] = {
		.def_mode = DR_STE_TYPE_BWC_BYTE,
		.def_type = DR_STE_DEFINER_TYPE_SRC_QP_GVMI__ETHL2_I,
	},
	[DR_STE_LU_TYPE_ETHL3_IPV4_5_TUPLE_O] = {
		.def_mode = DR_STE_TYPE_BWC_BYTE,
		.def_type = DR_STE_DEFINER_TYPE_ETHL3_IPV6_DES_O__ETHL3_IPV4_5_TUPLE_O,
	},
	[DR_STE_LU_TYPE_ETHL3_IPV4_5_TUPLE_D] = {
		.def_mode = DR_STE_TYPE_BWC_BYTE,
		.def_type = DR_STE_DEFINER_TYPE_ETHL3_IPV6_DES_O__ETHL3_IPV4_5_TUPLE_O,
	},
	[DR_STE_LU_TYPE_ETHL3_IPV4_5_TUPLE_I] = {
		.def_mode = DR_STE_TYPE_BWC_BYTE,
		.def_type = DR_STE_DEFINER_TYPE_ETHL3_IPV6_DES_I__ETHL3_IPV4_5_TUPLE_I,
	},
	[DR_STE_LU_TYPE_ETHL2_SRC_DST_D] = {
		.def_mode = DR_STE_TYPE_BWC_BYTE,
		.def_type = DR_STE_DEFINER_TYPE_MPLS_O__ETHL2_SRC_DST_O,
	},
	[DR_STE_LU_TYPE_ETHL2_SRC_DST_O] = {
		.def_mode = DR_STE_TYPE_BWC_BYTE,
		.def_type = DR_STE_DEFINER_TYPE_MPLS_O__ETHL2_SRC_DST_O,
	},
	[DR_STE_LU_TYPE_ETHL2_SRC_DST_I] = {
		.def_mode = DR_STE_TYPE_BWC_BYTE,
		.def_type = DR_STE_DEFINER_TYPE_MPLS_I__ETHL2_SRC_DST_I,
	},
	[DR_STE_LU_TYPE_ETHL3_IPV6_DST_O] = {
		.def_mode = DR_STE_TYPE_BWC_DW,
		.def_type = DR_STE_DEFINER_TYPE_ETHL3_IPV6_DES_O__ETHL3_IPV4_5_TUPLE_O,
	},
	[DR_STE_LU_TYPE_ETHL3_IPV6_DST_D] = {
		.def_mode = DR_STE_TYPE_BWC_DW,
		.def_type = DR_STE_DEFINER_TYPE_ETHL3_IPV6_DES_O__ETHL3_IPV4_5_TUPLE_O,
	},
	[DR_STE_LU_TYPE_ETHL3_IPV6_DST_I] = {
		.def_mode = DR_STE_TYPE_BWC_DW,
		.def_type = DR_STE_DEFINER_TYPE_ETHL3_IPV6_DES_I__ETHL3_IPV4_5_TUPLE_I,
	},
	[DR_STE_LU_TYPE_ETHL3_IPV6_SRC_O] = {
		.def_mode = DR_STE_TYPE_BWC_DW,
		.def_type = DR_STE_DEFINER_TYPE_ETHL3_IPV6_SRC_O__ETHL4_O,
	},
	[DR_STE_LU_TYPE_ETHL3_IPV6_SRC_D] = {
		.def_mode = DR_STE_TYPE_BWC_DW,
		.def_type = DR_STE_DEFINER_TYPE_ETHL3_IPV6_SRC_O__ETHL4_O,
	},
	[DR_STE_LU_TYPE_ETHL3_IPV6_SRC_I] = {
		.def_mode = DR_STE_TYPE_BWC_DW,
		.def_type = DR_STE_DEFINER_TYPE_ETHL3_IPV6_SRC_I__ETHL4_I,
	},
	[DR_STE_LU_TYPE_ETHL3_IPV4_MISC_O] = {
		.def_mode = DR_STE_TYPE_BWC_BYTE,
		.def_type = DR_STE_DEFINER_TYPE_GRE__ETHL3_IPV4_MISC_O,
	},
	[DR_STE_LU_TYPE_ETHL3_IPV4_MISC_D] = {
		.def_mode = DR_STE_TYPE_BWC_BYTE,
		.def_type = DR_STE_DEFINER_TYPE_GRE__ETHL3_IPV4_MISC_O,
	},
	[DR_STE_LU_TYPE_ETHL3_IPV4_MISC_I] = {
		.def_mode = DR_STE_TYPE_BWC_BYTE,
		.def_type = DR_STE_DEFINER_TYPE_STEERING_REGISTERS_0__ETHL3_IPV4_MISC_I,
	},
	[DR_STE_LU_TYPE_ETHL4_O] = {
		.def_mode = DR_STE_TYPE_BWC_BYTE,
		.def_type = DR_STE_DEFINER_TYPE_ETHL3_IPV6_SRC_O__ETHL4_O,
	},
	[DR_STE_LU_TYPE_ETHL4_D] = {
		.def_mode = DR_STE_TYPE_BWC_BYTE,
		.def_type = DR_STE_DEFINER_TYPE_ETHL3_IPV6_SRC_O__ETHL4_O,
	},
	[DR_STE_LU_TYPE_ETHL4_I] = {
		.def_mode = DR_STE_TYPE_BWC_BYTE,
		.def_type = DR_STE_DEFINER_TYPE_ETHL3_IPV6_SRC_I__ETHL4_I,
	},
	[DR_STE_LU_TYPE_ETHL2_SRC_I] = {
		.def_mode = DR_STE_TYPE_BWC_BYTE,
		.def_type = DR_STE_DEFINER_TYPE_ETHL2_HEADERS_I__ETHL2_SRC_I,
	},
	[DR_STE_LU_TYPE_ETHL2_SRC_O] = {
		.def_mode = DR_STE_TYPE_BWC_BYTE,
		.def_type = DR_STE_DEFINER_TYPE_ETHL2_HEADERS_O__ETHL2_SRC_O,
	},
	[DR_STE_LU_TYPE_ETHL2_SRC_D] = {
		.def_mode = DR_STE_TYPE_BWC_BYTE,
		.def_type = DR_STE_DEFINER_TYPE_ETHL2_HEADERS_O__ETHL2_SRC_O,
	},
	[DR_STE_LU_TYPE_GRE] = {
		.def_mode = DR_STE_TYPE_BWC_DW,
		.def_type = DR_STE_DEFINER_TYPE_GRE__ETHL3_IPV4_MISC_O,
	},
	[DR_STE_LU_TYPE_MPLS_FIRST_O] = {
		.def_mode = DR_STE_TYPE_BWC_DW,
		.def_type = DR_STE_DEFINER_TYPE_MPLS_O__ETHL2_SRC_DST_O,
	},
	[DR_STE_LU_TYPE_MPLS_FIRST_D] = {
		.def_mode = DR_STE_TYPE_BWC_DW,
		.def_type = DR_STE_DEFINER_TYPE_MPLS_O__ETHL2_SRC_DST_O,
	},
	[DR_STE_LU_TYPE_MPLS_FIRST_I] = {
		.def_mode = DR_STE_TYPE_BWC_DW,
		.def_type = DR_STE_DEFINER_TYPE_MPLS_I__ETHL2_SRC_DST_I,
	},
	[DR_STE_LU_TYPE_STEERING_REGISTERS_0] = {
		.def_mode = DR_STE_TYPE_BWC_DW,
		.def_type = DR_STE_DEFINER_TYPE_STEERING_REGISTERS_0__ETHL3_IPV4_MISC_I,
	},
	[DR_STE_LU_TYPE_STEERING_REGISTERS_1] = {
		.def_mode = DR_STE_TYPE_BWC_DW,
		.def_type = DR_STE_DEFINER_TYPE_STEERING_REGISTERS_1,
	},
	[DR_STE_LU_TYPE_FLEX_PARSER_0] = {
		.def_mode = DR_STE_TYPE_BWC_DW,
		.def_type = DR_STE_DEFINER_TYPE_FLEX_PARSER_0,
	},
	[DR_STE_LU_TYPE_FLEX_PARSER_1] = {
		.def_mode = DR_STE_TYPE_BWC_DW,
		.def_type = DR_STE_DEFINER_TYPE_FLEX_PARSER_1,
	},
	[DR_STE_LU_TYPE_ETHL4_MISC_O] = {
		.def_mode = DR_STE_TYPE_BWC_DW,
		.def_type = DR_STE_DEFINER_TYPE_ETHL4_MISC_O,
	},
	[DR_STE_LU_TYPE_ETHL4_MISC_I] = {
		.def_mode = DR_STE_TYPE_BWC_DW,
		.def_type = DR_STE_DEFINER_TYPE_ETHL4_MISC_I,
	},
	[DR_STE_LU_TYPE_ETHL4_MISC_D] = {
		.def_mode = DR_STE_TYPE_BWC_DW,
		.def_type = DR_STE_DEFINER_TYPE_ETHL4_MISC_I,
	},
	[DR_STE_LU_TYPE_ETHL2_TUNNELING_I] = {
		.def_mode = DR_STE_TYPE_BWC_BYTE,
		.def_type = DR_STE_DEFINER_TYPE_IBL3_EXT__ETHL2_TNL,
	},
	[DR_STE_LU_TYPE_SRC_GVMI_AND_QP] = {
		.def_mode = DR_STE_TYPE_BWC_DW,
		.def_type = DR_STE_DEFINER_TYPE_SRC_QP_GVMI__ETHL2_I,
	},
	[DR_STE_LU_TYPE_GENERAL_PURPOSE] = {
		.def_mode = DR_STE_TYPE_BWC_DW,
		.def_type = DR_STE_DEFINER_TYPE_GENERAL_PURPOSE__FLEX_PARSER_TNL_HEADER,
	},
	[DR_STE_LU_TYPE_FLEX_PARSER_TNL_HEADER] = {
		.def_mode = DR_STE_TYPE_BWC_BYTE,
		.def_type = DR_STE_DEFINER_TYPE_GENERAL_PURPOSE__FLEX_PARSER_TNL_HEADER,
	},
};

enum {
	MLX5_DR_ACTION_MDFY_HW_FLD_L2_OUT_0		= 0,
	MLX5_DR_ACTION_MDFY_HW_FLD_L2_OUT_1		= 1,
	MLX5_DR_ACTION_MDFY_HW_FLD_L2_OUT_2		= 2,

	MLX5_DR_ACTION_MDFY_HW_FLD_SRC_L2_OUT_0		= 8,
	MLX5_DR_ACTION_MDFY_HW_FLD_SRC_L2_OUT_1		= 9,

	MLX5_DR_ACTION_MDFY_HW_FLD_L3_OUT_0		= 14,

	MLX5_DR_ACTION_MDFY_HW_FLD_L4_OUT_0		= 24,
	MLX5_DR_ACTION_MDFY_HW_FLD_L4_OUT_1		= 25,

	MLX5_DR_ACTION_MDFY_HW_FLD_IPV4_OUT_0		= 64,
	MLX5_DR_ACTION_MDFY_HW_FLD_IPV4_OUT_1		= 65,

	MLX5_DR_ACTION_MDFY_HW_FLD_IPV6_DST_OUT_0	= 68,
	MLX5_DR_ACTION_MDFY_HW_FLD_IPV6_DST_OUT_1	= 69,
	MLX5_DR_ACTION_MDFY_HW_FLD_IPV6_DST_OUT_2	= 70,
	MLX5_DR_ACTION_MDFY_HW_FLD_IPV6_DST_OUT_3	= 71,

	MLX5_DR_ACTION_MDFY_HW_FLD_IPV6_SRC_OUT_0	= 76,
	MLX5_DR_ACTION_MDFY_HW_FLD_IPV6_SRC_OUT_1	= 77,
	MLX5_DR_ACTION_MDFY_HW_FLD_IPV6_SRC_OUT_2	= 78,
	MLX5_DR_ACTION_MDFY_HW_FLD_IPV6_SRC_OUT_3	= 79,

	MLX5_DR_ACTION_MDFY_HW_FLD_TCP_MISC_0		= 94,
	MLX5_DR_ACTION_MDFY_HW_FLD_TCP_MISC_1		= 95,

	MLX5_DR_ACTION_MDFY_HW_FLD_METADATA_2_CQE	= 123,
	MLX5_DR_ACTION_MDFY_HW_FLD_GNRL_PURPOSE		= 124,

	MLX5_DR_ACTION_MDFY_HW_FLD_REGISTER_2		= 140,
	MLX5_DR_ACTION_MDFY_HW_FLD_REGISTER_3		= 141,
	MLX5_DR_ACTION_MDFY_HW_FLD_REGISTER_4		= 142,
	MLX5_DR_ACTION_MDFY_HW_FLD_REGISTER_5		= 143,
	MLX5_DR_ACTION_MDFY_HW_FLD_REGISTER_6		= 144,
	MLX5_DR_ACTION_MDFY_HW_FLD_REGISTER_7		= 145,
};

static const struct dr_ste_modify_hdr_hw_field dr_ste_hw_modify_hdr_field_arr[] = {
	[MLX5_ACTION_IN_FIELD_OUT_SMAC_47_16] = {
		.hw_field = MLX5_DR_ACTION_MDFY_HW_FLD_SRC_L2_OUT_0, .start = 0, .end = 31,
	},
	[MLX5_ACTION_IN_FIELD_OUT_SMAC_15_0] = {
		.hw_field = MLX5_DR_ACTION_MDFY_HW_FLD_SRC_L2_OUT_1, .start = 16, .end = 31,
	},
	[MLX5_ACTION_IN_FIELD_OUT_ETHERTYPE] = {
		.hw_field = MLX5_DR_ACTION_MDFY_HW_FLD_L2_OUT_1, .start = 0, .end = 15,
	},
	[MLX5_ACTION_IN_FIELD_OUT_DMAC_47_16] = {
		.hw_field = MLX5_DR_ACTION_MDFY_HW_FLD_L2_OUT_0, .start = 0, .end = 31,
	},
	[MLX5_ACTION_IN_FIELD_OUT_DMAC_15_0] = {
		.hw_field = MLX5_DR_ACTION_MDFY_HW_FLD_L2_OUT_1, .start = 16, .end = 31,
	},
	[MLX5_ACTION_IN_FIELD_OUT_IP_DSCP] = {
		.hw_field = MLX5_DR_ACTION_MDFY_HW_FLD_L3_OUT_0, .start = 18, .end = 23,
	},
	[MLX5_ACTION_IN_FIELD_OUT_TCP_FLAGS] = {
		.hw_field = MLX5_DR_ACTION_MDFY_HW_FLD_L4_OUT_1, .start = 16, .end = 24,
		.l4_type = MLX5_DR_ACTION_MDFY_HW_HDR_L4_TCP,
	},
	[MLX5_ACTION_IN_FIELD_OUT_TCP_SPORT] = {
		.hw_field = MLX5_DR_ACTION_MDFY_HW_FLD_L4_OUT_0, .start = 16, .end = 31,
		.l4_type = MLX5_DR_ACTION_MDFY_HW_HDR_L4_TCP,
	},
	[MLX5_ACTION_IN_FIELD_OUT_TCP_DPORT] = {
		.hw_field = MLX5_DR_ACTION_MDFY_HW_FLD_L4_OUT_0, .start = 0, .end = 15,
		.l4_type = MLX5_DR_ACTION_MDFY_HW_HDR_L4_TCP,
	},
	[MLX5_ACTION_IN_FIELD_OUT_IP_TTL] = {
		.hw_field = MLX5_DR_ACTION_MDFY_HW_FLD_L3_OUT_0, .start = 8, .end = 15,
		.l3_type = MLX5_DR_ACTION_MDFY_HW_HDR_L3_IPV4,
	},
	[MLX5_ACTION_IN_FIELD_OUT_IPV6_HOPLIMIT] = {
		.hw_field = MLX5_DR_ACTION_MDFY_HW_FLD_L3_OUT_0, .start = 8, .end = 15,
		.l3_type = MLX5_DR_ACTION_MDFY_HW_HDR_L3_IPV6,
	},
	[MLX5_ACTION_IN_FIELD_OUT_UDP_SPORT] = {
		.hw_field = MLX5_DR_ACTION_MDFY_HW_FLD_L4_OUT_0, .start = 16, .end = 31,
		.l4_type = MLX5_DR_ACTION_MDFY_HW_HDR_L4_UDP,
	},
	[MLX5_ACTION_IN_FIELD_OUT_UDP_DPORT] = {
		.hw_field = MLX5_DR_ACTION_MDFY_HW_FLD_L4_OUT_0, .start = 0, .end = 15,
		.l4_type = MLX5_DR_ACTION_MDFY_HW_HDR_L4_UDP,
	},
	[MLX5_ACTION_IN_FIELD_OUT_SIPV6_127_96] = {
		.hw_field = MLX5_DR_ACTION_MDFY_HW_FLD_IPV6_SRC_OUT_0, .start = 0, .end = 31,
		.l3_type = MLX5_DR_ACTION_MDFY_HW_HDR_L3_IPV6,
	},
	[MLX5_ACTION_IN_FIELD_OUT_SIPV6_95_64] = {
		.hw_field = MLX5_DR_ACTION_MDFY_HW_FLD_IPV6_SRC_OUT_1, .start = 0, .end = 31,
		.l3_type = MLX5_DR_ACTION_MDFY_HW_HDR_L3_IPV6,
	},
	[MLX5_ACTION_IN_FIELD_OUT_SIPV6_63_32] = {
		.hw_field = MLX5_DR_ACTION_MDFY_HW_FLD_IPV6_SRC_OUT_2, .start = 0, .end = 31,
		.l3_type = MLX5_DR_ACTION_MDFY_HW_HDR_L3_IPV6,
	},
	[MLX5_ACTION_IN_FIELD_OUT_SIPV6_31_0] = {
		.hw_field = MLX5_DR_ACTION_MDFY_HW_FLD_IPV6_SRC_OUT_3, .start = 0, .end = 31,
		.l3_type = MLX5_DR_ACTION_MDFY_HW_HDR_L3_IPV6,
	},
	[MLX5_ACTION_IN_FIELD_OUT_DIPV6_127_96] = {
		.hw_field = MLX5_DR_ACTION_MDFY_HW_FLD_IPV6_DST_OUT_0, .start = 0, .end = 31,
		.l3_type = MLX5_DR_ACTION_MDFY_HW_HDR_L3_IPV6,
	},
	[MLX5_ACTION_IN_FIELD_OUT_DIPV6_95_64] = {
		.hw_field = MLX5_DR_ACTION_MDFY_HW_FLD_IPV6_DST_OUT_1, .start = 0, .end = 31,
		.l3_type = MLX5_DR_ACTION_MDFY_HW_HDR_L3_IPV6,
	},
	[MLX5_ACTION_IN_FIELD_OUT_DIPV6_63_32] = {
		.hw_field = MLX5_DR_ACTION_MDFY_HW_FLD_IPV6_DST_OUT_2, .start = 0, .end = 31,
		.l3_type = MLX5_DR_ACTION_MDFY_HW_HDR_L3_IPV6,
	},
	[MLX5_ACTION_IN_FIELD_OUT_DIPV6_31_0] = {
		.hw_field = MLX5_DR_ACTION_MDFY_HW_FLD_IPV6_DST_OUT_3, .start = 0, .end = 31,
		.l3_type = MLX5_DR_ACTION_MDFY_HW_HDR_L3_IPV6,
	},
	[MLX5_ACTION_IN_FIELD_OUT_SIPV4] = {
		.hw_field = MLX5_DR_ACTION_MDFY_HW_FLD_IPV4_OUT_0, .start = 0, .end = 31,
		.l3_type = MLX5_DR_ACTION_MDFY_HW_HDR_L3_IPV4,
	},
	[MLX5_ACTION_IN_FIELD_OUT_DIPV4] = {
		.hw_field = MLX5_DR_ACTION_MDFY_HW_FLD_IPV4_OUT_1, .start = 0, .end = 31,
		.l3_type = MLX5_DR_ACTION_MDFY_HW_HDR_L3_IPV4,
	},
	[MLX5_ACTION_IN_FIELD_OUT_METADATA_REGA] = {
		.hw_field = MLX5_DR_ACTION_MDFY_HW_FLD_GNRL_PURPOSE, .start = 0, .end = 31,
	},
	[MLX5_ACTION_IN_FIELD_OUT_METADATA_REGB] = {
		.hw_field = MLX5_DR_ACTION_MDFY_HW_FLD_METADATA_2_CQE, .start = 0, .end = 31,
	},
	[MLX5_ACTION_IN_FIELD_OUT_METADATA_REGC_0] = {
		.hw_field = MLX5_DR_ACTION_MDFY_HW_FLD_REGISTER_6, .start = 0, .end = 31,
	},
	[MLX5_ACTION_IN_FIELD_OUT_METADATA_REGC_1] = {
		.hw_field = MLX5_DR_ACTION_MDFY_HW_FLD_REGISTER_7, .start = 0, .end = 31,
	},
	[MLX5_ACTION_IN_FIELD_OUT_METADATA_REGC_2] = {
		.hw_field = MLX5_DR_ACTION_MDFY_HW_FLD_REGISTER_4, .start = 0, .end = 31,
	},
	[MLX5_ACTION_IN_FIELD_OUT_METADATA_REGC_3] = {
		.hw_field = MLX5_DR_ACTION_MDFY_HW_FLD_REGISTER_5, .start = 0, .end = 31,
	},
	[MLX5_ACTION_IN_FIELD_OUT_METADATA_REGC_4] = {
		.hw_field = MLX5_DR_ACTION_MDFY_HW_FLD_REGISTER_2, .start = 0, .end = 31,
	},
	[MLX5_ACTION_IN_FIELD_OUT_METADATA_REGC_5] = {
		.hw_field = MLX5_DR_ACTION_MDFY_HW_FLD_REGISTER_3, .start = 0, .end = 31,
	},
	[MLX5_ACTION_IN_FIELD_OUT_TCP_SEQ_NUM] = {
		.hw_field = MLX5_DR_ACTION_MDFY_HW_FLD_TCP_MISC_0, .start = 0, .end = 31,
	},
	[MLX5_ACTION_IN_FIELD_OUT_TCP_ACK_NUM] = {
		.hw_field = MLX5_DR_ACTION_MDFY_HW_FLD_TCP_MISC_1, .start = 0, .end = 31,
	},
	[MLX5_ACTION_IN_FIELD_OUT_FIRST_VID] = {
		.hw_field = MLX5_DR_ACTION_MDFY_HW_FLD_L2_OUT_2, .start = 0, .end = 15,
	},\
};

static const struct dr_ste_lu_type_info *
dr_ste_lu_type_conv(enum dr_ste_lu_type lu_type)
{
	const struct dr_ste_lu_type_info *lu_type_info;

	if (lu_type >= ARRAY_SIZE(dr_ste_lu_type_conv_arr))
		assert(false);

	lu_type_info = &dr_ste_lu_type_conv_arr[lu_type];
	if (lu_type && !lu_type_info->def_type)
		assert(false);

	return lu_type_info;
}

static void dr_ste_hw_set_entry_format(uint8_t *hw_ste_p, uint8_t entry_format)
{
	DR_STE_SET(match_bwc, hw_ste_p, entry_format, entry_format);
}

static uint8_t dr_ste_hw_get_entry_format(uint8_t *hw_ste_p)
{
	return DR_STE_GET(match_bwc, hw_ste_p, entry_format);
}

static void dr_ste_hw_set_miss_addr(uint8_t *hw_ste_p, uint64_t miss_addr)
{
	uint64_t index = miss_addr >> 6;

	DR_STE_SET(match_bwc, hw_ste_p, miss_address_39_32, index >> 26);
	DR_STE_SET(match_bwc, hw_ste_p, miss_address_31_6, index);
}

static uint64_t dr_ste_hw_get_miss_addr(uint8_t *hw_ste_p)
{
	uint64_t index =
		(DR_STE_GET(match_bwc, hw_ste_p, miss_address_31_6) |
		 DR_STE_GET(match_bwc, hw_ste_p, miss_address_39_32) << 26);

	return index << 6;
}

static void dr_ste_hw_set_byte_mask(uint8_t *hw_ste_p, uint16_t byte_mask)
{
	DR_STE_SET(match_bwc, hw_ste_p, byte_mask, byte_mask);
}

static uint16_t dr_ste_hw_get_byte_mask(uint8_t *hw_ste_p)
{
	return DR_STE_GET(match_bwc, hw_ste_p, byte_mask);
}

static void dr_ste_hw_set_lu_type(uint8_t *hw_ste_p, uint8_t lu_type)
{
	const struct dr_ste_lu_type_info *lu_type_info;

	lu_type_info = dr_ste_lu_type_conv(lu_type);

	DR_STE_SET(match_bwc, hw_ste_p, entry_format, lu_type_info->def_mode);
	DR_STE_SET(match_bwc, hw_ste_p, match_definer_ctx_idx, lu_type_info->def_type);
}

static void dr_ste_hw_set_next_lu_type(uint8_t *hw_ste_p, uint8_t lu_type)
{
	const struct dr_ste_lu_type_info *lu_type_info;

	lu_type_info = dr_ste_lu_type_conv(lu_type);

	DR_STE_SET(match_bwc, hw_ste_p, next_entry_format, lu_type_info->def_mode);
	DR_STE_SET(match_bwc, hw_ste_p, hash_definer_ctx_idx, lu_type_info->def_type);
}

static void dr_ste_hw_set_hit_gvmi(uint8_t *hw_ste_p, uint16_t gvmi)
{
	DR_STE_SET(match_bwc, hw_ste_p, next_table_base_63_48, gvmi);
}

static void dr_ste_hw_set_hit_addr(uint8_t *hw_ste_p, uint64_t icm_addr, uint32_t ht_size)
{
	uint64_t index = (icm_addr >> 5) | ht_size;

	DR_STE_SET(match_bwc, hw_ste_p, next_table_base_39_32_size, index >> 27);
	DR_STE_SET(match_bwc, hw_ste_p, next_table_base_31_5_size, index);
}

static void dr_ste_hw_init(uint8_t *hw_ste_p, uint8_t lu_type,
			   uint8_t entry_type, uint16_t gvmi)
{
	dr_ste_hw_set_lu_type(hw_ste_p, lu_type);
	dr_ste_hw_set_next_lu_type(hw_ste_p, DR_STE_LU_TYPE_DONT_CARE);

	DR_STE_SET(match_bwc, hw_ste_p, gvmi, gvmi);
	DR_STE_SET(match_bwc, hw_ste_p, next_table_base_63_48, gvmi);
	DR_STE_SET(match_bwc, hw_ste_p, miss_address_63_48, gvmi);
}

static void dr_ste_hw_init_match(uint8_t *hw_ste_p, uint8_t lu_type, uint16_t gvmi)
{
	void *actions;

	dr_ste_hw_set_lu_type(hw_ste_p, lu_type);
	dr_ste_hw_set_next_lu_type(hw_ste_p, DR_STE_LU_TYPE_DONT_CARE);
	dr_ste_hw_set_entry_format(hw_ste_p, DR_STE_TYPE_MATCH);

	actions = DEVX_ADDR_OF(ste_mask_and_match, hw_ste_p, action);
	memset(actions, 0, DEVX_FLD_SZ_BYTES(ste_mask_and_match, action));

	DR_STE_SET(match_bwc, hw_ste_p, next_table_base_63_48, gvmi);
	DR_STE_SET(match_bwc, hw_ste_p, miss_address_63_48, gvmi);
}

// TODO merge with present enum
enum {
	STE_ACTION_SIZE_SINGLE = 1,
	STE_ACTION_SIZE_DOUBLE = 2,
};

/* translate action_type to action_id which it is implemented with */
static enum dr_ste_action_id dr_ste_get_action_id(enum dr_ste_entry_type ste_type,
						  enum dr_action_type action_type)
{
	if (ste_type == DR_STE_TYPE_RX) {
		switch (action_type) {
		case DR_ACTION_TYP_TNL_L2_TO_L2:
			return DR_STE_ACTION_ID_REMOVE_HEADER_TO_HEADER;
		case DR_ACTION_TYP_TNL_L3_TO_L2:
		case DR_ACTION_TYP_MODIFY_HDR:
			return DR_STE_ACTION_ID_MODIFY_LIST;
		case DR_ACTION_TYP_POP_VLAN:
			return DR_STE_ACTION_ID_REMOVE_BY_SIZE;
		case DR_ACTION_TYP_TAG:
			return DR_STE_ACTION_ID_FLOW_TAG;
		default:
			assert(false);
			break;
		}
	} else {
		switch (action_type) {
		case DR_ACTION_TYP_MODIFY_HDR:
			return DR_STE_ACTION_ID_MODIFY_LIST;
		case DR_ACTION_TYP_PUSH_VLAN:
			return DR_STE_ACTION_ID_INSERT_INLINE;
		case DR_ACTION_TYP_L2_TO_TNL_L2:
			return DR_STE_ACTION_ID_INSERT_POINTER;
		case DR_ACTION_TYP_L2_TO_TNL_L3:
			return DR_STE_ACTION_ID_SPECIAL_ENCAP_L3;
		default:
			assert(false);
			break;
		}
	}
	assert(false);
	return -1;
}

static void dr_ste_hw_rx_set_flow_tag(uint8_t *s_action, uint32_t flow_tag)
{
	enum dr_ste_action_id action_id =
		dr_ste_get_action_id(DR_STE_TYPE_RX, DR_ACTION_TYP_TAG);

	DR_STE_SET(single_action_flow_tag, s_action, action_id, action_id);
	DR_STE_SET(single_action_flow_tag, s_action, flow_tag, flow_tag);
}

static void dr_ste_hw_set_counter_id(uint8_t *hw_ste_p, uint32_t ctr_id)
{
	DR_STE_SET(match_bwc, hw_ste_p, counter_id, ctr_id);
}

static void dr_ste_hw_set_reparse(uint8_t *hw_ste_p)
{
	DR_STE_SET(match_bwc, hw_ste_p, reparse, 1);
}

static void dr_ste_hw_set_tx_push_vlan(uint8_t *ste, uint8_t *d_action,
				       uint32_t vlan_hdr)
{
	enum dr_ste_action_id action_id =
		dr_ste_get_action_id(DR_STE_TYPE_TX,
				     DR_ACTION_TYP_PUSH_VLAN);

	DR_STE_SET(double_action_insert_with_inline, d_action, action_id,
		   action_id);
	/* The hardware expects here offset to vlan header in words (2 byte) */
	DR_STE_SET(double_action_insert_with_inline, d_action, start_offset,
		   HDR_LEN_L2_MACS >> 1);
	DR_STE_SET(double_action_insert_with_inline, d_action, inline_data, vlan_hdr);

	dr_ste_hw_set_reparse(ste);
}

static void dr_ste_hw_set_rx_pop_vlan(uint8_t *hw_ste_p, uint8_t *s_action)
{
	enum dr_ste_action_id action_id =
		dr_ste_get_action_id(DR_STE_TYPE_RX,
				     DR_ACTION_TYP_POP_VLAN);

	DR_STE_SET(single_action_remove_header_size, s_action, action_id,
		   action_id);
	DR_STE_SET(single_action_remove_header_size, s_action, start_anchor,
		   DR_STE_HEADER_ANCHOR_1ST_VLAN);
	/* The hardware expects here size in words (2 byte) */
	DR_STE_SET(single_action_remove_header_size, s_action, remove_size,
		   HDR_LEN_L2_VLAN >> 1);

	dr_ste_hw_set_reparse(hw_ste_p);
}

static void dr_ste_hw_set_tx_encap(uint8_t *hw_ste_p, uint8_t *d_action,
				   uint32_t reformat_id, int size)
{
	enum dr_ste_action_id action_id =
		dr_ste_get_action_id(DR_STE_TYPE_TX,
				     DR_ACTION_TYP_L2_TO_TNL_L2);

	DR_STE_SET(double_action_insert_with_ptr, d_action, action_id,
		   action_id);

	/* The hardware expects here size in words (2 byte) */
	DR_STE_SET(double_action_insert_with_ptr, d_action, size, size / 2);
	DR_STE_SET(double_action_insert_with_ptr, d_action, pointer, reformat_id);

	dr_ste_hw_set_reparse(hw_ste_p);
}

static void dr_ste_hw_set_rx_decap(uint8_t *hw_ste_p, uint8_t *s_action)
{
	enum dr_ste_action_id action_id =
		dr_ste_get_action_id(DR_STE_TYPE_RX,
				     DR_ACTION_TYP_TNL_L2_TO_L2);

	DR_STE_SET(single_action_remove_header, s_action, action_id,
		   action_id);

	DR_STE_SET(single_action_remove_header, s_action, decap, 1);
	DR_STE_SET(single_action_remove_header, s_action, vni_to_cqe, 1);
	DR_STE_SET(single_action_remove_header, s_action, end_anchor,
		   DR_STE_HEADER_ANCHOR_INNER_MAC);

	dr_ste_hw_set_reparse(hw_ste_p);
}

static void dr_ste_hw_set_rx_decap_l3(uint8_t *hw_ste_p,
				      uint8_t *s_action,
				      uint16_t decap_actions,
				      uint32_t decap_index)
{
	enum dr_ste_action_id action_id =
		dr_ste_get_action_id(DR_STE_TYPE_RX,
				     DR_ACTION_TYP_TNL_L3_TO_L2);

	DR_STE_SET(single_action_modify_list, s_action, action_id, action_id);

	DR_STE_SET(single_action_modify_list, s_action, num_of_modify_actions,
		   decap_actions);
	DR_STE_SET(single_action_modify_list, s_action, modify_actions_ptr,
		   decap_index);

	dr_ste_hw_set_reparse(hw_ste_p);
}

static void dr_ste_hw_set_rewrite_actions(uint8_t *hw_ste_p,
					  uint8_t *s_action,
					  uint16_t num_of_actions,
					  uint32_t re_write_index)
{
	enum dr_ste_action_id action_id =
		dr_ste_get_action_id(DR_STE_TYPE_RX, DR_ACTION_TYP_MODIFY_HDR);

	DR_STE_SET(single_action_modify_list, s_action, action_id, action_id);
	DR_STE_SET(single_action_modify_list, s_action, num_of_modify_actions,
		   num_of_actions);
	DR_STE_SET(single_action_modify_list, s_action, modify_actions_ptr,
		   re_write_index);

	dr_ste_hw_set_reparse(hw_ste_p);
}

#define DR_MODIFY_HEADER_QW_OFFSET (0x20)

static void dr_ste_hw_set_set_action(uint8_t *d_action,
				     uint8_t hw_field,
				     uint8_t shifter,
				     uint8_t length,
				     uint32_t data)
{
	length = (length == 32) ? 0 : length;
	shifter += DR_MODIFY_HEADER_QW_OFFSET;
	DR_STE_SET(double_action_set, d_action, action_id, DR_STE_ACTION_ID_SET);
	DR_STE_SET(double_action_set, d_action, destination_dw_offset, hw_field);
	DR_STE_SET(double_action_set, d_action, destination_left_shifter, shifter);
	DR_STE_SET(double_action_set, d_action, destination_length, length);
	DR_STE_SET(double_action_set, d_action, inline_data, data);
}

static void dr_ste_hw_set_add_action(uint8_t *d_action,
				     uint8_t hw_field,
				     uint8_t shifter,
				     uint8_t length,
				     uint32_t data)
{
	length = (length == 32) ? 0 : length;
	shifter += DR_MODIFY_HEADER_QW_OFFSET;
	DR_STE_SET(double_action_add, d_action, action_id, DR_STE_ACTION_ID_ADD);
	DR_STE_SET(double_action_add, d_action, destination_dw_offset, hw_field);
	DR_STE_SET(double_action_add, d_action, destination_left_shifter, shifter);
	DR_STE_SET(double_action_add, d_action, destination_length, length);
	DR_STE_SET(double_action_add, d_action, add_value, data);
}

static void dr_ste_hw_set_copy_action(uint8_t *d_action,
				      uint8_t dst_hw_field,
				      uint8_t dst_shifter,
				      uint8_t dst_len,
				      uint8_t src_hw_field,
				      uint8_t src_shifter)
{
	dst_shifter += DR_MODIFY_HEADER_QW_OFFSET;
	src_shifter += DR_MODIFY_HEADER_QW_OFFSET;
	DR_STE_SET(double_action_copy, d_action, action_id, DR_STE_ACTION_ID_COPY);
	DR_STE_SET(double_action_copy, d_action, destination_dw_offset, dst_hw_field);
	DR_STE_SET(double_action_copy, d_action, destination_left_shifter, dst_shifter);
	DR_STE_SET(double_action_copy, d_action, destination_length, dst_len);
	DR_STE_SET(double_action_copy, d_action, source_dw_offset, src_hw_field);
	DR_STE_SET(double_action_copy, d_action, source_right_shifter, src_shifter);
}

#define DR_STE_DECAP_L3_ACTION_NUM	8
#define DR_STE_L2_HDR_MAX_SZ		20
#define DR_STE_INLINE_DATA_SZ		4

static int
dr_ste_hw_set_decap_l3_action_list(void *data, uint32_t data_sz,
				   uint8_t *hw_action, uint32_t hw_action_sz,
				   uint16_t *used_hw_action_num)
{
	uint8_t padded_data[DR_STE_L2_HDR_MAX_SZ] = {};
	void *data_ptr = padded_data;
	uint16_t used_actions = 0;
	uint32_t i;

	if (hw_action_sz / DR_STE_ACTION_DOUBLE_SZ < DR_STE_DECAP_L3_ACTION_NUM) {
		errno = EINVAL;
		return errno;
	}

	memcpy(padded_data, data, data_sz);

	/* Remove L2L3 outer headers */
	DR_STE_SET(single_action_remove_header, hw_action, action_id,
		   DR_STE_ACTION_ID_REMOVE_HEADER_TO_HEADER);
	DR_STE_SET(single_action_remove_header, hw_action, decap, 1);
	DR_STE_SET(single_action_remove_header, hw_action, vni_to_cqe, 1);
	DR_STE_SET(single_action_remove_header, hw_action, end_anchor,
		   DR_STE_HEADER_ANCHOR_INNER_IPV6_IPV4);
	hw_action += DR_STE_ACTION_DOUBLE_SZ;
	used_actions += 2; /* one for remove one for NOP */

	/* Add the new header inline + 2 extra bytes */
	for (i = 0; i < data_sz / DR_STE_INLINE_DATA_SZ + 1; i++) {
		void *addr_inline;

		DR_STE_SET(double_action_insert_with_inline, hw_action, action_id,
			   DR_STE_ACTION_ID_INSERT_INLINE);
		/* The hardware expects here offset to words (2 byte) */
		DR_STE_SET(double_action_insert_with_inline, hw_action, start_offset,
			   i * 2);

		/* Copy byte byte in order to skip endianness problem */
		addr_inline = DEVX_ADDR_OF(ste_double_action_insert_with_inline,
					   hw_action, inline_data);
		memcpy(addr_inline, data_ptr, DR_STE_INLINE_DATA_SZ);
		hw_action += DR_STE_ACTION_DOUBLE_SZ;
		data_ptr += DR_STE_INLINE_DATA_SZ;
		used_actions++;
	}

	/* Remove 2 extra bytes */
	DR_STE_SET(single_action_remove_header_size, hw_action, action_id,
		   DR_STE_ACTION_ID_REMOVE_BY_SIZE);
	DR_STE_SET(single_action_remove_header_size, hw_action, start_offset, data_sz / 2);
	/* The hardware expects here size in words (2 byte) */
	DR_STE_SET(single_action_remove_header_size, hw_action, remove_size, 1);
	used_actions++;

	*used_hw_action_num = used_actions;

	return 0;
}

static void dr_ste_hw_prepare_ste(uint8_t *hw_ste_p, uint32_t size)
{
	struct dr_hw_ste_format *hw_ste = (struct dr_hw_ste_format *)hw_ste_p;
	uint8_t tmp_tag[DR_STE_SIZE_TAG] = {};

	if (size == DR_STE_SIZE_CTRL)
		return;

	if (size != DR_STE_SIZE)
		assert(false);

	/* Backup mask */
	memcpy(tmp_tag, hw_ste->tag, DR_STE_SIZE_MASK);

	/* Swap mask and tag  both are the same size */
	memcpy(&hw_ste->tag, &hw_ste->mask, DR_STE_SIZE_MASK);
	memcpy(&hw_ste->mask, tmp_tag, DR_STE_SIZE_TAG);
}

static void dr_ste_hw_arr_init_next(uint8_t **last_hw_ste,
				    uint32_t *added_stes,
				    uint16_t gvmi)
{
	(*added_stes)++;
	*last_hw_ste += DR_STE_SIZE;
	dr_ste_hw_init_match(*last_hw_ste, DR_STE_LU_TYPE_DONT_CARE, gvmi);
}

static int dr_ste_get_max_actions(uint8_t *hw_ste_p)
{
	enum dr_ste_entry_format entry_type =
		dr_ste_hw_get_entry_format(hw_ste_p);

	switch (entry_type) {
	case DR_STE_TYPE_BWC_BYTE:
	case DR_STE_TYPE_BWC_DW:
		return 2;
	case DR_STE_TYPE_MATCH:
		return 3;
	default:
		assert(false);
		return -1;
	}
}

static int dr_ste_get_action_size_by_action_id(uint8_t *ste,
					       enum dr_ste_action_id action_hw_id)
{
	switch (action_hw_id) {
	case DR_STE_ACTION_ID_REMOVE_BY_SIZE:
	case DR_STE_ACTION_ID_REMOVE_HEADER_TO_HEADER:
	case DR_STE_ACTION_ID_FLOW_TAG:
		return STE_ACTION_SIZE_SINGLE;
	case DR_STE_ACTION_ID_MODIFY_LIST:
		/* Modify always alone in ste (C37) */
		return dr_ste_get_max_actions(ste);
	case DR_STE_ACTION_ID_COPY:
	case DR_STE_ACTION_ID_SET:
	case DR_STE_ACTION_ID_ADD:
	case DR_STE_ACTION_ID_INSERT_INLINE:
	case DR_STE_ACTION_ID_INSERT_POINTER:
	case DR_STE_ACTION_ID_ACCELERATED_LIST:
		return STE_ACTION_SIZE_DOUBLE;
	case DR_STE_ACTION_ID_SPECIAL_ENCAP_L3: /* used for l3_encap, 2 action id's */
		return STE_ACTION_SIZE_DOUBLE + STE_ACTION_SIZE_SINGLE;
	default:
		assert(false);
		return -1;
	}
}

static int dr_ste_get_action_size(enum dr_ste_entry_type ste_type,
				  enum dr_action_type action_type,
				  uint8_t *ste)
{
	int action_hw_id;

	if (action_type == DR_ACTION_TYP_CTR)
		return 0;

	action_hw_id = dr_ste_get_action_id(ste_type, action_type);

	return dr_ste_get_action_size_by_action_id(ste, action_hw_id);
}

#define DR_ACTION_SIZE_IN_STE 4

static void dr_ste_set_action_place(uint8_t *ste,
				    uint8_t curr_actions_in_ste,
				    uint8_t **action)
{
	enum dr_ste_entry_format entry_type =
		dr_ste_hw_get_entry_format(ste);

	switch (entry_type) {
	case DR_STE_TYPE_BWC_BYTE:
	case DR_STE_TYPE_BWC_DW:
		*action = DEVX_ADDR_OF(ste_match_bwc, ste, action);
		break;
	case DR_STE_TYPE_MATCH:
		*action = DEVX_ADDR_OF(ste_mask_and_match, ste, action);
		break;
	default:
		assert(false);
		return;
	}

	*action += curr_actions_in_ste * DR_ACTION_SIZE_IN_STE;
}

static bool dr_ste_is_encap_decap_action(enum dr_action_type action)
{
	if (action == DR_ACTION_TYP_TNL_L2_TO_L2 ||
	    action == DR_ACTION_TYP_L2_TO_TNL_L2 ||
	    action == DR_ACTION_TYP_TNL_L3_TO_L2 ||
	    action == DR_ACTION_TYP_L2_TO_TNL_L3)
		return true;

	return false;
}

static void dr_ste_hw_arr_prepare_next(enum dr_action_type cur_action,
				       enum dr_action_type prev_action,
				       uint8_t *num_current_actions,
				       uint8_t **last_hw_ste,
				       uint8_t **action,
				       uint32_t *added_stes,
				       enum dr_ste_entry_type entry_type,
				       uint16_t gvmi)
{
	int action_size;

	if (cur_action == DR_ACTION_TYP_CTR)
		return;

	action_size = dr_ste_get_action_size(entry_type, cur_action,
					     *last_hw_ste);

	/* modify_action or no-more-place handled here */
	if (*num_current_actions + action_size >
	    dr_ste_get_max_actions(*last_hw_ste)) {
		dr_ste_hw_arr_init_next(last_hw_ste, added_stes, gvmi);
		*num_current_actions = action_size;
		dr_ste_set_action_place(*last_hw_ste, 0, action);
		return;
	}

	/* calc according HW constrains */
	switch (cur_action) {
	case DR_ACTION_TYP_TNL_L3_TO_L2:/* part of modify_list, handled */
	case DR_ACTION_TYP_L2_TO_TNL_L2:
	case DR_ACTION_TYP_L2_TO_TNL_L3:
	case DR_ACTION_TYP_TNL_L2_TO_L2:
		break;
	case DR_ACTION_TYP_POP_VLAN:
	case DR_ACTION_TYP_PUSH_VLAN:
		/* vlan can only be before encap/decap */
		if (dr_ste_is_encap_decap_action(prev_action))
			goto add_to_next_ste;
		break;
	default:
		break;
	}

	/* handle action at the current ste */
	dr_ste_set_action_place(*last_hw_ste, *num_current_actions, action);
	*num_current_actions += action_size;

	return;

add_to_next_ste:
	dr_ste_hw_arr_init_next(last_hw_ste, added_stes, gvmi);
	*num_current_actions = action_size;
	dr_ste_set_action_place(*last_hw_ste, 0, action);
}

static void dr_ste_hw_set_tx_encap_l3(uint8_t *ste, uint8_t *frst_s_action,
				      uint32_t reformat_id,
				      int size)
{
	uint8_t *scnd_d_action;
	int first_action_size;

	assert(dr_ste_hw_get_entry_format(ste) == DR_STE_TYPE_MATCH);

	/* Remove L2 headers */
	DR_STE_SET(single_action_remove_header, frst_s_action, action_id,
		   DR_STE_ACTION_ID_REMOVE_HEADER_TO_HEADER);
	DR_STE_SET(single_action_remove_header, frst_s_action, end_anchor,
		   DR_STE_HEADER_ANCHOR_IPV6_IPV4);

	first_action_size =
		dr_ste_get_action_size_by_action_id(ste,
						    DR_STE_ACTION_ID_REMOVE_HEADER_TO_HEADER);

	dr_ste_set_action_place(ste, first_action_size, &scnd_d_action);

	/* Encapsulate with given reformat ID */
	DR_STE_SET(double_action_insert_with_ptr, scnd_d_action, action_id,
		   DR_STE_ACTION_ID_INSERT_POINTER);
	/* The hardware expects here size in words (2 byte) */
	DR_STE_SET(double_action_insert_with_ptr, scnd_d_action, size, size / 2);
	DR_STE_SET(double_action_insert_with_ptr, scnd_d_action, pointer, reformat_id);

	dr_ste_hw_set_reparse(ste);

}

static void dr_ste_hw_set_actions_tx(uint8_t *action_type_set,
				     uint8_t *hw_ste_arr,
				     struct dr_ste_actions_attr *attr,
				     uint32_t *added_stes)
{
	enum dr_action_type prev_action = DR_ACTION_TYP_MAX;
	uint8_t num_of_action_slots = 0;
	uint8_t *action;

	/* We want to make sure the modify header comes before L2
	 * encapsulation. The reason for that is that we support
	 * modify headers for outer headers only
	 */
	if (action_type_set[DR_ACTION_TYP_MODIFY_HDR]) {
		dr_ste_hw_arr_prepare_next(DR_ACTION_TYP_MODIFY_HDR,
					   prev_action, &num_of_action_slots,
					   &hw_ste_arr, &action, added_stes,
					   DR_STE_TYPE_TX, attr->gvmi);

		prev_action = DR_ACTION_TYP_MODIFY_HDR;

		dr_ste_hw_set_rewrite_actions(hw_ste_arr, action,
					      attr->modify_actions,
					      attr->modify_index);
	}

	if (action_type_set[DR_ACTION_TYP_PUSH_VLAN]) {
		int i;

		for (i = 0; i < attr->vlans.count; i++) {
			dr_ste_hw_arr_prepare_next(DR_ACTION_TYP_PUSH_VLAN,
						   prev_action,
						   &num_of_action_slots,
						   &hw_ste_arr, &action,
						   added_stes,
						   DR_STE_TYPE_TX,
						   attr->gvmi);

			prev_action = DR_ACTION_TYP_PUSH_VLAN;

			dr_ste_hw_set_tx_push_vlan(hw_ste_arr, action,
						   attr->vlans.headers[i]);
		}
	}

	if (action_type_set[DR_ACTION_TYP_L2_TO_TNL_L2]) {
		dr_ste_hw_arr_prepare_next(DR_ACTION_TYP_L2_TO_TNL_L2,
					   prev_action, &num_of_action_slots,
					   &hw_ste_arr, &action, added_stes,
					   DR_STE_TYPE_TX, attr->gvmi);

		prev_action = DR_ACTION_TYP_L2_TO_TNL_L2;

		dr_ste_hw_set_tx_encap(hw_ste_arr, action,
				       attr->reformat_id,
				       attr->reformat_size);
	}

	if (action_type_set[DR_ACTION_TYP_L2_TO_TNL_L3]) {
		dr_ste_hw_arr_prepare_next(DR_ACTION_TYP_L2_TO_TNL_L3,
					   prev_action, &num_of_action_slots,
					   &hw_ste_arr, &action, added_stes,
					   DR_STE_TYPE_TX, attr->gvmi);

		prev_action = DR_ACTION_TYP_L2_TO_TNL_L3;

		dr_ste_hw_set_tx_encap_l3(hw_ste_arr, action,
					  attr->reformat_id,
					  attr->reformat_size);
	}

	if (action_type_set[DR_ACTION_TYP_CTR]) {
		dr_ste_hw_arr_prepare_next(DR_ACTION_TYP_CTR, prev_action,
					   &num_of_action_slots, &hw_ste_arr,
					   &action, added_stes, DR_STE_TYPE_TX,
					   attr->gvmi);
		dr_ste_hw_set_counter_id(hw_ste_arr, attr->ctr_id);
	}

	dr_ste_hw_set_hit_gvmi(hw_ste_arr, attr->hit_gvmi);
	dr_ste_hw_set_hit_addr(hw_ste_arr, attr->final_icm_addr, 1);
}

static void dr_ste_hw_set_actions_rx(uint8_t *action_type_set,
				     uint8_t *hw_ste_arr,
				     struct dr_ste_actions_attr *attr,
				     uint32_t *added_stes)
{
	enum dr_action_type prev_action = DR_ACTION_TYP_MAX;
	uint8_t num_of_action_slots = 0;
	uint8_t *action;

	if (action_type_set[DR_ACTION_TYP_CTR]) {
		dr_ste_hw_arr_prepare_next(DR_ACTION_TYP_CTR, prev_action,
					   &num_of_action_slots, &hw_ste_arr,
					   &action, added_stes, DR_STE_TYPE_RX,
					   attr->gvmi);
		dr_ste_hw_set_counter_id(hw_ste_arr, attr->ctr_id);
		prev_action = DR_ACTION_TYP_CTR;
	}

	if (action_type_set[DR_ACTION_TYP_POP_VLAN]) {
		int i;

		for (i = 0; i < attr->vlans.count; i++) {
			dr_ste_hw_arr_prepare_next(DR_ACTION_TYP_POP_VLAN,
						   prev_action, &num_of_action_slots,
						   &hw_ste_arr, &action, added_stes,
						   DR_STE_TYPE_RX, attr->gvmi);
			prev_action = DR_ACTION_TYP_POP_VLAN;
			dr_ste_hw_set_rx_pop_vlan(hw_ste_arr, action);
		}
	}

	if (action_type_set[DR_ACTION_TYP_TNL_L3_TO_L2]) {
		dr_ste_hw_arr_prepare_next(DR_ACTION_TYP_TNL_L3_TO_L2,
					   prev_action, &num_of_action_slots,
					   &hw_ste_arr, &action, added_stes,
					   DR_STE_TYPE_RX, attr->gvmi);

		prev_action = DR_ACTION_TYP_TNL_L3_TO_L2;

		dr_ste_hw_set_rx_decap_l3(hw_ste_arr, action,
					  attr->decap_actions,
					  attr->decap_index);
	}

	if (action_type_set[DR_ACTION_TYP_TNL_L2_TO_L2]) {
		dr_ste_hw_arr_prepare_next(DR_ACTION_TYP_TNL_L2_TO_L2,
					   prev_action, &num_of_action_slots,
					   &hw_ste_arr, &action, added_stes,
					   DR_STE_TYPE_RX, attr->gvmi);

		prev_action = DR_ACTION_TYP_TNL_L2_TO_L2;

		dr_ste_hw_set_rx_decap(hw_ste_arr, action);
	}

	if (action_type_set[DR_ACTION_TYP_TAG]) {
		dr_ste_hw_arr_prepare_next(DR_ACTION_TYP_TAG,
					   prev_action, &num_of_action_slots,
					   &hw_ste_arr, &action, added_stes,
					   DR_STE_TYPE_RX, attr->gvmi);
		prev_action = DR_ACTION_TYP_TAG;

		dr_ste_hw_rx_set_flow_tag(action, attr->flow_tag);
	}

	if (action_type_set[DR_ACTION_TYP_MODIFY_HDR]) {
		dr_ste_hw_arr_prepare_next(DR_ACTION_TYP_MODIFY_HDR,
					   prev_action, &num_of_action_slots,
					   &hw_ste_arr, &action, added_stes,
					   DR_STE_TYPE_RX, attr->gvmi);

		prev_action = DR_ACTION_TYP_MODIFY_HDR;

		dr_ste_hw_set_rewrite_actions(hw_ste_arr, action,
					      attr->modify_actions,
					      attr->modify_index);
	}

	dr_ste_hw_set_hit_gvmi(hw_ste_arr, attr->hit_gvmi);
	dr_ste_hw_set_hit_addr(hw_ste_arr, attr->final_icm_addr, 1);
}

static int dr_ste_build_eth_l2_src_des_bit_mask(struct dr_match_param *value,
						bool inner, uint8_t *bit_mask)
{
	struct dr_match_spec *mask = inner ? &value->inner : &value->outer;

	DR_STE_SET_MASK_V(eth_l2_src_dst_v1, bit_mask, dmac_47_16, mask, dmac_47_16);
	DR_STE_SET_MASK_V(eth_l2_src_dst_v1, bit_mask, dmac_15_0, mask, dmac_15_0);

	DR_STE_SET_MASK_V(eth_l2_src_dst_v1, bit_mask, smac_47_16, mask, smac_47_16);
	DR_STE_SET_MASK_V(eth_l2_src_dst_v1, bit_mask, smac_15_0, mask, smac_15_0);

	DR_STE_SET_MASK_V(eth_l2_src_dst_v1, bit_mask, first_vlan_id, mask, first_vid);
	DR_STE_SET_MASK_V(eth_l2_src_dst_v1, bit_mask, first_cfi, mask, first_cfi);
	DR_STE_SET_MASK_V(eth_l2_src_dst_v1, bit_mask, first_priority, mask, first_prio);
	DR_STE_SET_MASK(eth_l2_src_dst_v1, bit_mask, l3_type, mask, ip_version);

	if (mask->cvlan_tag) {
		DR_STE_SET(eth_l2_src_dst_v1, bit_mask, first_vlan_qualifier, -1);
		mask->cvlan_tag = 0;
	} else if (mask->svlan_tag) {
		DR_STE_SET(eth_l2_src_dst_v1, bit_mask, first_vlan_qualifier, -1);
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

	DR_STE_SET_TAG(eth_l2_src_dst_v1, tag, dmac_47_16, spec, dmac_47_16);
	DR_STE_SET_TAG(eth_l2_src_dst_v1, tag, dmac_15_0, spec, dmac_15_0);

	DR_STE_SET_TAG(eth_l2_src_dst_v1, tag, smac_47_16, spec, smac_47_16);
	DR_STE_SET_TAG(eth_l2_src_dst_v1, tag, smac_15_0, spec, smac_15_0);

	if (spec->ip_version) {
		if (spec->ip_version == IP_VERSION_IPV4) {
			DR_STE_SET(eth_l2_src_dst_v1, tag, l3_type, STE_IPV4);
			spec->ip_version = 0;
		} else if (spec->ip_version == IP_VERSION_IPV6) {
			DR_STE_SET(eth_l2_src_dst_v1, tag, l3_type, STE_IPV6);
			spec->ip_version = 0;
		} else {
			errno = EINVAL;
			return errno;
		}
	}

	DR_STE_SET_TAG(eth_l2_src_dst_v1, tag, first_vlan_id, spec, first_vid);
	DR_STE_SET_TAG(eth_l2_src_dst_v1, tag, first_cfi, spec, first_cfi);
	DR_STE_SET_TAG(eth_l2_src_dst_v1, tag, first_priority, spec, first_prio);

	if (spec->cvlan_tag) {
		DR_STE_SET(eth_l2_src_dst_v1, tag, first_vlan_qualifier, DR_STE_CVLAN);
		spec->cvlan_tag = 0;
	} else if (spec->svlan_tag) {
		DR_STE_SET(eth_l2_src_dst_v1, tag, first_vlan_qualifier, DR_STE_SVLAN);
		spec->svlan_tag = 0;
	}
	return 0;
}

static void dr_ste_build_eth_l3_ipv4_5_tuple_bit_mask(struct dr_match_param *value,
						      bool inner,
						      uint8_t *bit_mask)
{
	struct dr_match_spec *mask = inner ? &value->inner : &value->outer;

	DR_STE_SET_MASK_V(eth_l3_ipv4_5_tuple_v1, bit_mask, destination_address, mask, dst_ip_31_0);
	DR_STE_SET_MASK_V(eth_l3_ipv4_5_tuple_v1, bit_mask, source_address, mask, src_ip_31_0);
	DR_STE_SET_MASK_V(eth_l3_ipv4_5_tuple_v1, bit_mask, destination_port, mask, tcp_dport);
	DR_STE_SET_MASK_V(eth_l3_ipv4_5_tuple_v1, bit_mask, destination_port, mask, udp_dport);
	DR_STE_SET_MASK_V(eth_l3_ipv4_5_tuple_v1, bit_mask, source_port, mask, tcp_sport);
	DR_STE_SET_MASK_V(eth_l3_ipv4_5_tuple_v1, bit_mask, source_port, mask, udp_sport);
	DR_STE_SET_MASK_V(eth_l3_ipv4_5_tuple_v1, bit_mask, protocol, mask, ip_protocol);
	DR_STE_SET_MASK_V(eth_l3_ipv4_5_tuple_v1, bit_mask, fragmented, mask, frag);
	DR_STE_SET_MASK_V(eth_l3_ipv4_5_tuple_v1, bit_mask, dscp, mask, ip_dscp);
	DR_STE_SET_MASK_V(eth_l3_ipv4_5_tuple_v1, bit_mask, ecn, mask, ip_ecn);

	if (mask->tcp_flags) {
		DR_STE_SET_TCP_FLAGS(eth_l3_ipv4_5_tuple_v1, bit_mask, mask);
		mask->tcp_flags = 0;
	}
}

static int dr_ste_build_eth_l3_ipv4_5_tuple_tag(struct dr_match_param *value,
						struct dr_ste_build *sb,
						uint8_t *tag)
{
	struct dr_match_spec *spec = sb->inner ? &value->inner : &value->outer;

	DR_STE_SET_TAG(eth_l3_ipv4_5_tuple_v1, tag, destination_address, spec, dst_ip_31_0);
	DR_STE_SET_TAG(eth_l3_ipv4_5_tuple_v1, tag, source_address, spec, src_ip_31_0);
	DR_STE_SET_TAG(eth_l3_ipv4_5_tuple_v1, tag, destination_port, spec, tcp_dport);
	DR_STE_SET_TAG(eth_l3_ipv4_5_tuple_v1, tag, destination_port, spec, udp_dport);
	DR_STE_SET_TAG(eth_l3_ipv4_5_tuple_v1, tag, source_port, spec, tcp_sport);
	DR_STE_SET_TAG(eth_l3_ipv4_5_tuple_v1, tag, source_port, spec, udp_sport);
	DR_STE_SET_TAG(eth_l3_ipv4_5_tuple_v1, tag, protocol, spec, ip_protocol);
	DR_STE_SET_TAG(eth_l3_ipv4_5_tuple_v1, tag, fragmented, spec, frag);
	DR_STE_SET_TAG(eth_l3_ipv4_5_tuple_v1, tag, dscp, spec, ip_dscp);
	DR_STE_SET_TAG(eth_l3_ipv4_5_tuple_v1, tag, ecn, spec, ip_ecn);

	if (spec->tcp_flags) {
		DR_STE_SET_TCP_FLAGS(eth_l3_ipv4_5_tuple_v1, tag, spec);
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

	DR_STE_SET_MASK_V(eth_l2_src_v1, bit_mask, first_vlan_id, mask, first_vid);
	DR_STE_SET_MASK_V(eth_l2_src_v1, bit_mask, first_cfi, mask, first_cfi);
	DR_STE_SET_MASK_V(eth_l2_src_v1, bit_mask, first_priority, mask, first_prio);
	DR_STE_SET_MASK_V(eth_l2_src_v1, bit_mask, ip_fragmented, mask, frag); // ?
	DR_STE_SET_MASK_V(eth_l2_src_v1, bit_mask, l3_ethertype, mask, ethertype); // ?
	DR_STE_SET_MASK(eth_l2_src_v1, bit_mask, l3_type, mask, ip_version);

	if (mask->svlan_tag || mask->cvlan_tag) {
		DR_STE_SET(eth_l2_src_v1, bit_mask, first_vlan_qualifier, -1);
		mask->cvlan_tag = 0;
		mask->svlan_tag = 0;
	}

	if (inner) {
		if (misc_mask->inner_second_cvlan_tag ||
		    misc_mask->inner_second_svlan_tag) {
			DR_STE_SET(eth_l2_src_v1, bit_mask, second_vlan_qualifier, -1);
			misc_mask->inner_second_cvlan_tag = 0;
			misc_mask->inner_second_svlan_tag = 0;
		}

		DR_STE_SET_MASK_V(eth_l2_src_v1, bit_mask, second_vlan_id, misc_mask, inner_second_vid);
		DR_STE_SET_MASK_V(eth_l2_src_v1, bit_mask, second_cfi, misc_mask, inner_second_cfi);
		DR_STE_SET_MASK_V(eth_l2_src_v1, bit_mask, second_priority, misc_mask, inner_second_prio);
	} else {
		if (misc_mask->outer_second_cvlan_tag ||
		    misc_mask->outer_second_svlan_tag) {
			DR_STE_SET(eth_l2_src_v1, bit_mask, second_vlan_qualifier, -1);
			misc_mask->outer_second_cvlan_tag = 0;
			misc_mask->outer_second_svlan_tag = 0;
		}

		DR_STE_SET_MASK_V(eth_l2_src_v1, bit_mask, second_vlan_id, misc_mask, outer_second_vid);
		DR_STE_SET_MASK_V(eth_l2_src_v1, bit_mask, second_cfi, misc_mask, outer_second_cfi);
		DR_STE_SET_MASK_V(eth_l2_src_v1, bit_mask, second_priority, misc_mask, outer_second_prio);
	}
}

static int dr_ste_build_eth_l2_src_or_dst_tag(struct dr_match_param *value,
					      bool inner, uint8_t *tag)
{
	struct dr_match_spec *spec = inner ? &value->inner : &value->outer;
	struct dr_match_misc *misc_spec = &value->misc;

	DR_STE_SET_TAG(eth_l2_src_v1, tag, first_vlan_id, spec, first_vid);
	DR_STE_SET_TAG(eth_l2_src_v1, tag, first_cfi, spec, first_cfi);
	DR_STE_SET_TAG(eth_l2_src_v1, tag, first_priority, spec, first_prio);
	DR_STE_SET_TAG(eth_l2_src_v1, tag, ip_fragmented, spec, frag);
	DR_STE_SET_TAG(eth_l2_src_v1, tag, l3_ethertype, spec, ethertype);

	if (spec->ip_version) {
		if (spec->ip_version == IP_VERSION_IPV4) {
			DR_STE_SET(eth_l2_src_v1, tag, l3_type, STE_IPV4);
			spec->ip_version = 0;
		} else if (spec->ip_version == IP_VERSION_IPV6) {
			DR_STE_SET(eth_l2_src_v1, tag, l3_type, STE_IPV6);
			spec->ip_version = 0;
		} else {
			errno = EINVAL;
			return errno;
		}
	}

	if (spec->cvlan_tag) {
		DR_STE_SET(eth_l2_src_v1, tag, first_vlan_qualifier, DR_STE_CVLAN);
		spec->cvlan_tag = 0;
	} else if (spec->svlan_tag) {
		DR_STE_SET(eth_l2_src_v1, tag, first_vlan_qualifier, DR_STE_SVLAN);
		spec->svlan_tag = 0;
	}

	if (inner) {
		if (misc_spec->inner_second_cvlan_tag) {
			DR_STE_SET(eth_l2_src_v1, tag, second_vlan_qualifier, DR_STE_CVLAN);
			misc_spec->inner_second_cvlan_tag = 0;
		} else if (misc_spec->inner_second_svlan_tag) {
			DR_STE_SET(eth_l2_src_v1, tag, second_vlan_qualifier, DR_STE_SVLAN);
			misc_spec->inner_second_svlan_tag = 0;
		}

		DR_STE_SET_TAG(eth_l2_src_v1, tag, second_vlan_id, misc_spec, inner_second_vid);
		DR_STE_SET_TAG(eth_l2_src_v1, tag, second_cfi, misc_spec, inner_second_cfi);
		DR_STE_SET_TAG(eth_l2_src_v1, tag, second_priority, misc_spec, inner_second_prio);
	} else {
		if (misc_spec->outer_second_cvlan_tag) {
			DR_STE_SET(eth_l2_src_v1, tag, second_vlan_qualifier, DR_STE_CVLAN);
			misc_spec->outer_second_cvlan_tag = 0;
		} else if (misc_spec->outer_second_svlan_tag) {
			DR_STE_SET(eth_l2_src_v1, tag, second_vlan_qualifier, DR_STE_SVLAN);
			misc_spec->outer_second_svlan_tag = 0;
		}
		DR_STE_SET_TAG(eth_l2_src_v1, tag, second_vlan_id, misc_spec, outer_second_vid);
		DR_STE_SET_TAG(eth_l2_src_v1, tag, second_cfi, misc_spec, outer_second_cfi);
		DR_STE_SET_TAG(eth_l2_src_v1, tag, second_priority, misc_spec, outer_second_prio);
	}

	return 0;
}

static void dr_ste_build_eth_l2_src_bit_mask(struct dr_match_param *value,
					     bool inner, uint8_t *bit_mask)
{
	struct dr_match_spec *mask = inner ? &value->inner : &value->outer;

	DR_STE_SET_MASK_V(eth_l2_src_v1, bit_mask, smac_47_16, mask, smac_47_16);
	DR_STE_SET_MASK_V(eth_l2_src_v1, bit_mask, smac_15_0, mask, smac_15_0);

	dr_ste_build_eth_l2_src_or_dst_bit_mask(value, inner, bit_mask);
}

static int dr_ste_build_eth_l2_src_tag(struct dr_match_param *value,
				       struct dr_ste_build *sb,
				       uint8_t *tag)
{
	struct dr_match_spec *spec = sb->inner ? &value->inner : &value->outer;

	DR_STE_SET_TAG(eth_l2_src_v1, tag, smac_47_16, spec, smac_47_16);
	DR_STE_SET_TAG(eth_l2_src_v1, tag, smac_15_0, spec, smac_15_0);

	return dr_ste_build_eth_l2_src_or_dst_tag(value, sb->inner, tag);
}

static void dr_ste_build_eth_l2_dst_bit_mask(struct dr_match_param *value,
					     bool inner, uint8_t *bit_mask)
{
	struct dr_match_spec *mask = inner ? &value->inner : &value->outer;

	DR_STE_SET_MASK_V(eth_l2_dst_v1, bit_mask, dmac_47_16, mask, dmac_47_16);
	DR_STE_SET_MASK_V(eth_l2_dst_v1, bit_mask, dmac_15_0, mask, dmac_15_0);

	dr_ste_build_eth_l2_src_or_dst_bit_mask(value, inner, bit_mask);
}

static int dr_ste_build_eth_l2_dst_tag(struct dr_match_param *value,
				       struct dr_ste_build *sb,
				       uint8_t *tag)
{
	struct dr_match_spec *spec = sb->inner ? &value->inner : &value->outer;

	DR_STE_SET_TAG(eth_l2_dst_v1, tag, dmac_47_16, spec, dmac_47_16);
	DR_STE_SET_TAG(eth_l2_dst_v1, tag, dmac_15_0, spec, dmac_15_0);

	return dr_ste_build_eth_l2_src_or_dst_tag(value, sb->inner, tag);
}

static void dr_ste_build_eth_l2_tnl_bit_mask(struct dr_match_param *value,
					     bool inner, uint8_t *bit_mask)
{
	struct dr_match_spec *mask = inner ? &value->inner : &value->outer;
	struct dr_match_misc *misc = &value->misc;

	DR_STE_SET_MASK_V(eth_l2_tnl_v1, bit_mask, dmac_47_16, mask, dmac_47_16);
	DR_STE_SET_MASK_V(eth_l2_tnl_v1, bit_mask, dmac_15_0, mask, dmac_15_0);
	DR_STE_SET_MASK_V(eth_l2_tnl_v1, bit_mask, first_vlan_id, mask, first_vid);
	DR_STE_SET_MASK_V(eth_l2_tnl_v1, bit_mask, first_cfi, mask, first_cfi);
	DR_STE_SET_MASK_V(eth_l2_tnl_v1, bit_mask, first_priority, mask, first_prio);
	DR_STE_SET_MASK_V(eth_l2_tnl_v1, bit_mask, ip_fragmented, mask, frag);
	DR_STE_SET_MASK_V(eth_l2_tnl_v1, bit_mask, l3_ethertype, mask, ethertype);
	DR_STE_SET_MASK(eth_l2_tnl_v1, bit_mask, l3_type, mask, ip_version);

	if (misc->vxlan_vni) {
		DR_STE_SET(eth_l2_tnl_v1, bit_mask, l2_tunneling_network_id, (misc->vxlan_vni << 8));
		misc->vxlan_vni = 0;
	}

	if (mask->svlan_tag || mask->cvlan_tag) {
		DR_STE_SET(eth_l2_tnl_v1, bit_mask, first_vlan_qualifier, -1);
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

	DR_STE_SET_TAG(eth_l2_tnl_v1, tag, dmac_47_16, spec, dmac_47_16);
	DR_STE_SET_TAG(eth_l2_tnl_v1, tag, dmac_15_0, spec, dmac_15_0);
	DR_STE_SET_TAG(eth_l2_tnl_v1, tag, first_vlan_id, spec, first_vid);
	DR_STE_SET_TAG(eth_l2_tnl_v1, tag, first_cfi, spec, first_cfi);
	DR_STE_SET_TAG(eth_l2_tnl_v1, tag, ip_fragmented, spec, frag);
	DR_STE_SET_TAG(eth_l2_tnl_v1, tag, first_priority, spec, first_prio);
	DR_STE_SET_TAG(eth_l2_tnl_v1, tag, l3_ethertype, spec, ethertype);

	if (misc->vxlan_vni) {
		DR_STE_SET(eth_l2_tnl_v1, tag, l2_tunneling_network_id,
			   (misc->vxlan_vni << 8));
		misc->vxlan_vni = 0;
	}

	if (spec->cvlan_tag) {
		DR_STE_SET(eth_l2_tnl_v1, tag, first_vlan_qualifier, DR_STE_CVLAN);
		spec->cvlan_tag = 0;
	} else if (spec->svlan_tag) {
		DR_STE_SET(eth_l2_tnl_v1, tag, first_vlan_qualifier, DR_STE_SVLAN);
		spec->svlan_tag = 0;
	}

	if (spec->ip_version) {
		if (spec->ip_version == IP_VERSION_IPV4) {
			DR_STE_SET(eth_l2_tnl_v1, tag, l3_type, STE_IPV4);
			spec->ip_version = 0;
		} else if (spec->ip_version == IP_VERSION_IPV6) {
			DR_STE_SET(eth_l2_tnl_v1, tag, l3_type, STE_IPV6);
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

	DR_STE_SET_MASK_V(eth_l3_ipv4_misc_v1, bit_mask, time_to_live, mask, ip_ttl_hoplimit);
}

static int dr_ste_build_eth_l3_ipv4_misc_tag(struct dr_match_param *value,
					     struct dr_ste_build *sb,
					     uint8_t *tag)
{
	struct dr_match_spec *spec = sb->inner ? &value->inner : &value->outer;

	DR_STE_SET_TAG(eth_l3_ipv4_misc_v1, tag, time_to_live, spec, ip_ttl_hoplimit);

	return 0;
}

static void dr_ste_build_ipv6_l3_l4_bit_mask(struct dr_match_param *value,
					     bool inner, uint8_t *bit_mask)
{
	struct dr_match_spec *mask = inner ? &value->inner : &value->outer;

	DR_STE_SET_MASK_V(eth_l4_v1, bit_mask, dst_port, mask, tcp_dport);
	DR_STE_SET_MASK_V(eth_l4_v1, bit_mask, src_port, mask, tcp_sport);
	DR_STE_SET_MASK_V(eth_l4_v1, bit_mask, dst_port, mask, udp_dport);
	DR_STE_SET_MASK_V(eth_l4_v1, bit_mask, src_port, mask, udp_sport);
	DR_STE_SET_MASK_V(eth_l4_v1, bit_mask, protocol, mask, ip_protocol);
	DR_STE_SET_MASK_V(eth_l4_v1, bit_mask, fragmented, mask, frag);
	DR_STE_SET_MASK_V(eth_l4_v1, bit_mask, dscp, mask, ip_dscp);
	DR_STE_SET_MASK_V(eth_l4_v1, bit_mask, ecn, mask, ip_ecn);
	DR_STE_SET_MASK_V(eth_l4_v1, bit_mask, ipv6_hop_limit, mask, ip_ttl_hoplimit);

	if (mask->tcp_flags) {
		DR_STE_SET_TCP_FLAGS(eth_l4_v1, bit_mask, mask);
		mask->tcp_flags = 0;
	}
}

static int dr_ste_build_ipv6_l3_l4_tag(struct dr_match_param *value,
				       struct dr_ste_build *sb,
				       uint8_t *tag)
{
	struct dr_match_spec *spec = sb->inner ? &value->inner : &value->outer;

	DR_STE_SET_TAG(eth_l4_v1, tag, dst_port, spec, tcp_dport);
	DR_STE_SET_TAG(eth_l4_v1, tag, src_port, spec, tcp_sport);
	DR_STE_SET_TAG(eth_l4_v1, tag, dst_port, spec, udp_dport);
	DR_STE_SET_TAG(eth_l4_v1, tag, src_port, spec, udp_sport);
	DR_STE_SET_TAG(eth_l4_v1, tag, protocol, spec, ip_protocol);
	DR_STE_SET_TAG(eth_l4_v1, tag, fragmented, spec, frag);
	DR_STE_SET_TAG(eth_l4_v1, tag, dscp, spec, ip_dscp);
	DR_STE_SET_TAG(eth_l4_v1, tag, ecn, spec, ip_ecn);
	DR_STE_SET_TAG(eth_l4_v1, tag, ipv6_hop_limit, spec, ip_ttl_hoplimit);

	if (spec->tcp_flags) {
		DR_STE_SET_TCP_FLAGS(eth_l4_v1, tag, spec);
		spec->tcp_flags = 0;
	}

	return 0;
}

static void dr_ste_build_eth_l4_misc_bit_mask(struct dr_match_param *value,
					      bool inner, uint8_t *bit_mask)
{
	struct dr_match_misc3 *misc_3_mask = &value->misc3;

	if (inner) {
		DR_STE_SET_MASK_V(eth_l4_misc_v1, bit_mask, seq_num, misc_3_mask,
				  inner_tcp_seq_num);
		DR_STE_SET_MASK_V(eth_l4_misc_v1, bit_mask, ack_num, misc_3_mask,
				  inner_tcp_ack_num);
	} else {
		DR_STE_SET_MASK_V(eth_l4_misc_v1, bit_mask, seq_num, misc_3_mask,
				  outer_tcp_seq_num);
		DR_STE_SET_MASK_V(eth_l4_misc_v1, bit_mask, ack_num, misc_3_mask,
				  outer_tcp_ack_num);
	}
}

static int dr_ste_build_eth_l4_misc_tag(struct dr_match_param *value,
					struct dr_ste_build *sb,
					uint8_t *tag)
{
	struct dr_match_misc3 *misc3 = &value->misc3;

	if (sb->inner) {
		DR_STE_SET_TAG(eth_l4_misc_v1, tag, seq_num, misc3, inner_tcp_seq_num);
		DR_STE_SET_TAG(eth_l4_misc_v1, tag, ack_num, misc3, inner_tcp_ack_num);
	} else {
		DR_STE_SET_TAG(eth_l4_misc_v1, tag, seq_num, misc3, outer_tcp_seq_num);
		DR_STE_SET_TAG(eth_l4_misc_v1, tag, ack_num, misc3, outer_tcp_ack_num);
	}

	return 0;
}

static void dr_ste_build_mpls_bit_mask(struct dr_match_param *value,
				       bool inner, uint8_t *bit_mask)
{
	struct dr_match_misc2 *misc2_mask = &value->misc2;

	if (inner)
		DR_STE_SET_MPLS_MASK(mpls_v1, misc2_mask, inner, bit_mask);
	else
		DR_STE_SET_MPLS_MASK(mpls_v1, misc2_mask, outer, bit_mask);
}

static int dr_ste_build_mpls_tag(struct dr_match_param *value,
				 struct dr_ste_build *sb,
				 uint8_t *tag)
{
	struct dr_match_misc2 *misc2_mask = &value->misc2;

	if (sb->inner)
		DR_STE_SET_MPLS_TAG(mpls_v1, misc2_mask, inner, tag);
	else
		DR_STE_SET_MPLS_TAG(mpls_v1, misc2_mask, outer, tag);

	return 0;
}

static void dr_ste_build_gre_bit_mask(struct dr_match_param *value,
				      bool inner, uint8_t *bit_mask)
{
	struct dr_match_misc *misc_mask = &value->misc;

	DR_STE_SET_MASK_V(gre_v1, bit_mask, gre_protocol, misc_mask, gre_protocol);
	DR_STE_SET_MASK_V(gre_v1, bit_mask, gre_k_present, misc_mask, gre_k_present);
	DR_STE_SET_MASK_V(gre_v1, bit_mask, gre_key_h, misc_mask, gre_key_h);
	DR_STE_SET_MASK_V(gre_v1, bit_mask, gre_key_l, misc_mask, gre_key_l);

	DR_STE_SET_MASK_V(gre_v1, bit_mask, gre_c_present, misc_mask, gre_c_present);
	DR_STE_SET_MASK_V(gre_v1, bit_mask, gre_s_present, misc_mask, gre_s_present);
}

static int dr_ste_build_gre_tag(struct dr_match_param *value,
				struct dr_ste_build *sb,
				uint8_t *tag)
{
	struct  dr_match_misc *misc = &value->misc;

	DR_STE_SET_TAG(gre_v1, tag, gre_protocol, misc, gre_protocol);

	DR_STE_SET_TAG(gre_v1, tag, gre_k_present, misc, gre_k_present);
	DR_STE_SET_TAG(gre_v1, tag, gre_key_h, misc, gre_key_h);
	DR_STE_SET_TAG(gre_v1, tag, gre_key_l, misc, gre_key_l);

	DR_STE_SET_TAG(gre_v1, tag, gre_c_present, misc, gre_c_present);

	DR_STE_SET_TAG(gre_v1, tag, gre_s_present, misc, gre_s_present);

	return 0;
}

static void
dr_ste_build_flex_parser_tnl_gtpu_bit_mask(struct dr_match_param *value,
					   uint8_t *bit_mask)
{
	// TODO
}

static int
dr_ste_build_flex_parser_tnl_gtpu_tag(struct dr_match_param *value,
				      struct dr_ste_build *sb,
				      uint8_t *tag)
{
	// TODO NOT SUPPORTED YET
	return EINVAL;
}

static void dr_ste_build_flex_parser_0_bit_mask(struct dr_match_param *value,
						bool inner, uint8_t *bit_mask)
{
	// TODO NOT SUPPORTED
}

static int dr_ste_build_flex_parser_0_tag(struct dr_match_param *value,
					  struct dr_ste_build *sb,
					  uint8_t *tag)
{
	// flex_parser_0 - > tunnel
	// TODO NOT SUPPORTED MPLS OVER UDP
	// TODO NOT SUPPORTED MPLS OVER GRE

	return EINVAL;
}

#define ICMP_TYPE_OFFSET_FIRST_DW		24
#define ICMP_CODE_OFFSET_FIRST_DW		16
#define ICMP_HEADER_DATA_OFFSET_SECOND_DW	0

static int dr_ste_build_flex_parser_1_bit_mask(struct dr_match_param *mask,
					       struct dr_devx_caps *caps,
					       uint8_t *bit_mask)
{
	// TODO NOT SUPPORTED ICMP -used with tcp

	return EINVAL;
}

static int dr_ste_build_flex_parser_1_tag(struct dr_match_param *value,
					  struct dr_ste_build *sb,
					  uint8_t *tag)
{
	// TODO NOT SUPPORTED ICMP -used with tcp

	return EINVAL;
}

static int dr_ste_build_src_gvmi_qpn_bit_mask(struct dr_match_param *value,
					      uint8_t *bit_mask)
{
	struct dr_match_misc *misc_mask = &value->misc;

	if (misc_mask->source_port && misc_mask->source_port != 0xffff) {
		errno = EINVAL;
		return errno;
	}
	DR_STE_SET_MASK(src_gvmi_qp_v1, bit_mask, source_gvmi, misc_mask, source_port);
	DR_STE_SET_MASK(src_gvmi_qp_v1, bit_mask, source_qp, misc_mask, source_sqn);

	return 0;
}

static int dr_ste_build_src_gvmi_qpn_tag(struct dr_match_param *value,
					 struct dr_ste_build *sb,
					 uint8_t *tag)
{
	struct dr_match_misc *misc = &value->misc;
	struct dr_devx_vport_cap *vport_cap;

	DR_STE_SET_TAG(src_gvmi_qp_v1, tag, source_qp, misc, source_sqn);

	vport_cap = dr_get_vport_cap(sb->caps, misc->source_port);
	if (!vport_cap)
		return errno;

	if (vport_cap->vport_gvmi)
		DR_STE_SET(src_gvmi_qp_v1, tag, source_gvmi, vport_cap->vport_gvmi);

	misc->source_port = 0;

	return 0;
}

static struct dr_ste_ctx ste_ctx_v1 = {
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

	.prepare_ste_info = dr_ste_hw_prepare_ste,
};

struct dr_ste_ctx *dr_ste_init_ctx_v1(void)
{
	return &ste_ctx_v1;
}
