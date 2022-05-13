#ifndef LINUX_3_11_COMPAT_H
#define LINUX_3_11_COMPAT_H

#include <linux/version.h>

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 11, 0))
#include <linux/netdevice.h>

#ifndef AF_IB
#define AF_IB		27      /* Native InfiniBand address    */
#define PF_IB		AF_IB
#endif /* AF_IB */

#if !defined(CONFIG_COMPAT_IFLA_VF_LINK_STATE_MAX)
enum {
	IFLA_VF_LINK_STATE_AUTO,	/* link state of the uplink */
	IFLA_VF_LINK_STATE_ENABLE,	/* link always up */
	IFLA_VF_LINK_STATE_DISABLE,	/* link always down */
	__IFLA_VF_LINK_STATE_MAX,
};
#endif

#ifndef CONFIG_COMPAT_SCATTERLIST_SG_PCOPY_TO_BUFFER

#include <linux/scatterlist.h>

#define sg_copy_from_buffer LINUX_BACKPORT(sg_copy_from_buffer)
size_t sg_copy_from_buffer(struct scatterlist *sgl, unsigned int nents,
			   void *buf, size_t buflen);
#define sg_copy_to_buffer LINUX_BACKPORT(sg_copy_to_buffer)
size_t sg_copy_to_buffer(struct scatterlist *sgl, unsigned int nents,
			 void *buf, size_t buflen);

#define sg_pcopy_from_buffer LINUX_BACKPORT(sg_pcopy_from_buffer)
size_t sg_pcopy_from_buffer(struct scatterlist *sgl, unsigned int nents,
			    void *buf, size_t buflen, off_t skip);
#define sg_pcopy_to_buffer LINUX_BACKPORT(sg_pcopy_to_buffer)
size_t sg_pcopy_to_buffer(struct scatterlist *sgl, unsigned int nents,
			  void *buf, size_t buflen, off_t skip);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0))
#define fixed_size_llseek LINUX_BACKPORT(fixed_size_llseek)
loff_t fixed_size_llseek(struct file *file, loff_t offset, int whence,
			 loff_t size);
#endif

#endif

#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(3, 11, 0)) */
#endif /* LINUX_3_11_COMPAT_H */
