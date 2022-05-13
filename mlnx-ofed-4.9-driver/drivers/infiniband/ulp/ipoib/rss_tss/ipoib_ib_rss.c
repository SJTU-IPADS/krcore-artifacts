/*
 * Copyright (c) 2004, 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Sun Microsystems, Inc. All rights reserved.
 * Copyright (c) 2005 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2004, 2005 Voltaire, Inc. All rights reserved.
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

static int ipoib_ib_post_receive_rss(struct net_device *dev,
				     struct ipoib_recv_ring *recv_ring, int id)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	int ret;

	recv_ring->rx_wr.wr_id   = id | IPOIB_OP_RECV;
	recv_ring->rx_sge[0].addr = recv_ring->rx_ring[id].mapping[0];
	recv_ring->rx_sge[1].addr = recv_ring->rx_ring[id].mapping[1];

	ret = ib_post_recv(recv_ring->recv_qp, &recv_ring->rx_wr, NULL);
	if (unlikely(ret)) {
		ipoib_warn(priv, "receive failed for buf %d (%d)\n", id, ret);
		ipoib_ud_dma_unmap_rx(priv, recv_ring->rx_ring[id].mapping);
		dev_kfree_skb_any(recv_ring->rx_ring[id].skb);
		recv_ring->rx_ring[id].skb = NULL;
	}

	return ret;
}

static struct sk_buff *ipoib_alloc_rx_skb_rss(struct net_device *dev,
					      struct ipoib_recv_ring *recv_ring,
					      int id)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	struct sk_buff *skb;
	int buf_size;
	u64 *mapping;

	buf_size = IPOIB_UD_BUF_SIZE(priv->max_ib_mtu);

	skb = dev_alloc_skb(buf_size + IPOIB_HARD_LEN);
	if (unlikely(!skb))
		return NULL;

	/*
	 * the IP header will be at IPOIP_HARD_LEN + IB_GRH_BYTES, that is
	 * 64 bytes aligned
	 */
	skb_reserve(skb, sizeof(struct ipoib_pseudo_header));

	mapping = recv_ring->rx_ring[id].mapping;
	mapping[0] = ib_dma_map_single(priv->ca, skb->data, buf_size,
				       DMA_FROM_DEVICE);
	if (unlikely(ib_dma_mapping_error(priv->ca, mapping[0])))
		goto error;

	recv_ring->rx_ring[id].skb = skb;
	return skb;
error:
	dev_kfree_skb_any(skb);
	return NULL;
}

static int ipoib_ib_post_ring_receives_rss(struct net_device *dev,
					   struct ipoib_recv_ring *recv_ring)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	int i;

	for (i = 0; i < priv->recvq_size; ++i) {
		if (!ipoib_alloc_rx_skb_rss(dev, recv_ring, i)) {
			ipoib_warn(priv,
				"failed to allocate receive buffer (%d,%d)\n",
				recv_ring->index, i);
			return -ENOMEM;
		}
		if (ipoib_ib_post_receive_rss(dev, recv_ring, i)) {
			ipoib_warn(priv,
				"ipoib_ib_post_receive_rss failed for buf (%d,%d)\n",
				recv_ring->index, i);
			return -EIO;
		}
	}

	return 0;
}

static int ipoib_ib_post_receives_rss(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	struct ipoib_recv_ring *recv_ring;
	int err;
	int i;

	recv_ring = priv->recv_ring;
	for (i = 0; i < priv->num_rx_queues; ++i) {
		err = ipoib_ib_post_ring_receives_rss(dev, recv_ring);
		if (err)
			return err;
		recv_ring++;
	}

	return 0;
}

