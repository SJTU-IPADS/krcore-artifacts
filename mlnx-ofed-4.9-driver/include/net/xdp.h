#ifndef COMPAT_NET_XDP_H
#define COMPAT_NET_XDP_H

#include "../../compat/config.h"

#ifdef HAVE_XDP_BUFF
#ifdef HAVE_NET_XDP_H
#include_next <net/xdp.h>
#endif
#endif

#endif /* COMPAT_NET_XDP_H */

