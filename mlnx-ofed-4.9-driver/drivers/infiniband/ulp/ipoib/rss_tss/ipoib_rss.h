/*
 * Copyright (c) 2004, 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Sun Microsystems, Inc. All rights reserved.
 * Copyright (c) 2004 Voltaire, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef _IPOIB_RSS_H
#define _IPOIB_RSS_H

#define IPOIB_FLAGS_TSS		0x20
#define IPOIB_TSS_SUPPORTED(ha)   (ha[0] & (IPOIB_FLAGS_TSS))

enum {
	IPOIB_MAX_RX_QUEUES = 16,
	IPOIB_MAX_TX_QUEUES = 16,
};

/*
 * Per QP stats
 */
struct ipoib_tx_ring_stats {
	unsigned long tx_packets;
	unsigned long tx_bytes;
	unsigned long tx_errors;
	unsigned long tx_dropped;
};

struct ipoib_rx_ring_stats {
	unsigned long rx_packets;
	unsigned long rx_bytes;
	unsigned long rx_errors;
	unsigned long rx_dropped;
	unsigned long multicast;
};

/*
 * Encapsulates the per send QP information
 */
struct ipoib_send_ring {
	struct net_device	*dev;
	struct ib_cq		*send_cq;
	struct ib_qp		*send_qp;
	struct ipoib_tx_buf	*tx_ring;
	unsigned		tx_head;
	unsigned		tx_tail;
	atomic_t                tx_outstanding;
	struct napi_struct	napi;
	struct work_struct	reschedule_napi_work;
	struct ib_sge		tx_sge[MAX_SKB_FRAGS + 1];
	struct ib_ud_wr		tx_wr;
	struct ib_wc		tx_wc[MAX_SEND_CQE];
	struct timer_list	poll_timer;
	struct ipoib_tx_ring_stats stats;
	unsigned		index;
};

void ipoib_napi_schedule_work_tss(struct work_struct *work);

struct ipoib_rx_cm_info {
	struct ib_sge		rx_sge[IPOIB_CM_RX_SG];
	struct ib_recv_wr       rx_wr;
};

/*
 * Encapsulates the per recv QP information
 */
struct ipoib_recv_ring {
	struct net_device	*dev;
	struct ib_qp		*recv_qp;
	struct ib_cq		*recv_cq;
	struct ib_wc		ibwc[IPOIB_NUM_WC];
	struct napi_struct	napi;
	struct ipoib_rx_buf	*rx_ring;
	struct ib_recv_wr	rx_wr;
	struct ib_sge		rx_sge[IPOIB_UD_RX_SG];
	struct ipoib_rx_cm_info	cm;
	struct ipoib_rx_ring_stats stats;
	unsigned		index;
};

static inline void ipoib_build_sge_rss(struct ipoib_send_ring *send_ring,
				       struct ipoib_tx_buf *tx_req)
{
	int i, off;
	struct sk_buff *skb = tx_req->skb;
	skb_frag_t *frags = skb_shinfo(skb)->frags;
	int nr_frags = skb_shinfo(skb)->nr_frags;
	u64 *mapping = tx_req->mapping;

	if (skb_headlen(skb)) {
		send_ring->tx_sge[0].addr         = mapping[0];
		send_ring->tx_sge[0].length       = skb_headlen(skb);
		off = 1;
	} else
		off = 0;

	for (i = 0; i < nr_frags; ++i) {
		send_ring->tx_sge[i + off].addr = mapping[i + off];
		send_ring->tx_sge[i + off].length = skb_frag_size(&frags[i]);
	}
	send_ring->tx_wr.wr.num_sge = nr_frags + off;
}

struct ipoib_func_pointers {
	/* ipoib_cm function pointers */
	int (*ipoib_cm_nonsrq_init_rx)(struct net_device *dev, struct ib_cm_id *cm_id, struct ipoib_cm_rx *rx);

	void (*ipoib_cm_tx_destroy)(struct ipoib_cm_tx *p);

	struct ib_qp * (*ipoib_cm_create_rx_qp)(struct net_device *dev, struct ipoib_cm_rx *p);