static void ipoib_ib_handle_rx_wc_rss(struct net_device *dev,
				      struct ipoib_recv_ring *recv_ring,
				      struct ib_wc *wc)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	unsigned int wr_id = wc->wr_id & ~IPOIB_OP_RECV;
	struct sk_buff *skb;
	u64 mapping[IPOIB_UD_RX_SG];
	union ib_gid *dgid;
	union ib_gid *sgid;

	ipoib_dbg_data(priv, "recv completion: id %d, status: %d\n",
		       wr_id, wc->status);

	if (unlikely(wr_id >= priv->recvq_size)) {
		ipoib_warn(priv, "recv completion event with wrid %d (> %d)\n",
			   wr_id, priv->recvq_size);
		return;
	}

	skb  = recv_ring->rx_ring[wr_id].skb;

	if (unlikely(wc->status != IB_WC_SUCCESS)) {
		if (wc->status != IB_WC_WR_FLUSH_ERR)
			ipoib_warn(priv, "failed recv event "
				   "(status=%d, wrid=%d vend_err %x)\n",
				   wc->status, wr_id, wc->vendor_err);
		ipoib_ud_dma_unmap_rx(priv, recv_ring->rx_ring[wr_id].mapping);
		dev_kfree_skb_any(skb);
		recv_ring->rx_ring[wr_id].skb = NULL;
		return;
	}

	memcpy(mapping, recv_ring->rx_ring[wr_id].mapping,
	       IPOIB_UD_RX_SG * sizeof *mapping);

	/*
	 * If we can't allocate a new RX buffer, dump
	 * this packet and reuse the old buffer.
	 */
	if (unlikely(!ipoib_alloc_rx_skb_rss(dev, recv_ring, wr_id))) {
		++recv_ring->stats.rx_dropped;
		goto repost;
	}

	skb_record_rx_queue(skb, recv_ring->index);

	ipoib_dbg_data(priv, "received %d bytes, SLID 0x%04x\n",
		       wc->byte_len, wc->slid);

	ipoib_ud_dma_unmap_rx(priv, mapping);

	skb_put(skb, wc->byte_len);

	/* First byte of dgid signals multicast when 0xff */
	dgid = &((struct ib_grh *)skb->data)->dgid;

	if (!(wc->wc_flags & IB_WC_GRH) || dgid->raw[0] != 0xff)
		skb->pkt_type = PACKET_HOST;
	else if (memcmp(dgid, dev->broadcast + 4, sizeof(union ib_gid)) == 0)
		skb->pkt_type = PACKET_BROADCAST;
	else
		skb->pkt_type = PACKET_MULTICAST;

	sgid = &((struct ib_grh *)skb->data)->sgid;

	/*
	 * Drop packets that this interface sent, ie multicast packets
	 * that the HCA has replicated.
	 * Note with SW TSS MC were sent using priv->qp so no need to mask
	 */
	if (wc->slid == priv->local_lid && wc->src_qp == priv->qp->qp_num) {
		int need_repost = 1;

		if ((wc->wc_flags & IB_WC_GRH) &&
		    sgid->global.interface_id != priv->local_gid.global.interface_id)
			need_repost = 0;

		if (need_repost) {
			dev_kfree_skb_any(skb);
			goto repost;
		}
	}

	skb_pull(skb, IB_GRH_BYTES);

	skb->protocol = ((struct ipoib_header *) skb->data)->proto;
	skb_add_pseudo_hdr(skb);

	++recv_ring->stats.rx_packets;
	recv_ring->stats.rx_bytes += skb->len;
	if (skb->pkt_type == PACKET_MULTICAST)
		recv_ring->stats.multicast++;

	if (unlikely(be16_to_cpu(skb->protocol) == ETH_P_ARP))
		ipoib_create_repath_ent(dev, skb, wc->slid);

	skb->dev = dev;
	if ((dev->features & NETIF_F_RXCSUM) &&
			likely(wc->wc_flags & IB_WC_IP_CSUM_OK))
		skb->ip_summed = CHECKSUM_UNNECESSARY;

	napi_gro_receive(&recv_ring->napi, skb);

repost:
	if (unlikely(ipoib_ib_post_receive_rss(dev, recv_ring, wr_id)))
		ipoib_warn(priv, "ipoib_ib_post_receive_rss failed "
			   "for buf %d\n", wr_id);
}

static void ipoib_ib_handle_tx_wc_rss(struct ipoib_send_ring *send_ring,
				      struct ib_wc *wc)
{
	struct net_device *dev = send_ring->dev;
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	unsigned int wr_id = wc->wr_id;
	struct ipoib_tx_buf *tx_req;

