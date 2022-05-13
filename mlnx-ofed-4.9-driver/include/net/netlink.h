#ifndef _COMPAT_NET_NETLINK_H
#define _COMPAT_NET_NETLINK_H 1

#include "../../compat/config.h"

#include_next <net/netlink.h>

#ifndef HAVE_NLA_PARSE_6_PARAMS
#define nla_parse(p1, p2, p3, p4, p5, p6) nla_parse(p1, p2, p3, p4, p5)
#define nlmsg_parse(p1, p2, p3, p4, p5, p6) nlmsg_parse(p1, p2, p3, p4, p5)
#define nlmsg_validate(p1, p2, p3, p4, p5) nlmsg_validate(p1, p2, p3, p4)
#endif

#ifndef HAVE_NLA_PUT_U64_64BIT
#define nla_put_u64_64bit(p1, p2, p3, p4) nla_put_u64(p1, p2, p3)
#endif

#ifndef HAVE_NETLINK_EXTACK
#define NETLINK_MAX_COOKIE_LEN	20
struct netlink_ext_ack {
	const char *_msg;
	const struct nlattr *bad_attr;
	u8 cookie[NETLINK_MAX_COOKIE_LEN];
	u8 cookie_len;
};

#define UNUSED(x) (void)(x)
#ifndef NL_SET_ERR_MSG
#define NL_SET_ERR_MSG(extack, msg) { \
		UNUSED(extack); \
		pr_err("%s\n", msg); \
	}
#define NL_SET_ERR_MSG_MOD(extack, msg) NL_SET_ERR_MSG(extack, KBUILD_MODNAME ": " msg)
#endif
#endif/* NL_SET_ERR_MSG */

#endif	/* _COMPAT_NET_NETLINK_H */

