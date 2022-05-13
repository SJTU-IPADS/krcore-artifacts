#ifndef LINUX_3_2_COMPAT_H
#define LINUX_3_2_COMPAT_H

#include <linux/version.h>

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,2,0))

#include <linux/skbuff.h>
#include <linux/dma-mapping.h>
#include <linux/ethtool.h>

/* backports b4625dab */
#define  SDIO_CCCR_REV_3_00    3       /* CCCR/FBR Version 3.00 */
#define  SDIO_SDIO_REV_3_00    4       /* SDIO Spec Version 3.00 */

#define PMSG_IS_AUTO(msg)	(((msg).event & PM_EVENT_AUTO) != 0)

#define __ethtool_get_settings LINUX_BACKPORT(__ethtool_get_settings)
extern int __ethtool_get_settings(struct net_device *dev,
				  struct ethtool_cmd *cmd);

/**
 * skb_frag_page - retrieve the page refered to by a paged fragment
 * @frag: the paged fragment
 *
 * Returns the &struct page associated with @frag.
 */
#define skb_frag_page LINUX_BACKPORT(skb_frag_page)
static inline struct page *skb_frag_page(const skb_frag_t *frag)
{
	return frag->page;
}

/**
 * __skb_frag_set_page - sets the page contained in a paged fragment
 * @frag: the paged fragment
 * @page: the page to set
 *
 * Sets the fragment @frag to contain @page.
 */
#define __skb_frag_set_page LINUX_BACKPORT(__skb_frag_set_page)
static inline void __skb_frag_set_page(skb_frag_t *frag, struct page *page)
{
	frag->page = page;
}

/**
 * skb_frag_set_page - sets the page contained in a paged fragment of an skb
 * @skb: the buffer
 * @f: the fragment offset
 * @page: the page to set
 *
 * Sets the @f'th fragment of @skb to contain @page.
 */
#define skb_frag_set_page LINUX_BACKPORT(skb_frag_set_page)
static inline void skb_frag_set_page(struct sk_buff *skb, int f,
				     struct page *page)
{
	__skb_frag_set_page(&skb_shinfo(skb)->frags[f], page);
}

/**
 * skb_frag_address - gets the address of the data contained in a paged fragment
 * @frag: the paged fragment buffer
 *
 * Returns the address of the data within @frag. The page must already
 * be mapped.
 */
#define skb_frag_address LINUX_BACKPORT(skb_frag_address)
static inline void *skb_frag_address(const skb_frag_t *frag)
{
	return page_address(skb_frag_page(frag)) + frag->page_offset;
}

/**
 * skb_frag_address_safe - gets the address of the data contained in a paged fragment
 * @frag: the paged fragment buffer
 *
 * Returns the address of the data within @frag. Checks that the page
 * is mapped and returns %NULL otherwise.
 */
#define skb_frag_address_safe LINUX_BACKPORT(skb_frag_address_safe)
static inline void *skb_frag_address_safe(const skb_frag_t *frag)
{
	void *ptr = page_address(skb_frag_page(frag));
	if (unlikely(!ptr))
		return NULL;

	return ptr + frag->page_offset;
}

/**
 * skb_frag_dma_map - maps a paged fragment via the DMA API
 * @device: the device to map the fragment to
 * @frag: the paged fragment to map
 * @offset: the offset within the fragment (starting at the
 *          fragment's own offset)
 * @size: the number of bytes to map
 * @direction: the direction of the mapping (%PCI_DMA_*)
 *
 * Maps the page associated with @frag to @device.
 */
#define skb_frag_dma_map LINUX_BACKPORT(skb_frag_dma_map)
static inline dma_addr_t skb_frag_dma_map(struct device *dev,
					  const skb_frag_t *frag,
					  size_t offset, size_t size,
					  enum dma_data_direction dir)
{
	return dma_map_page(dev, skb_frag_page(frag),
			    frag->page_offset + offset, size, dir);
}

/**
 * __skb_frag_unref - release a reference on a paged fragment.
 * @frag: the paged fragment
 *
 * Releases a reference on the paged fragment @frag.
 */
#define __skb_frag_unref LINUX_BACKPORT(__skb_frag_unref)
static inline void __skb_frag_unref(skb_frag_t *frag)
{
	put_page(skb_frag_page(frag));
}

#define ETH_P_TDLS	0x890D          /* TDLS */

#define skb_frag_size LINUX_BACKPORT(skb_frag_size)
static inline unsigned int skb_frag_size(const skb_frag_t *frag)
{
	return frag->size;
}

#define skb_frag_size_set LINUX_BACKPORT(skb_frag_size_set)
static inline void skb_frag_size_set(skb_frag_t *frag, unsigned int size)
{
	frag->size = size;
}

#define skb_frag_size_add LINUX_BACKPORT(skb_frag_size_add)
static inline void skb_frag_size_add(skb_frag_t *frag, int delta)
{
	frag->size += delta;
}

#define skb_frag_size_sub LINUX_BACKPORT(skb_frag_size_sub)
static inline void skb_frag_size_sub(skb_frag_t *frag, int delta)
{
	frag->size -= delta;
}

#define hex_byte_pack LINUX_BACKPORT(hex_byte_pack)
static inline char *hex_byte_pack(char *buf, u8 byte)
{
	*buf++ = hex_asc_hi(byte);
	*buf++ = hex_asc_lo(byte);
	return buf;
}

/* module_platform_driver() - Helper macro for drivers that don't do
 * anything special in module init/exit.  This eliminates a lot of
 * boilerplate.  Each module may only use this macro once, and
 * calling it replaces module_init() and module_exit()
 */
#ifndef module_platform_driver
#define module_platform_driver(__platform_driver) \
        module_driver(__platform_driver, platform_driver_register, \
                        platform_driver_unregister)
#endif

#define dma_zalloc_coherent LINUX_BACKPORT(dma_zalloc_coherent)
static inline void *dma_zalloc_coherent(struct device *dev, size_t size,
					dma_addr_t *dma_handle, gfp_t flag)
{
	void *ret = dma_alloc_coherent(dev, size, dma_handle, flag);
	if (ret)
		memset(ret, 0, size);
	return ret;
}

#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(3,2,0)) */

#endif /* LINUX_3_2_COMPAT_H */