	ipoib_dbg_data(priv, "send completion: id %d, status: %d\n",
		       wr_id, wc->status);

	if (unlikely(wr_id >= priv->sendq_size)) {
		ipoib_warn(priv, "send completion event with wrid %d (> %d)\n",
			   wr_id, priv->sendq_size);
		return;
	}

	tx_req = &send_ring->tx_ring[wr_id];
	if (!tx_req->is_inline)
		ipoib_dma_unmap_tx(priv, tx_req);

	++send_ring->stats.tx_packets;
	send_ring->stats.tx_bytes += tx_req->skb->len;

	dev_kfree_skb_any(tx_req->skb);
	tx_req->skb = NULL;

	++send_ring->tx_tail;
	atomic_dec(&send_ring->tx_outstanding);

	if (unlikely(__netif_subqueue_stopped(dev, send_ring->index) &&
		     (atomic_read(&send_ring->tx_outstanding) <= priv->sendq_size >> 1) &&
		     test_bit(IPOIB_FLAG_ADMIN_UP, &priv->flags)))
		netif_wake_subqueue(dev, send_ring->index);

	if (wc->status != IB_WC_SUCCESS &&
	    wc->status != IB_WC_WR_FLUSH_ERR) {
		struct ipoib_qp_state_validate *qp_work;
		ipoib_warn(priv, "failed send event "
			   "(status=%d, wrid=%d vend_err %x)\n",
			   wc->status, wr_id, wc->vendor_err);
		qp_work = kzalloc(sizeof(*qp_work), GFP_ATOMIC);
		if (!qp_work) {
			ipoib_warn(priv, "%s Failed alloc ipoib_qp_state_validate for qp: 0x%x\n",
				   __func__, priv->qp->qp_num);
			return;
		}

		INIT_WORK(&qp_work->work, ipoib_qp_state_validate_work);
		qp_work->priv = priv;
		queue_work(priv->wq, &qp_work->work);
	}
}

int ipoib_rx_poll_rss(struct napi_struct *napi, int budget)
{
	struct ipoib_recv_ring *rx_ring;
	struct net_device *dev;
	int done;
	int t;
	int n, i;

	done  = 0;
	rx_ring = container_of(napi, struct ipoib_recv_ring, napi);
	dev = rx_ring->dev;

poll_more:
	while (done < budget) {
		int max = (budget - done);

		t = min(IPOIB_NUM_WC, max);
		n = ib_poll_cq(rx_ring->recv_cq, t, rx_ring->ibwc);

		for (i = 0; i < n; i++) {
			struct ib_wc *wc = rx_ring->ibwc + i;

			if (wc->wr_id & IPOIB_OP_RECV) {
				++done;
				if (wc->wr_id & IPOIB_OP_CM)
					ipoib_cm_handle_rx_wc_rss(dev, wc);
				else
					ipoib_ib_handle_rx_wc_rss(dev, rx_ring, wc);
			} else {
				pr_warn("%s: Got unexpected wqe id\n", __func__);
			}
		}

		if (n != t)
			break;
	}

	if (done < budget) {
		napi_complete(napi);
		if (unlikely(ib_req_notify_cq(rx_ring->recv_cq,
					      IB_CQ_NEXT_COMP |
					      IB_CQ_REPORT_MISSED_EVENTS)) &&
		    napi_reschedule(napi))
			goto poll_more;
	}

	return done;
}

int ipoib_tx_poll_rss(struct napi_struct *napi, int budget)
{
	struct ipoib_send_ring *send_ring;
	struct net_device *dev;
	int n, i;
	struct ib_wc *wc;

	send_ring = container_of(napi, struct ipoib_send_ring, napi);
	dev = send_ring->dev;

poll_more:
	n = ib_poll_cq(send_ring->send_cq, MAX_SEND_CQE, send_ring->tx_wc);

	for (i = 0; i < n; i++) {
		wc = send_ring->tx_wc + i;
		if (wc->wr_id & IPOIB_OP_CM)
			ipoib_cm_handle_tx_wc_rss(dev, wc);
		else
			ipoib_ib_handle_tx_wc_rss(send_ring, wc);
	}

	if (n < budget) {
		napi_complete(napi);
		if (unlikely(ib_req_notify_cq(send_ring->send_cq,
					      IB_CQ_NEXT_COMP)) &&
		    napi_reschedule(napi))
			goto poll_more;
	}

	return n < 0 ? 0 : n;
}

