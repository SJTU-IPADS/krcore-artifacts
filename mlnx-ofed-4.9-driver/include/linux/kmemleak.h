#include <linux/version.h>

#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,30))
#include_next <linux/kmemleak.h>
#endif /* (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,25)) */
