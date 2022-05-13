#ifndef LINUX_3_14_COMPAT_H
#define LINUX_3_14_COMPAT_H

#include <linux/version.h>
#include <linux/completion.h>

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,14,0))

#ifndef PCI_EXP_LNKSTA_CLS_8_0GB
#define  PCI_EXP_LNKSTA_CLS_8_0GB 0x0003 /* Current Link Speed 8.0GT/s */
#endif

#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(3,14,0)) */

#endif /* LINUX_3_14_COMPAT_H */