static int poll_tx_ring(struct ipoib_send_ring *send_ring)
{
	int n, i;
	struct ib_wc *wc;

	n = ib_poll_cq(send_ring->send_cq, MAX_SEND_CQE, send_ring->tx_wc);

	for (i = 0; i < n; ++i) {
		wc = send_ring->tx_wc + i;
		if (wc->wr_id & IPOIB_OP_CM)
			ipoib_cm_handle_tx_wc_rss(send_ring->dev, wc);
		else
			ipoib_ib_handle_tx_wc_rss(send_ring, wc);
	}

	return n == MAX_SEND_CQE;
}

void ipoib_ib_completion_rss(struct ib_cq *cq, void *ctx_ptr)
{
	struct ipoib_recv_ring *recv_ring = (struct ipoib_recv_ring *) ctx_ptr;
	napi_schedule(&recv_ring->napi);
}

static inline int post_send_rss(struct ipoib_send_ring *send_ring,
				unsigned int wr_id,
				struct ib_ah *address, u32 dqpn,
				struct ipoib_tx_buf *tx_req,
				void *head, int hlen)
{
	struct sk_buff *skb = tx_req->skb;

	if (tx_req->is_inline) {
		send_ring->tx_sge[0].addr	= (u64)skb->data;
		send_ring->tx_sge[0].length	= skb->len;
		send_ring->tx_wr.wr.num_sge	= 1;
	} else {
		ipoib_build_sge_rss(send_ring, tx_req);
	}

	send_ring->tx_wr.wr.wr_id	= wr_id;
	send_ring->tx_wr.remote_qpn	= dqpn;
	send_ring->tx_wr.ah		= address;

	if (head) {
		send_ring->tx_wr.mss = skb_shinfo(skb)->gso_size;
		send_ring->tx_wr.header = head;
		send_ring->tx_wr.hlen = hlen;
		send_ring->tx_wr.wr.opcode = IB_WR_LSO;
	} else
		send_ring->tx_wr.wr.opcode = IB_WR_SEND;

	return ib_post_send(send_ring->send_qp, &send_ring->tx_wr.wr, NULL);
}

