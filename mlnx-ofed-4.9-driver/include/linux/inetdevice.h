#ifndef _COMPAT_LINUX_INETDEVICE_H
#define _COMPAT_LINUX_INETDEVICE_H 1

#include "../../compat/config.h"
#include_next <linux/inetdevice.h>

#ifndef HAVE_INET_CONFIRM_ADDR_EXPORTED
static inline __be32 confirm_addr_indev(struct in_device *in_dev, __be32 dst,
                                        __be32 local, int scope)
{
#ifndef HAVE_FOR_IFA
	const struct in_ifaddr *ifa;
#endif
        int same = 0;
        __be32 addr = 0;
#ifndef HAVE_FOR_IFA
	in_dev_for_each_ifa_rcu(ifa, in_dev) {
#else
	for_ifa(in_dev) {
#endif
                if (!addr &&
                    (local == ifa->ifa_local || !local) &&
                    ifa->ifa_scope <= scope) {
                        addr = ifa->ifa_local;
                        if (same)
                                break;
                }
                if (!same) {
                        same = (!local || inet_ifa_match(local, ifa)) &&
                                (!dst || inet_ifa_match(dst, ifa));
                        if (same && addr) {
                                if (local || !dst)
                                        break;
                                /* Is the selected addr into dst subnet? */
                                if (inet_ifa_match(addr, ifa))
                                        break;
                                /* No, then can we use new local src? */
                                if (ifa->ifa_scope <= scope) {
                                        addr = ifa->ifa_local;
                                        break;
                                }
                                /* search for large dst subnet for addr */
                                same = 0;
                        }
                }
        }
#ifdef HAVE_FOR_IFA
	endfor_ifa(in_dev);
#endif

        return same ? addr : 0;
}
#endif
#endif /* __COMPAT_LINUX_INETDEVICE_H */
