#ifndef _COMPAT_LINUX_INET_H
#define _COMPAT_LINUX_INET_H

#include "../../compat/config.h"
#include <linux/version.h>

#include_next <linux/inet.h>

#if (defined(RHEL_MAJOR) && RHEL_MAJOR -0 == 7 && RHEL_MINOR -0 >= 2) || \
	(LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
#ifndef HAVE_INET_PTON_WITH_SCOPE

#include <net/net_namespace.h>
#include <linux/socket.h>
#include <net/ipv6.h>


static int inet4_pton(const char *src, u16 port_num,
		struct sockaddr_storage *addr)
{
	struct sockaddr_in *addr4 = (struct sockaddr_in *)addr;
	int srclen = strlen(src);

	if (srclen > INET_ADDRSTRLEN)
		return -EINVAL;

	if (in4_pton(src, srclen, (u8 *)&addr4->sin_addr.s_addr,
		     '\n', NULL) == 0)
		return -EINVAL;

	addr4->sin_family = AF_INET;
	addr4->sin_port = htons(port_num);

	return 0;
}

static int inet6_pton(struct net *net, const char *src, u16 port_num,
		struct sockaddr_storage *addr)
{
	struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)addr;
	const char *scope_delim;
	int srclen = strlen(src);

	if (srclen > INET6_ADDRSTRLEN)
		return -EINVAL;

	if (in6_pton(src, srclen, (u8 *)&addr6->sin6_addr.s6_addr,
		     '%', &scope_delim) == 0)
		return -EINVAL;

	if (ipv6_addr_type(&addr6->sin6_addr) & IPV6_ADDR_LINKLOCAL &&
	    src + srclen != scope_delim && *scope_delim == '%') {
		struct net_device *dev;
		char scope_id[16];
		size_t scope_len = min_t(size_t, sizeof(scope_id) - 1,
					 src + srclen - scope_delim - 1);

		memcpy(scope_id, scope_delim + 1, scope_len);
		scope_id[scope_len] = '\0';

		dev = dev_get_by_name(net, scope_id);
		if (dev) {
			addr6->sin6_scope_id = dev->ifindex;
			dev_put(dev);
		} else if (kstrtouint(scope_id, 0, &addr6->sin6_scope_id)) {
			return -EINVAL;
		}
	}

	addr6->sin6_family = AF_INET6;
	addr6->sin6_port = htons(port_num);

	return 0;
}

/**
 * inet_pton_with_scope - convert an IPv4/IPv6 and port to socket address
 * @net: net namespace (used for scope handling)
 * @af: address family, AF_INET, AF_INET6 or AF_UNSPEC for either
 * @src: the start of the address string
 * @port: the start of the port string (or NULL for none)
 * @addr: output socket address
 *
 * Return zero on success, return errno when any error occurs.
 */
static inline int inet_pton_with_scope(struct net *net, __kernel_sa_family_t af,
		const char *src, const char *port, struct sockaddr_storage *addr)
{
	u16 port_num;
	int ret = -EINVAL;

	if (port) {
		if (kstrtou16(port, 0, &port_num))
			return -EINVAL;
	} else {
		port_num = 0;
	}

	switch (af) {
	case AF_INET:
		ret = inet4_pton(src, port_num, addr);
		break;
	case AF_INET6:
		ret = inet6_pton(net, src, port_num, addr);
		break;
	case AF_UNSPEC:
		ret = inet4_pton(src, port_num, addr);
		if (ret)
			ret = inet6_pton(net, src, port_num, addr);
		break;
	default:
		pr_err("unexpected address family %d\n", af);
	};

	return ret;
}

#endif /* inet_pton_with_scope */

#endif

#endif /* _COMPAT_LINUX_INET_H */
