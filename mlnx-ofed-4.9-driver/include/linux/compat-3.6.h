#ifndef LINUX_3_6_COMPAT_H
#define LINUX_3_6_COMPAT_H

#include <linux/version.h>

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,6,0))

#include <linux/scatterlist.h>

#define memweight LINUX_BACKPORT(memweight)
extern size_t memweight(const void *ptr, size_t bytes);

/* backports efc42bc9 */
#define sg_alloc_table_from_pages LINUX_BACKPORT(sg_alloc_table_from_pages)
int sg_alloc_table_from_pages(struct sg_table *sgt,
			      struct page **pages, unsigned int n_pages,
			      unsigned long offset, unsigned long size,
			      gfp_t gfp_mask);

/**
 * Backports
 *
 * commit d81a5d1956731c453b85c141458d4ff5d6cc5366
 * Author: Gustavo Padovan <gustavo.padovan@collabora.co.uk>
 * Date:   Tue Jul 10 19:10:06 2012 -0300
 *
 * 	USB: add USB_VENDOR_AND_INTERFACE_INFO() macro
 */
#include <linux/usb.h>
#define USB_VENDOR_AND_INTERFACE_INFO(vend, cl, sc, pr) \
       .match_flags = USB_DEVICE_ID_MATCH_INT_INFO \
               | USB_DEVICE_ID_MATCH_VENDOR, \
       .idVendor = (vend), \
       .bInterfaceClass = (cl), \
       .bInterfaceSubClass = (sc), \
       .bInterfaceProtocol = (pr)

/**
 * Backports
 *
 * commit cdcac9cd7741af2c2b9255cbf060f772596907bb
 * Author: Dave Airlie <airlied@redhat.com>
 * Date:   Wed Jun 27 08:35:52 2012 +0100
 *
 * 	pci_regs: define LNKSTA2 pcie cap + bits.
 *
 * 	We need these for detecting the max link speed for drm drivers.
 *
 * 	Acked-by: Bjorn Helgaas <bhelgass@google.com>
 * 	Signed-off-by: Dave Airlie <airlied@redhat.com>
 */

#ifndef  PCI_EXP_LNKCAP2
#define  PCI_EXP_LNKCAP2 		44	/* Link Capability 2 */
#define  PCI_EXP_LNKCAP2_SLS_2_5GB 	0x01	/* Current Link Speed 2.5GT/s */
#define  PCI_EXP_LNKCAP2_SLS_5_0GB 	0x02	/* Current Link Speed 5.0GT/s */
#define  PCI_EXP_LNKCAP2_SLS_8_0GB 	0x04	/* Current Link Speed 8.0GT/s */
#define  PCI_EXP_LNKCAP2_CROSSLINK 	0x100 /* Crosslink supported */
#endif

#include <linux/etherdevice.h>

/**
 * eth_broadcast_addr - Assign broadcast address
 * @addr: Pointer to a six-byte array containing the Ethernet address
 *
 * Assign the broadcast address to the given address array.
 */
#define eth_broadcast_addr LINUX_BACKPORT(eth_broadcast_addr)
static inline void eth_broadcast_addr(u8 *addr)
{
	memset(addr, 0xff, ETH_ALEN);
}

/*
 * Backports 
 * 
 * commit 959d62fa865d2e616b61a509e1cc5b88741f065e
 * Author: Shuah Khan <shuahkhan@gmail.com>
 * Date:   Thu Jun 14 04:34:30 2012 +0800
 *
 *   leds: Rename led_brightness_set() to led_set_brightness()
 *   
 *   Rename leds external interface led_brightness_set() to led_set_brightness().
 *   This is the second phase of the change to reduce confusion between the
 *   leds internal and external interfaces that set brightness. With this change,
 *   now the external interface is led_set_brightness(). The first phase renamed
 *   the internal interface led_set_brightness() to __led_set_brightness().
 *   There are no changes to the interface implementations.
 *   
 *   Signed-off-by: Shuah Khan <shuahkhan@gmail.com>
 *   Signed-off-by: Bryan Wu <bryan.wu@canonical.com>
 */
#define led_set_brightness(_dev, _switch) led_brightness_set(_dev, _switch)

#define netif_get_num_default_rss_queues LINUX_BACKPORT(netif_get_num_default_rss_queues)
extern int netif_get_num_default_rss_queues(void);

#ifndef DEFAULT_MAX_NUM_RSS_QUEUES
#define DEFAULT_MAX_NUM_RSS_QUEUES      (8)
#endif

#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(3,6,0)) */

#endif /* LINUX_3_6_COMPAT_H */
