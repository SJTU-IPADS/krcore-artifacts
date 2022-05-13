#ifndef COMPAT_LINUX_FILTER_H
#define COMPAT_LINUX_FILTER_H

#include "../../compat/config.h"

#include_next <linux/filter.h>

#ifdef HAVE_XDP_BUFF
#ifndef HAVE_XDP_FRAME
struct xdp_frame {
	void *data;
	u16 len;
	u16 headroom;
#ifdef HAVE_XDP_SET_DATA_META_INVALID
	u16 metasize;
#endif
};

/* Convert xdp_buff to xdp_frame */
static inline
struct xdp_frame *convert_to_xdp_frame(struct xdp_buff *xdp)
{
	struct xdp_frame *xdp_frame;
	int metasize;
	int headroom;

	/* Assure headroom is available for storing info */
#ifdef HAVE_XDP_BUFF_DATA_HARD_START
	headroom = xdp->data - xdp->data_hard_start;
#else
	headroom = 0;
#endif
#ifdef HAVE_XDP_SET_DATA_META_INVALID
	metasize = xdp->data - xdp->data_meta;
#else
	metasize = 0;
#endif
	metasize = metasize > 0 ? metasize : 0;
	if (unlikely((headroom - metasize) < sizeof(*xdp_frame)))
		return NULL;

	/* Store info in top of packet */
#ifdef HAVE_XDP_BUFF_DATA_HARD_START
	xdp_frame = xdp->data_hard_start;
#else
	xdp_frame = xdp->data;
#endif

	xdp_frame->data = xdp->data;
	xdp_frame->len  = xdp->data_end - xdp->data;
	xdp_frame->headroom = headroom - sizeof(*xdp_frame);
#ifdef HAVE_XDP_SET_DATA_META_INVALID
	xdp_frame->metasize = metasize;
#endif

	return xdp_frame;
}
#endif
#endif

#endif /* COMPAT_LINUX_FILTER_H */