int ipoib_send_rss(struct net_device *dev, struct sk_buff *skb,
		   struct ib_ah *address, u32 dqpn)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	struct ipoib_tx_buf *tx_req;
	struct ipoib_send_ring *send_ring;
	u16 queue_index;
	int hlen, rc;
	void *phead;
	int req_index;
	unsigned usable_sge = priv->max_send_sge - !!skb_headlen(skb);

	/* Find the correct QP to submit the IO to */
	queue_index = skb_get_queue_mapping(skb);
	send_ring = priv->send_ring + queue_index;

	if (skb_is_gso(skb)) {
		hlen = skb_transport_offset(skb) + tcp_hdrlen(skb);
		phead = skb->data;
		if (unlikely(!skb_pull(skb, hlen))) {
			ipoib_warn(priv, "linear data too small\n");
			++send_ring->stats.tx_dropped;
			++send_ring->stats.tx_errors;
			dev_kfree_skb_any(skb);
			return -1;
		}
	} else {
		if (unlikely(skb->len > priv->mcast_mtu + IPOIB_ENCAP_LEN)) {
			ipoib_warn(priv, "packet len %d (> %d) too long to send, dropping\n",
				   skb->len, priv->mcast_mtu + IPOIB_ENCAP_LEN);
			++send_ring->stats.tx_dropped;
			++send_ring->stats.tx_errors;
			ipoib_cm_skb_too_long(dev, skb, priv->mcast_mtu);
			return -1;
		}
		phead = NULL;
		hlen  = 0;
	}
	if (skb_shinfo(skb)->nr_frags > usable_sge) {
		if (skb_linearize(skb) < 0) {
			ipoib_warn(priv, "skb could not be linearized\n");
			++send_ring->stats.tx_dropped;
			++send_ring->stats.tx_errors;
			dev_kfree_skb_any(skb);
			return -1;
		}
		/* Does skb_linearize return ok without reducing nr_frags? */
		if (skb_shinfo(skb)->nr_frags > usable_sge) {
			ipoib_warn(priv, "too many frags after skb linearize\n");
			++send_ring->stats.tx_dropped;
			++send_ring->stats.tx_errors;
			dev_kfree_skb_any(skb);
			return -1;
		}
	}

	ipoib_dbg_data(priv, "sending packet, length=%d address=%p qpn=0x%06x\n",
		       skb->len, address, dqpn);

	/*
	 * We put the skb into the tx_ring _before_ we call post_send_rss()
	 * because it's entirely possible that the completion handler will
	 * run before we execute anything after the post_send_rss().  That
	 * means we have to make sure everything is properly recorded and
	 * our state is consistent before we call post_send_rss().
	 */
	req_index = send_ring->tx_head & (priv->sendq_size - 1);
	tx_req = &send_ring->tx_ring[req_index];
	tx_req->skb = skb;

	if (skb->len < ipoib_inline_thold &&
	    !skb_shinfo(skb)->nr_frags) {
		tx_req->is_inline = 1;
		send_ring->tx_wr.wr.send_flags |= IB_SEND_INLINE;
	} else {
		if (unlikely(ipoib_dma_map_tx(priv->ca, tx_req))) {
			++send_ring->stats.tx_errors;
			dev_kfree_skb_any(skb);
			return -1;
		}
		tx_req->is_inline = 0;
		send_ring->tx_wr.wr.send_flags &= ~IB_SEND_INLINE;
	}

	if (skb->ip_summed == CHECKSUM_PARTIAL)
		send_ring->tx_wr.wr.send_flags |= IB_SEND_IP_CSUM;
	else
		send_ring->tx_wr.wr.send_flags &= ~IB_SEND_IP_CSUM;
	/* increase the tx_head after send success, but use it for queue state */
	if (atomic_read(&send_ring->tx_outstanding) == priv->sendq_size - 1) {
		ipoib_dbg(priv, "TX ring full, stopping kernel net queue\n");
		netif_stop_subqueue(dev, queue_index);
	}

	skb_orphan(skb);
	skb_dst_drop(skb);

	if (__netif_subqueue_stopped(dev, queue_index))
		if (ib_req_notify_cq(send_ring->send_cq, IB_CQ_NEXT_COMP |
				     IB_CQ_REPORT_MISSED_EVENTS))
			ipoib_warn(priv, "request notify on send CQ failed\n");

	rc = post_send_rss(send_ring, req_index,
			   address, dqpn, tx_req, phead, hlen);
	if (unlikely(rc)) {
		ipoib_warn(priv, "post_send_rss failed, error %d\n", rc);
		++send_ring->stats.tx_errors;
		if (!tx_req->is_inline)
			ipoib_dma_unmap_tx(priv, tx_req);
		dev_kfree_skb_any(skb);
		if (__netif_subqueue_stopped(dev, queue_index))
			netif_wake_subqueue(dev, queue_index);
		rc = 0;
	} else {
		netdev_get_tx_queue(dev, queue_index)->trans_start = jiffies;
		rc = send_ring->tx_head;
		++send_ring->tx_head;
		atomic_inc(&send_ring->tx_outstanding);
	}

	return rc;
}

/* The function will force napi_schedule */
void ipoib_napi_schedule_work_tss(struct work_struct *work)
{
	struct ipoib_send_ring *send_ring =
		container_of(work, struct ipoib_send_ring, reschedule_napi_work);
	struct ipoib_dev_priv *priv;
	bool ret;

	priv = ipoib_priv(send_ring->dev);

	do {
		ret = napi_reschedule(&send_ring->napi);
		if (!ret)
			msleep(3);
	} while (!ret && __netif_subqueue_stopped(send_ring->dev, send_ring->index) &&
		 test_bit(IPOIB_FLAG_INITIALIZED, &priv->flags));
}

