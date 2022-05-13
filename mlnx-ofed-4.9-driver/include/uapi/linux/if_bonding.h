#ifndef LINUX_IF_BONDING_H
#define LINUX_IF_BONDING_H

#include "../../../compat/config.h"

/* just include the header from the correct location */
#ifdef HAVE_UAPI_IF_BONDING_H
#include_next <uapi/linux/if_bonding.h>
#else
#include_next <linux/if_bonding.h>
#endif


#endif /* LINUX_IF_BONDING_H */
