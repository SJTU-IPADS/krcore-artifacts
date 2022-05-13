#ifndef LINUX_26_35_COMPAT_H
#define LINUX_26_35_COMPAT_H

#include <linux/version.h>

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,35))
#include <linux/etherdevice.h>
#include <net/sock.h>
#include <linux/types.h>
#include <linux/usb.h>

/* added on linux/kernel.h */
#define USHRT_MAX      ((u16)(~0U))
#define SHRT_MAX       ((s16)(USHRT_MAX>>1))
#define SHRT_MIN       ((s16)(-SHRT_MAX - 1))

#define  SDIO_BUS_ECSI		0x20	/* Enable continuous SPI interrupt */
#define  SDIO_BUS_SCSI		0x40	/* Support continuous SPI interrupt */

#ifndef  PCI_EXP_LNKSTA_CLS_2_5GB
#define  PCI_EXP_LNKSTA_CLS_2_5GB 0x01 /* Current Link Speed 2.5GT/s */
#endif

#ifndef  PCI_EXP_LNKSTA_CLS_5_0GB
#define  PCI_EXP_LNKSTA_CLS_5_0GB 0x02 /* Current Link Speed 5.0GT/s */
#endif

static inline wait_queue_head_t *sk_sleep(struct sock *sk)
{
	return sk->sk_sleep;
}

#define sdio_writeb_readb(func, write_byte, addr, err_ret) sdio_readb(func, addr, err_ret)

#define hex_to_bin LINUX_BACKPORT(hex_to_bin)
int hex_to_bin(char ch);

#define noop_llseek LINUX_BACKPORT(noop_llseek)
extern loff_t noop_llseek(struct file *file, loff_t offset, int origin);

#ifndef __ALIGN_KERNEL
#define __ALIGN_KERNEL(x, a)		__ALIGN_KERNEL_MASK(x, (typeof(x))(a) - 1)
#define __ALIGN_KERNEL_MASK(x, mask)	(((x) + (mask)) & ~(mask))
#endif

#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,35)) */

#endif /* LINUX_26_35_COMPAT_H */
