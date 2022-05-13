#ifndef __EXTEND_LINUX_INIT_H_TO_3_8__
#define __EXTEND_LINUX_INIT_H_TO_3_8__

#include <linux/version.h>
#include_next <linux/init.h>

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,8,0))

#define __devinit
#define __devinitdata
#define __devinitconst
#define __devexit
#define __devexitdata
#define __devexitconst

#define __devexit_p

#endif /* (LINUX_VERSION_CODE >= KERNEL_VERSION(3,8,0)) */
#endif /* __EXTEND_LINUX_INIT_H_TO_3_8__ */