void ipoib_ib_tx_completion_rss(struct ib_cq *cq, void *ctx_ptr)
{
	struct ipoib_send_ring *send_ring = (struct ipoib_send_ring *)ctx_ptr;
	bool ret;

	ret = napi_reschedule(&send_ring->napi);
	/*
	 * if the queue is closed the driver must be able to schedule napi,
	 * otherwise we can end with closed queue forever, because no new
	 * packets to send and napi callback might not get new event after
	 * its re-arm of the napi.
	 */
	if (!ret && __netif_subqueue_stopped(send_ring->dev, send_ring->index))
		schedule_work(&send_ring->reschedule_napi_work);
}

static void ipoib_napi_enable_rss(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	int i;

	for (i = 0; i < priv->num_rx_queues; i++)
		napi_enable(&priv->recv_ring[i].napi);

	for (i = 0; i < priv->num_tx_queues; i++) {
		napi_enable(&priv->send_ring[i].napi);
		INIT_WORK(&priv->send_ring[i].reschedule_napi_work,
			  ipoib_napi_schedule_work_tss);
	}
}

static void ipoib_napi_disable_rss(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	int i;

	for (i = 0; i < priv->num_rx_queues; i++)
		napi_disable(&priv->recv_ring[i].napi);

	for (i = 0; i < priv->num_tx_queues; i++)
		napi_disable(&priv->send_ring[i].napi);
}

int ipoib_ib_dev_open_default_rss(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	struct ipoib_recv_ring *recv_ring;
	int ret, i;

	/* Re-arm the RX CQs due to a race between completion event and arm
	 * during default stop. This fix is temporary and should be removed
	 * once the mlx4/5 bug is solved
	 */
	recv_ring = priv->recv_ring;
	for (i = 0; i < priv->num_rx_queues; ++i) {
		ib_req_notify_cq(recv_ring->recv_cq, IB_CQ_NEXT_COMP);
		recv_ring++;
	}

	ret = ipoib_init_qp_rss(dev);
	if (ret) {
		ipoib_warn(priv, "ipoib_init_qp returned %d\n", ret);
		return -1;
	}

	ret = ipoib_ib_post_receives_rss(dev);
	if (ret) {
		ipoib_warn(priv, "ipoib_ib_post_receives_rss returned %d\n", ret);
		goto out;
	}

	ret = ipoib_cm_dev_open(dev);
	if (ret) {
		ipoib_warn(priv, "ipoib_cm_dev_open returned %d\n", ret);
		goto out;
	}

	if (!test_bit(IPOIB_FLAG_INITIALIZED, &priv->flags))
		ipoib_napi_enable_rss(dev);

	return 0;
out:
	return -1;
}

static int recvs_pending_rss(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	struct ipoib_recv_ring *recv_ring;
	int pending = 0;
	int i, j;

	recv_ring = priv->recv_ring;
	for (j = 0; j < priv->num_rx_queues; j++) {
		for (i = 0; i < priv->recvq_size; ++i) {
			if (recv_ring->rx_ring[i].skb)
				++pending;
		}
		recv_ring++;
	}

	return pending;
}

static int sends_pending_rss(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	struct ipoib_send_ring *send_ring;
	int pending = 0;
	int i;

	send_ring = priv->send_ring;
	for (i = 0; i < priv->num_tx_queues; i++) {
		/*
		* Note that since head and tails are unsigned then
		* the result of the substruction is correct even when
		* the counters wrap around
		*/
		pending += send_ring->tx_head - send_ring->tx_tail;
		send_ring++;
	}

	return pending;
}

static void ipoib_drain_rx_ring(struct ipoib_dev_priv *priv,
				struct ipoib_recv_ring *rx_ring)
{
	struct net_device *dev = priv->dev;
	int i, n;