	struct ib_qp * (*ipoib_cm_create_tx_qp)(struct net_device *dev, struct ipoib_cm_tx *tx);

	void (*ipoib_cm_send)(struct net_device *dev, struct sk_buff *skb, struct ipoib_cm_tx *tx);

	/* ipoib_main pointers */
	int (*ipoib_set_mode)(struct net_device *dev, const char *buf);

	struct ipoib_neigh * (*ipoib_neigh_ctor)(u8 *daddr, struct net_device *dev);

	void (*ipoib_dev_cleanup)(struct net_device *dev);

	/* ipoib_ib pointers */
	void (*ipoib_drain_cq)(struct net_device *dev);

	void (*__ipoib_reap_ah)(struct net_device *dev);
};

void ipoib_ib_tx_completion_rss(struct ib_cq *cq, void *ctx_ptr);

int ipoib_ib_dev_init_rss(struct net_device *dev, struct ib_device *ca, int port);

int ipoib_mcast_attach_rss(struct net_device *dev, struct ib_device *hca,
			   union ib_gid *mgid, u16 mlid, int set_qkey, u32 qkey);

int ipoib_init_qp_rss(struct net_device *dev);
int ipoib_transport_dev_init_rss(struct net_device *dev, struct ib_device *ca);
void ipoib_transport_dev_cleanup_rss(struct net_device *dev);

int ipoib_send_rss(struct net_device *dev, struct sk_buff *skb,
		   struct ib_ah *address, u32 dqpn);

void ipoib_ib_completion_rss(struct ib_cq *cq, void *ctx_ptr);

int ipoib_set_rss_sysfs(struct ipoib_dev_priv *priv);

int ipoib_reinit_rss(struct net_device *dev, int num_rx, int num_tx);

/* Function pointer for RSS/TSS an non-RSS/TSS functionality:
 * Since RSS/TSS support modifies many functions in IPoIB code
 * it was decided to split the functionality to separate files to make
 * the base code closer to the upstream code.
 * RSS/TSS is not accepted upstream.  */
void ipoib_cm_rss_init_fp(struct ipoib_dev_priv *priv);
void ipoib_main_rss_init_fp(struct ipoib_dev_priv *priv);
void ipoib_ib_rss_init_fp(struct ipoib_dev_priv *priv);

int ipoib_set_fp_rss(struct ipoib_dev_priv *priv, struct ib_device *hca);

const struct net_device_ops *ipoib_get_netdev_ops(struct ipoib_dev_priv *priv);
const struct net_device_ops *ipoib_get_rn_ops(struct ipoib_dev_priv *priv);

void ipoib_set_ethtool_ops_rss(struct net_device *dev);

int ipoib_ib_dev_open_default_rss(struct net_device *dev);
int ipoib_ib_dev_stop_default_rss(struct net_device *dev);

int ipoib_rx_poll_rss(struct napi_struct *napi, int budget);
int ipoib_tx_poll_rss(struct napi_struct *napi, int budget);

#ifdef CONFIG_INFINIBAND_IPOIB_CM

void ipoib_cm_handle_rx_wc_rss(struct net_device *dev, struct ib_wc *wc);
void ipoib_cm_handle_tx_wc_rss(struct net_device *dev, struct ib_wc *wc);
void ipoib_cm_send_rss(struct net_device *dev, struct sk_buff *skb,
		       struct ipoib_cm_tx *tx);
void ipoib_drain_cq_rss(struct net_device *dev);
#else

static inline int ipoib_cm_handle_rx_wc_rss(struct net_device *dev,
					    struct ib_wc *wc)
{
	return 0;
}
static inline int ipoib_cm_handle_tx_wc_rss(struct net_device *dev,
					    struct ib_wc *wc)
{
	return 0;
}

static inline void ipoib_cm_send_rss(struct net_device *dev,
				     struct sk_buff *skb,
				     struct ipoib_cm_tx *tx)
{
	return;
}
#endif

#endif /* _IPOIB_RSS_H */


