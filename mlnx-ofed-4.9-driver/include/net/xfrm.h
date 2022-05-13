#ifndef _COMPAT_NET_XFRM_H
#define _COMPAT_NET_XFRM_H 1

#include_next <net/xfrm.h>

#ifndef XFRM_ESP_NO_TRAILER
#define XFRM_ESP_NO_TRAILER     64
#endif

#endif	/* _COMPAT_NET_XFRM_H */