	/*
	 * We call completion handling routines that expect to be
	 * called from the BH-disabled NAPI poll context, so disable
	 * BHs here too.
	 */
	local_bh_disable();

	do {
		n = ib_poll_cq(rx_ring->recv_cq, IPOIB_NUM_WC, rx_ring->ibwc);
		for (i = 0; i < n; ++i) {
			/*
			 * Convert any successful completions to flush
			 * errors to avoid passing packets up the
			 * stack after bringing the device down.
			 */
			if (rx_ring->ibwc[i].status == IB_WC_SUCCESS)
				rx_ring->ibwc[i].status = IB_WC_WR_FLUSH_ERR;

			if (rx_ring->ibwc[i].wr_id & IPOIB_OP_RECV) {
				if (rx_ring->ibwc[i].wr_id & IPOIB_OP_CM)
					ipoib_cm_handle_rx_wc_rss(dev, rx_ring->ibwc + i);
				else
					ipoib_ib_handle_rx_wc_rss(dev, rx_ring,
								  rx_ring->ibwc + i);
			} else
				pr_warn("%s got strange wrid: %llu\n",
					__func__, rx_ring->ibwc[i].wr_id);
		}
	} while (n == IPOIB_NUM_WC);

	local_bh_enable();
}

static void drain_rx_rings(struct ipoib_dev_priv *priv)
{
	struct ipoib_recv_ring *recv_ring;
	int i;

	recv_ring = priv->recv_ring;
	for (i = 0; i < priv->num_rx_queues; i++) {
		ipoib_drain_rx_ring(priv, recv_ring);
		recv_ring++;
	}
}

static void drain_tx_rings(struct ipoib_dev_priv *priv)
{
	struct ipoib_send_ring *send_ring;
	int bool_value = 0;
	int i;

	do {
		bool_value = 0;
		send_ring = priv->send_ring;
		for (i = 0; i < priv->num_tx_queues; i++) {
			local_bh_disable();
			bool_value |= poll_tx_ring(send_ring);
			local_bh_enable();
			send_ring++;
		}
	} while (bool_value);
}

void ipoib_drain_cq_rss(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);

	drain_rx_rings(priv);

	drain_tx_rings(priv);
}

static void ipoib_ib_send_ring_stop(struct ipoib_dev_priv *priv)
{
	struct ipoib_send_ring *tx_ring;
	struct ipoib_tx_buf *tx_req;
	int i;

	tx_ring = priv->send_ring;
	for (i = 0; i < priv->num_tx_queues; i++) {
		while ((int) tx_ring->tx_tail - (int) tx_ring->tx_head < 0) {
			tx_req = &tx_ring->tx_ring[tx_ring->tx_tail &
				  (priv->sendq_size - 1)];
			if (!tx_req->skb) {
				ipoib_dbg(priv,
					  "timing out; tx skb already empty - continue\n");
				++tx_ring->tx_tail;
				continue;
			}
			if (!tx_req->is_inline)
				ipoib_dma_unmap_tx(priv, tx_req);
			dev_kfree_skb_any(tx_req->skb);
			tx_req->skb = NULL;
			++tx_ring->tx_tail;
			atomic_dec(&tx_ring->tx_outstanding);
		}
		tx_ring++;
	}
}

static void ipoib_ib_recv_ring_stop(struct ipoib_dev_priv *priv)
{
	struct ipoib_recv_ring *recv_ring;
	int i, j;

	recv_ring = priv->recv_ring;
	for (j = 0; j < priv->num_rx_queues; ++j) {
		for (i = 0; i < priv->recvq_size; ++i) {
			struct ipoib_rx_buf *rx_req;

			rx_req = &recv_ring->rx_ring[i];
			if (!rx_req->skb)
				continue;
			ipoib_ud_dma_unmap_rx(priv,
					      recv_ring->rx_ring[i].mapping);
			dev_kfree_skb_any(rx_req->skb);
			rx_req->skb = NULL;
		}
		recv_ring++;
	}
}

