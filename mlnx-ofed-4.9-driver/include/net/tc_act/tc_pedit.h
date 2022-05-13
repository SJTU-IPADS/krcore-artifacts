#ifndef _COMPAT_NET_TC_ACT_TC_PEDIT_H
#define _COMPAT_NET_TC_ACT_TC_PEDIT_H 1

#include "../../../compat/config.h"

#ifdef CONFIG_COMPAT_TCF_PEDIT_MOD
#include <net/act_api.h>
#include "uapi/linux/tc_act/tc_pedit.h"

struct tcf_pedit_key_ex {
	enum pedit_header_type htype;
	enum pedit_cmd cmd;
};

struct tcf_pedit {
#ifdef HAVE_TCF_COMMON
	struct tcf_common	common;
#else
	struct tc_action	common;
#endif
	unsigned char		tcfp_nkeys;
	unsigned char		tcfp_flags;
	struct tc_pedit_key	*tcfp_keys;
	struct tcf_pedit_key_ex	*tcfp_keys_ex;
};

#ifdef HAVE_TCF_COMMON
#define to_pedit(a) ((struct tcf_pedit *) a->priv)

#define pc_to_pedit(pc) \
	container_of(pc, struct tcf_pedit, common)
#else
#define to_pedit(a) ((struct tcf_pedit *)a)
#endif

#else /* CONFIG_COMPAT_TCF_PEDIT_MOD */
#ifdef HAVE_TCF_PEDIT_TCFP_KEYS_EX
#include_next <net/tc_act/tc_pedit.h>
#endif
#endif

#ifdef HAVE_TCF_PEDIT_TCFP_KEYS_EX

#ifndef HAVE_TCF_PEDIT_NKEYS
#include <linux/tc_act/tc_pedit.h>

static inline bool is_tcf_pedit(const struct tc_action *a)
{
#ifdef CONFIG_NET_CLS_ACT
	if (a->ops && a->ops->type == TCA_ACT_PEDIT)
		return true;
#endif
	return false;
}

static inline int tcf_pedit_nkeys(const struct tc_action *a)
{
	return to_pedit(a)->tcfp_nkeys;
}

static inline u32 tcf_pedit_htype(const struct tc_action *a, int index)
{
	if (to_pedit(a)->tcfp_keys_ex)
		return to_pedit(a)->tcfp_keys_ex[index].htype;

	return TCA_PEDIT_KEY_EX_HDR_TYPE_NETWORK;
}

static inline u32 tcf_pedit_cmd(const struct tc_action *a, int index)
{
	if (to_pedit(a)->tcfp_keys_ex)
		return to_pedit(a)->tcfp_keys_ex[index].cmd;

	return __PEDIT_CMD_MAX;
}

static inline u32 tcf_pedit_mask(const struct tc_action *a, int index)
{
	return to_pedit(a)->tcfp_keys[index].mask;
}

static inline u32 tcf_pedit_val(const struct tc_action *a, int index)
{
	return to_pedit(a)->tcfp_keys[index].val;
}

static inline u32 tcf_pedit_offset(const struct tc_action *a, int index)
{
	return to_pedit(a)->tcfp_keys[index].off;
}

#endif /* HAVE_TCF_PEDIT_NKEYS */

#endif /* HAVE_TCF_PEDIT_TCFP_KEYS_EX */

#endif	/* _COMPAT_NET_TC_ACT_TC_PEDIT_H */
