#ifndef LINUX_3_13_COMPAT_H
#define LINUX_3_13_COMPAT_H

#include <linux/version.h>
#include <linux/completion.h>
#include <linux/list.h>

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,13,0))

#ifndef CONFIG_COMPAT_IS_REINIT_COMPLETION
#define CONFIG_COMPAT_IS_REINIT_COMPLETION

static inline void reinit_completion(struct completion *x)
{
	x->done = 0;
}

#endif

#define pcie_get_mps LINUX_BACKPORT(pcie_get_mps)
int pcie_get_mps(struct pci_dev *dev);

#define pcie_set_mps LINUX_BACKPORT(pcie_set_mps)
int pcie_set_mps(struct pci_dev *dev, int mps);

#define list_last_entry(ptr, type, member) \
	list_entry((ptr)->prev, type, member)

#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(3,13,0)) */

#endif /* LINUX_3_13_COMPAT_H */
