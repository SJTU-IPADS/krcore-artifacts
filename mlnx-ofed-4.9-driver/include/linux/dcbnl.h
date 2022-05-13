#ifndef LINUX_DCBNL_H
#define LINUX_DCBNL_H

#include "../../compat/config.h"

#include_next <linux/dcbnl.h>

#ifndef HAVE_IEEE_GETQCN

#ifndef HAVE_STRUCT_IEEE_QCN
enum dcbnl_cndd_states {
	DCB_CNDD_RESET = 0,
	DCB_CNDD_EDGE,
	DCB_CNDD_INTERIOR,
	DCB_CNDD_INTERIOR_READY,
};

struct ieee_qcn {
	__u8 rpg_enable[IEEE_8021QAZ_MAX_TCS];
	__u32 rppp_max_rps[IEEE_8021QAZ_MAX_TCS];
	__u32 rpg_time_reset[IEEE_8021QAZ_MAX_TCS];
	__u32 rpg_byte_reset[IEEE_8021QAZ_MAX_TCS];
	__u32 rpg_threshold[IEEE_8021QAZ_MAX_TCS];
	__u32 rpg_max_rate[IEEE_8021QAZ_MAX_TCS];
	__u32 rpg_ai_rate[IEEE_8021QAZ_MAX_TCS];
	__u32 rpg_hai_rate[IEEE_8021QAZ_MAX_TCS];
	__u32 rpg_gd[IEEE_8021QAZ_MAX_TCS];
	__u32 rpg_min_dec_fac[IEEE_8021QAZ_MAX_TCS];
	__u32 rpg_min_rate[IEEE_8021QAZ_MAX_TCS];
	__u32 cndd_state_machine[IEEE_8021QAZ_MAX_TCS];
};
#endif /* HAVE_STRUCT_IEEE_QCN */

/* RH7.3 backported this struct, but it does not have
 * all needed feilds as below
 * */
#define ieee_qcn_stats mlnx_ofed_ieee_qcn_stats
struct ieee_qcn_stats {
	__u64 rppp_rp_centiseconds[IEEE_8021QAZ_MAX_TCS];
	__u32 rppp_created_rps[IEEE_8021QAZ_MAX_TCS];
	__u32 ignored_cnm[IEEE_8021QAZ_MAX_TCS];
	__u32 estimated_total_rate[IEEE_8021QAZ_MAX_TCS];
	__u32 cnms_handled_successfully[IEEE_8021QAZ_MAX_TCS];
	__u32 min_total_limiters_rate[IEEE_8021QAZ_MAX_TCS];
	__u32 max_total_limiters_rate[IEEE_8021QAZ_MAX_TCS];
};

#endif

#ifndef IEEE_8021QAZ_APP_SEL_DSCP
#define IEEE_8021QAZ_APP_SEL_DSCP	5
#endif

#ifndef HAVE_STRUCT_IEEE_PFC
struct ieee_pfc {
	__u8	pfc_cap;
	__u8	pfc_en;
	__u8	mbc;
	__u16	delay;
	__u64	requests[IEEE_8021QAZ_MAX_TCS];
	__u64	indications[IEEE_8021QAZ_MAX_TCS];
};
#endif

#endif /* LINUX_DCBNL_H */
