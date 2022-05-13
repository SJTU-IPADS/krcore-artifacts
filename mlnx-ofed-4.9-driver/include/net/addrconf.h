#ifndef LINUX_ADDRCONF_H
#define LINUX_ADDRCONF_H

#include "../../compat/config.h"

#include_next <net/addrconf.h>

#ifndef HAVE_ADDRCONF_IFID_EUI48
static inline int addrconf_ifid_eui48(u8 *eui, struct net_device *dev)
{
	if (dev->addr_len != ETH_ALEN)
		return -1;
	memcpy(eui, dev->dev_addr, 3);
	memcpy(eui + 5, dev->dev_addr + 3, 3);

	/*
	 * The zSeries OSA network cards can be shared among various
	 * OS instances, but the OSA cards have only one MAC address.
	 * This leads to duplicate address conflicts in conjunction
	 * with IPv6 if more than one instance uses the same card.
	 *
	 * The driver for these cards can deliver a unique 16-bit
	 * identifier for each instance sharing the same card.  It is
	 * placed instead of 0xFFFE in the interface identifier.  The
	 * "u" bit of the interface identifier is not inverted in this
	 * case.  Hence the resulting interface identifier has local
	 * scope according to RFC2373.
	 */
	if (dev->dev_id) {
		eui[3] = (dev->dev_id >> 8) & 0xFF;
		eui[4] = dev->dev_id & 0xFF;
	} else {
		eui[3] = 0xFF;
		eui[4] = 0xFE;
		eui[0] ^= 2;
	}
	return 0;
}
#endif

#ifndef HAVE_ADDRCONF_ADDR_EUI48
static inline void addrconf_addr_eui48_base(u8 *eui, const char *const addr)
{
	memcpy(eui, addr, 3);
	eui[3] = 0xFF;
	eui[4] = 0xFE;
	memcpy(eui + 5, addr + 3, 3);
}

static inline void addrconf_addr_eui48(u8 *eui, const char *const addr)
{
	addrconf_addr_eui48_base(eui, addr);
	eui[0] ^= 2;
}
#endif

#endif /* LINUX_ADDRCONF_H */
