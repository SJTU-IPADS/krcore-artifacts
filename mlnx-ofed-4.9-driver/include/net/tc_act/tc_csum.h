#ifndef _COMPAT_NET_TC_ACT_TC_CSUM_H
#define _COMPAT_NET_TC_ACT_TC_CSUM_H 1

#include "../../../compat/config.h"

#ifdef HAVE_TCA_CSUM_UPDATE_FLAG_IPV4HDR
#include <linux/tc_act/tc_csum.h>
#include_next <net/tc_act/tc_csum.h>

#ifndef HAVE_IS_TCF_CSUM
#define HAVE_IS_TCF_CSUM

#ifdef HAVE_TCF_COMMON
#define to_csum(a) ((struct tcf_csum *) a->priv)
#else
#define to_csum(a) ((struct tcf_csum *)a)
#endif

static inline bool is_tcf_csum(const struct tc_action *a)
{
#ifdef CONFIG_NET_CLS_ACT
	if (a->ops && a->ops->type == TCA_ACT_CSUM) {
		return true;
	}
#endif
	return false;
}

static inline u32 tcf_csum_update_flags(const struct tc_action *a)
{
	return to_csum(a)->update_flags;
}
#endif /* HAVE_IS_TCF_CSUM */

#endif /* HAVE_TCA_CSUM_UPDATE_FLAG_IPV4HDR */

#endif /* _COMPAT_NET_TC_ACT_TC_CSUM_H */