static void set_tx_rings_qp_state(struct ipoib_dev_priv *priv,
				  enum ib_qp_state new_state)
{
	struct ipoib_send_ring *send_ring;
	struct ib_qp_attr qp_attr;
	int i;

	send_ring = priv->send_ring;
	for (i = 0; i <  priv->num_tx_queues; i++) {
		qp_attr.qp_state = new_state;
		if (ib_modify_qp(send_ring->send_qp, &qp_attr, IB_QP_STATE))
			check_qp_movement_and_print(priv, send_ring->send_qp,
						    new_state);
		send_ring++;
	}
}

static void set_rx_rings_qp_state(struct ipoib_dev_priv *priv,
				  enum ib_qp_state new_state)
{
	struct ipoib_recv_ring *recv_ring;
	struct ib_qp_attr qp_attr;
	int i;

	recv_ring = priv->recv_ring;
	for (i = 0; i < priv->num_rx_queues; i++) {
		qp_attr.qp_state = new_state;
		if (ib_modify_qp(recv_ring->recv_qp, &qp_attr, IB_QP_STATE))
			check_qp_movement_and_print(priv, recv_ring->recv_qp,
						    new_state);
		recv_ring++;
	}
}

static void set_rings_qp_state(struct ipoib_dev_priv *priv,
			       enum ib_qp_state new_state)
{
	set_tx_rings_qp_state(priv, new_state);

	if (priv->num_rx_queues > 1)
		set_rx_rings_qp_state(priv, new_state);
}

int ipoib_ib_dev_stop_default_rss(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	unsigned long begin;
	struct ipoib_recv_ring *recv_ring;
	int i;

	if (test_bit(IPOIB_FLAG_INITIALIZED, &priv->flags))
		ipoib_napi_disable_rss(dev);

	ipoib_cm_dev_stop(dev);

	/*
	 * Move our QP to the error state and then reinitialize in
	 * when all work requests have completed or have been flushed.
	 */
	set_rings_qp_state(priv, IB_QPS_ERR);

	/* Wait for all sends and receives to complete */
	begin = jiffies;

	while (sends_pending_rss(dev) || recvs_pending_rss(dev)) {
		if (time_after(jiffies, begin + 5 * HZ)) {
			ipoib_warn(priv, "timing out; %d sends %d receives not completed\n",
				   sends_pending_rss(dev), recvs_pending_rss(dev));

			/*
			 * assume the HW is wedged and just free up
			 * all our pending work requests.
			 */
			ipoib_ib_send_ring_stop(priv);

			ipoib_ib_recv_ring_stop(priv);

			goto timeout;
		}

		ipoib_drain_cq_rss(dev);

		msleep(1);
	}

	ipoib_dbg(priv, "All sends and receives done.\n");

timeout:
	set_rings_qp_state(priv, IB_QPS_RESET);

	recv_ring = priv->recv_ring;
	for (i = 0; i < priv->num_rx_queues; ++i) {
		ib_req_notify_cq(recv_ring->recv_cq, IB_CQ_NEXT_COMP);
		recv_ring++;
	}
	return 0;
}

static void __ipoib_reap_ah_rss(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	struct ipoib_ah *ah, *tah;
	LIST_HEAD(remove_list);
	unsigned long flags;

	netif_tx_lock_bh(dev);
	spin_lock_irqsave(&priv->lock, flags);

	list_for_each_entry_safe(ah, tah, &priv->dead_ahs, list) {
		list_del(&ah->list);
		rdma_destroy_ah(ah->ah, 0);
		kfree(ah);
	}
	spin_unlock_irqrestore(&priv->lock, flags);
	netif_tx_unlock_bh(dev);
}

void ipoib_ib_rss_init_fp(struct ipoib_dev_priv *priv)
{
	if (priv->max_tx_queues > 1) {
		priv->fp.ipoib_drain_cq = ipoib_drain_cq_rss;
		priv->fp.__ipoib_reap_ah = __ipoib_reap_ah_rss;
	} else {
		priv->fp.ipoib_drain_cq = ipoib_drain_cq;
		priv->fp.__ipoib_reap_ah = __ipoib_reap_ah;
	}
}
