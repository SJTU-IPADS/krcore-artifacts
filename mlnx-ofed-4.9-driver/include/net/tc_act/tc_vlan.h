#ifndef _COMPAT_NET_TC_ACT_TC_VLAN_H
#define _COMPAT_NET_TC_ACT_TC_VLAN_H 1

#include "../../../compat/config.h"

#ifdef HAVE_IS_TCF_VLAN
#ifndef CONFIG_COMPAT_TCF_VLAN_MOD
#include_next <net/tc_act/tc_vlan.h>
#endif

#ifndef to_vlan
#define act_to_vlan(a) ((struct tcf_vlan *) a->priv)
#else
#define act_to_vlan(a) to_vlan(a)
#endif

#ifdef CONFIG_COMPAT_TCF_VLAN_MOD
#include <net/act_api.h>
#include <linux/tc_act/tc_vlan.h>

struct tcf_vlan {
	struct tcf_common common;
	int tcfv_action;
	u16 tcfv_push_vid;
	__be16 tcfv_push_proto;
	u8 tcfv_push_prio;
};
#define pc_to_vlan(pc) \
	container_of(pc, struct tcf_vlan, common)

static inline bool is_tcf_vlan(const struct tc_action *a)
{
#ifdef CONFIG_NET_CLS_ACT
	if (a->ops && a->ops->type == TCA_ACT_VLAN)
		return true;
#endif
	return false;
}

static inline u32 tcf_vlan_action(const struct tc_action *a)
{
	return act_to_vlan(a)->tcfv_action;
}

static inline u16 tcf_vlan_push_vid(const struct tc_action *a)
{
	return act_to_vlan(a)->tcfv_push_vid;
}

static inline __be16 tcf_vlan_push_proto(const struct tc_action *a)
{
	return act_to_vlan(a)->tcfv_push_proto;
}

#endif

#ifndef HAVE_TCF_VLAN_PUSH_PRIO
static inline __be16 tcf_vlan_push_prio(const struct tc_action *a)
{
	return act_to_vlan(a)->tcfv_push_prio;
}
#endif

#endif /* HAVE_IS_TCF_VLAN */
#endif	/* _COMPAT_NET_TC_ACT_TC_VLAN_H */
