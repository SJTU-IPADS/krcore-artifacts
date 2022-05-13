/*
 * Copyright (c) 2006 Mellanox Technologies. All rights reserved
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

static int ipoib_cm_post_receive_srq_rss(struct net_device *dev,
					 int index, int id)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	struct ipoib_recv_ring *recv_ring = priv->recv_ring + index;
	struct ib_sge *sge;
	struct ib_recv_wr *wr;
	int i, ret;

	sge = recv_ring->cm.rx_sge;
	wr = &recv_ring->cm.rx_wr;

	wr->wr_id = id | IPOIB_OP_CM | IPOIB_OP_RECV;

	for (i = 0; i < priv->cm.num_frags; ++i)
		sge[i].addr = priv->cm.srq_ring[id].mapping[i];

	ret = ib_post_srq_recv(priv->cm.srq, wr, NULL);
	if (unlikely(ret)) {
		ipoib_warn(priv, "post srq failed for buf %d (%d)\n", id, ret);
		ipoib_cm_dma_unmap_rx(priv, priv->cm.num_frags - 1,
				      priv->cm.srq_ring[id].mapping);
		dev_kfree_skb_any(priv->cm.srq_ring[id].skb);
		priv->cm.srq_ring[id].skb = NULL;
	}

	return ret;
}

static int ipoib_cm_post_receive_nonsrq_rss(struct net_device *dev,
					    struct ipoib_cm_rx *rx, int id)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	struct ipoib_recv_ring *recv_ring = priv->recv_ring + rx->index;
	struct ib_sge *sge;
	struct ib_recv_wr *wr;
	int i, ret;

	sge = recv_ring->cm.rx_sge;
	wr = &recv_ring->cm.rx_wr;

	wr->wr_id = id | IPOIB_OP_CM | IPOIB_OP_RECV;

	for (i = 0; i < IPOIB_CM_RX_SG; ++i)
		sge[i].addr = rx->rx_ring[id].mapping[i];

	ret = ib_post_recv(rx->qp, wr, NULL);
	if (unlikely(ret)) {
		ipoib_warn(priv, "post recv failed for buf %d (%d)\n", id, ret);
		ipoib_cm_dma_unmap_rx(priv, IPOIB_CM_RX_SG - 1,
				      rx->rx_ring[id].mapping);
		dev_kfree_skb_any(rx->rx_ring[id].skb);
		rx->rx_ring[id].skb = NULL;
	}

	return ret;
}

static struct ib_qp *ipoib_cm_create_rx_qp_rss(struct net_device *dev,
					       struct ipoib_cm_rx *p)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	struct ib_qp_init_attr attr = {
		.event_handler = ipoib_cm_rx_event_handler,
		.srq = priv->cm.srq,
		.cap.max_send_wr = 1, /* For drain WR */
		.cap.max_send_sge = 1, /* FIXME: 0 Seems not to work */
		.sq_sig_type = IB_SIGNAL_ALL_WR,
		.qp_type = IB_QPT_RC,
		.qp_context = p,
	};
	int index;

	if (!ipoib_cm_has_srq(dev)) {
		attr.cap.max_recv_wr  = priv->recvq_size;
		attr.cap.max_recv_sge = IPOIB_CM_RX_SG;
	}

	index = priv->cm.rx_cq_ind;
	if (index >= priv->num_rx_queues)
		index = 0;

	priv->cm.rx_cq_ind = index + 1;
	/* send_cp for drain WR */
	attr.send_cq = attr.recv_cq = priv->recv_ring[index].recv_cq;
	p->index = index;

	return ib_create_qp(priv->pd, &attr);
}

static void ipoib_cm_init_rx_wr_rss(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	struct ipoib_recv_ring *recv_ring = priv->recv_ring;
	struct ib_sge *sge;
	struct ib_recv_wr *wr;
	int i, j;

	for (j = 0; j < priv->num_rx_queues; j++, recv_ring++) {
		sge = recv_ring->cm.rx_sge;
		wr = &recv_ring->cm.rx_wr;
		for (i = 0; i < priv->cm.num_frags; ++i)
			sge[i].lkey = priv->pd->local_dma_lkey;

		sge[0].length = IPOIB_CM_HEAD_SIZE;
		for (i = 1; i < priv->cm.num_frags; ++i)
			sge[i].length = PAGE_SIZE;

		wr->next    = NULL;
		wr->sg_list = sge;
		wr->num_sge = priv->cm.num_frags;
	}
}

static int ipoib_cm_nonsrq_init_rx_rss(struct net_device *dev, struct ib_cm_id *cm_id,
				       struct ipoib_cm_rx *rx)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	int ret;
	int i;

	rx->rx_ring = vzalloc(priv->recvq_size * sizeof(*rx->rx_ring));
	if (!rx->rx_ring) {
		printk(KERN_WARNING "%s: failed to allocate CM non-SRQ ring (%d entries)\n",
		       priv->ca->name, priv->recvq_size);
		return -ENOMEM;
	}

	spin_lock_irq(&priv->lock);

	if (priv->cm.nonsrq_conn_qp >= ipoib_max_conn_qp) {
		spin_unlock_irq(&priv->lock);
		ib_send_cm_rej(cm_id, IB_CM_REJ_NO_QP, NULL, 0, NULL, 0);
		ret = -EINVAL;
		goto err_free;
	} else
		++priv->cm.nonsrq_conn_qp;

	spin_unlock_irq(&priv->lock);

	for (i = 0; i < priv->recvq_size; ++i) {
		if (!ipoib_cm_alloc_rx_skb(dev, rx->rx_ring, i, IPOIB_CM_RX_SG - 1,
					   rx->rx_ring[i].mapping,
					   GFP_KERNEL)) {
			ipoib_warn(priv, "failed to allocate receive buffer %d\n", i);
			ret = -ENOMEM;
			goto err_count;
		}
		ret = ipoib_cm_post_receive_nonsrq_rss(dev, rx, i);
		if (ret) {
			ipoib_warn(priv, "ipoib_cm_post_receive_nonsrq_rss "
				   "failed for buf %d\n", i);
			ret = -EIO;
			goto err_count;
		}
	}

	rx->recv_count = priv->recvq_size;

	return 0;

err_count:
	spin_lock_irq(&priv->lock);
	--priv->cm.nonsrq_conn_qp;
	spin_unlock_irq(&priv->lock);

err_free:
	ipoib_cm_free_rx_ring(dev, rx->rx_ring);

	return ret;
}

void ipoib_cm_handle_rx_wc_rss(struct net_device *dev, struct ib_wc *wc)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	struct ipoib_cm_rx_buf *rx_ring;
	unsigned int wr_id = wc->wr_id & ~(IPOIB_OP_CM | IPOIB_OP_RECV);
	struct sk_buff *skb, *newskb;
	struct ipoib_cm_rx *p;
	unsigned long flags;
	u64 mapping[IPOIB_CM_RX_SG];
	int frags;
	int has_srq;
	struct sk_buff *small_skb;

	ipoib_dbg_data(priv, "cm recv completion: id %d, status: %d\n",
		       wr_id, wc->status);

	if (unlikely(wr_id >= priv->recvq_size)) {
		if (wr_id == (IPOIB_CM_RX_DRAIN_WRID & ~(IPOIB_OP_CM | IPOIB_OP_RECV))) {
			spin_lock_irqsave(&priv->lock, flags);
			list_splice_init(&priv->cm.rx_drain_list, &priv->cm.rx_reap_list);
			ipoib_cm_start_rx_drain(priv);
			queue_work(priv->wq, &priv->cm.rx_reap_task);
			spin_unlock_irqrestore(&priv->lock, flags);
		} else
			ipoib_warn(priv, "cm recv completion event with wrid %d (> %d)\n",
				   wr_id, priv->recvq_size);
		return;
	}

	p = wc->qp->qp_context;

	has_srq = ipoib_cm_has_srq(dev);
	rx_ring = has_srq ? priv->cm.srq_ring : p->rx_ring;

	skb = rx_ring[wr_id].skb;

	if (unlikely(wc->status != IB_WC_SUCCESS)) {
		ipoib_dbg(priv, "cm recv error "
			   "(status=%d, wrid=%d vend_err %x)\n",
			   wc->status, wr_id, wc->vendor_err);
		++priv->recv_ring[p->index].stats.rx_dropped;
		if (has_srq)
			goto repost;
		else {
			if (!--p->recv_count) {
				spin_lock_irqsave(&priv->lock, flags);
				list_move(&p->list, &priv->cm.rx_reap_list);
				spin_unlock_irqrestore(&priv->lock, flags);
				queue_work(priv->wq, &priv->cm.rx_reap_task);
			}
			return;
		}
	}

	if (unlikely(!(wr_id & IPOIB_CM_RX_UPDATE_MASK))) {
		if (p && time_after_eq(jiffies, p->jiffies + IPOIB_CM_RX_UPDATE_TIME)) {
			spin_lock_irqsave(&priv->lock, flags);
			p->jiffies = jiffies;
			/* Move this entry to list head, but do not re-add it
			 * if it has been moved out of list. */
			if (p->state == IPOIB_CM_RX_LIVE)
				list_move(&p->list, &priv->cm.passive_ids);
			spin_unlock_irqrestore(&priv->lock, flags);
		}
	}

	if (wc->byte_len < IPOIB_CM_COPYBREAK) {
		int dlen = wc->byte_len;

		small_skb = dev_alloc_skb(dlen + IPOIB_CM_RX_RESERVE);
		if (small_skb) {
			skb_reserve(small_skb, IPOIB_CM_RX_RESERVE);
			ib_dma_sync_single_for_cpu(priv->ca, rx_ring[wr_id].mapping[0],
						   dlen, DMA_FROM_DEVICE);
			skb_copy_from_linear_data(skb, small_skb->data, dlen);
			ib_dma_sync_single_for_device(priv->ca, rx_ring[wr_id].mapping[0],
						      dlen, DMA_FROM_DEVICE);
			skb_put(small_skb, dlen);
			skb = small_skb;
			goto copied;
		}
	}

	frags = PAGE_ALIGN(wc->byte_len - min(wc->byte_len,
					      (unsigned)IPOIB_CM_HEAD_SIZE)) / PAGE_SIZE;

	newskb = ipoib_cm_alloc_rx_skb(dev, rx_ring, wr_id, frags,
				       mapping, GFP_ATOMIC);
	if (unlikely(!newskb)) {
		/*
		 * If we can't allocate a new RX buffer, dump
		 * this packet and reuse the old buffer.
		 */
		ipoib_dbg(priv, "failed to allocate receive buffer %d\n", wr_id);
		++priv->recv_ring[p->index].stats.rx_dropped;
		goto repost;
	}

	ipoib_cm_dma_unmap_rx(priv, frags, rx_ring[wr_id].mapping);
	memcpy(rx_ring[wr_id].mapping, mapping, (frags + 1) * sizeof *mapping);

	ipoib_dbg_data(priv, "received %d bytes, SLID 0x%04x\n",
		       wc->byte_len, wc->slid);

	skb_put_frags(skb, IPOIB_CM_HEAD_SIZE, wc->byte_len, newskb);

copied:
	skb->protocol = ((struct ipoib_header *) skb->data)->proto;
	skb_add_pseudo_hdr(skb);

	++priv->recv_ring[p->index].stats.rx_packets;
	priv->recv_ring[p->index].stats.rx_bytes += skb->len;

	skb->dev = dev;
	/* XXX get correct PACKET_ type here */
	skb->pkt_type = PACKET_HOST;
	netif_receive_skb(skb);

repost:
	if (has_srq) {
		if (unlikely(ipoib_cm_post_receive_srq_rss(dev,
							p->index,
							wr_id)))
			ipoib_warn(priv, "ipoib_cm_post_receive_srq_rss failed "
				   "for buf %d\n", wr_id);
	} else {
		if (unlikely(ipoib_cm_post_receive_nonsrq_rss(dev, p,
							      wr_id))) {
			--p->recv_count;
			ipoib_warn(priv, "ipoib_cm_post_receive_nonsrq_rss failed "
				   "for buf %d\n", wr_id);
		}
	}
}

static inline int post_send_rss(struct ipoib_dev_priv *priv,
				struct ipoib_cm_tx *tx,
				unsigned int wr_id,
				struct ipoib_tx_buf *tx_req,
				struct ipoib_send_ring *send_ring)
{
	ipoib_build_sge_rss(send_ring, tx_req);

	send_ring->tx_wr.wr.wr_id	= wr_id | IPOIB_OP_CM;

	return ib_post_send(tx->qp, &send_ring->tx_wr.wr, NULL);
}

void ipoib_cm_send_rss(struct net_device *dev, struct sk_buff *skb, struct ipoib_cm_tx *tx)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	struct ipoib_tx_buf *tx_req;
	int rc;
	unsigned usable_sge = tx->max_send_sge - !!skb_headlen(skb);
	struct ipoib_send_ring *send_ring;
	u16 queue_index;

	queue_index = skb_get_queue_mapping(skb);
	send_ring = priv->send_ring + queue_index;

	if (unlikely(skb->len > tx->mtu)) {
		ipoib_warn(priv, "packet len %d (> %d) too long to send, dropping\n",
			   skb->len, tx->mtu);
		++send_ring->stats.tx_dropped;
		++send_ring->stats.tx_errors;
		ipoib_cm_skb_too_long(dev, skb, tx->mtu - IPOIB_ENCAP_LEN);
		return;
	}
	if (skb_shinfo(skb)->nr_frags > usable_sge) {
		if (skb_linearize(skb) < 0) {
			ipoib_warn(priv, "skb could not be linearized\n");
			++send_ring->stats.tx_dropped;
			++send_ring->stats.tx_errors;
			dev_kfree_skb_any(skb);
			return;
		}
		/* Does skb_linearize return ok without reducing nr_frags? */
		if (skb_shinfo(skb)->nr_frags > usable_sge) {
			ipoib_warn(priv, "too many frags after skb linearize\n");
			++send_ring->stats.tx_dropped;
			++send_ring->stats.tx_errors;
			dev_kfree_skb_any(skb);
			return;
		}
	}
	ipoib_dbg_data(priv, "sending packet: head 0x%x length %d connection 0x%x\n",
		       tx->tx_head, skb->len, tx->qp->qp_num);

	/*
	 * We put the skb into the tx_ring _before_ we call post_send()
	 * because it's entirely possible that the completion handler will
	 * run before we execute anything after the post_send().  That
	 * means we have to make sure everything is properly recorded and
	 * our state is consistent before we call post_send().
	 */
	tx_req = &tx->tx_ring[tx->tx_head & (priv->sendq_size - 1)];
	tx_req->skb = skb;

	if (skb->len < ipoib_inline_thold && !skb_shinfo(skb)->nr_frags) {
		tx_req->mapping[0] = (u64)skb->data;
		send_ring->tx_wr.wr.send_flags |= IB_SEND_INLINE;
		tx_req->is_inline = 1;
	} else {
		if (unlikely(ipoib_dma_map_tx(priv->ca, tx_req))) {
			++send_ring->stats.tx_errors;
			dev_kfree_skb_any(skb);
			return;
		}
		tx_req->is_inline = 0;
		send_ring->tx_wr.wr.send_flags &= ~IB_SEND_INLINE;
	}

	if (atomic_read(&send_ring->tx_outstanding) == priv->sendq_size - 1) {
		ipoib_dbg(priv, "TX ring 0x%x full, stopping kernel net queue\n",
			  tx->qp->qp_num);
		netif_stop_subqueue(dev, queue_index);
	}

	skb_orphan(skb);
	skb_dst_drop(skb);

	if (__netif_subqueue_stopped(dev, queue_index))
		if (ib_req_notify_cq(send_ring->send_cq, IB_CQ_NEXT_COMP |
				     IB_CQ_REPORT_MISSED_EVENTS)) {
			ipoib_warn(priv, "IPoIB/CM/TSS:request notify on send CQ failed\n");
			napi_schedule(&priv->send_napi);
		}

	rc = post_send_rss(priv, tx, tx->tx_head & (priv->sendq_size - 1), tx_req,
			   send_ring);
	if (unlikely(rc)) {
		ipoib_warn(priv, "IPoIB/CM/TSS:post_send failed, error %d\n", rc);
		++dev->stats.tx_errors;
		if (!tx_req->is_inline)
			ipoib_dma_unmap_tx(priv, tx_req);
		dev_kfree_skb_any(skb);

		if (__netif_subqueue_stopped(dev, queue_index))
			netif_wake_subqueue(dev, queue_index);
	} else {
		netdev_get_tx_queue(dev, queue_index)->trans_start = jiffies;
		++tx->tx_head;
		atomic_inc(&send_ring->tx_outstanding);
	}
}

void ipoib_cm_handle_tx_wc_rss(struct net_device *dev, struct ib_wc *wc)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	struct ipoib_cm_tx *tx = wc->qp->qp_context;
	unsigned int wr_id = wc->wr_id & ~IPOIB_OP_CM;
	struct ipoib_tx_buf *tx_req;
	unsigned long flags;
	struct ipoib_send_ring *send_ring;
	u16 queue_index;

	ipoib_dbg_data(priv, "cm send completion: id %d, status: %d\n",
		       wr_id, wc->status);

	if (unlikely(wr_id >= priv->sendq_size)) {
		ipoib_warn(priv, "cm send completion event with wrid %d (> %d)\n",
			   wr_id, priv->sendq_size);
		return;
	}

	tx_req = &tx->tx_ring[wr_id];
	queue_index = skb_get_queue_mapping(tx_req->skb);
	send_ring = priv->send_ring + queue_index;

	/* Checking whether inline send was used - nothing to unmap */
	if (!tx_req->is_inline)
		ipoib_dma_unmap_tx(priv, tx_req);

	/* FIXME: is this right? Shouldn't we only increment on success? */
	++send_ring->stats.tx_packets;
	send_ring->stats.tx_bytes += tx_req->skb->len;

	dev_kfree_skb_any(tx_req->skb);
	tx_req->skb = NULL;

	netif_tx_lock(dev);

	++tx->tx_tail;
	atomic_dec(&send_ring->tx_outstanding);

	if (unlikely(__netif_subqueue_stopped(dev, queue_index) &&
	  	     (atomic_read(&send_ring->tx_outstanding) <= priv->sendq_size >> 1) &&
		     test_bit(IPOIB_FLAG_ADMIN_UP, &priv->flags)))
		netif_wake_subqueue(dev, queue_index);

	if (wc->status != IB_WC_SUCCESS &&
	    wc->status != IB_WC_WR_FLUSH_ERR) {
		struct ipoib_neigh *neigh;

		if (IB_WC_RNR_RETRY_EXC_ERR != wc->status)
			ipoib_warn(priv, "%s: failed cm send event (status=%d, wrid=%d vend_err %x)\n",
				   __func__, wc->status, wr_id, wc->vendor_err);
		else
			ipoib_dbg(priv, "%s: failed cm send event (status=%d, wrid=%d vend_err %x)\n",
				  __func__, wc->status, wr_id, wc->vendor_err);

		spin_lock_irqsave(&priv->lock, flags);
		neigh = tx->neigh;

		if (neigh) {
			neigh->cm = NULL;
			ipoib_neigh_free(neigh);

			tx->neigh = NULL;
		}

		if (test_and_clear_bit(IPOIB_FLAG_INITIALIZED, &tx->flags)) {
			list_move(&tx->list, &priv->cm.reap_list);
			queue_work(priv->wq, &priv->cm.reap_task);
		}

		clear_bit(IPOIB_FLAG_OPER_UP, &tx->flags);

		spin_unlock_irqrestore(&priv->lock, flags);
	}

	netif_tx_unlock(dev);
}

static struct ib_qp *ipoib_cm_create_tx_qp_rss(struct net_device *dev, struct ipoib_cm_tx *tx)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	struct ib_qp_init_attr attr = {
		.srq			= priv->cm.srq,
		.cap.max_send_wr	= priv->sendq_size,
		.cap.max_send_sge	= 1,
		.cap.max_inline_data    = IPOIB_MAX_INLINE_SIZE,
		.sq_sig_type		= IB_SIGNAL_ALL_WR,
		.qp_type		= IB_QPT_RC,
		.qp_context		= tx,
		.create_flags		= 0
	};
	struct ib_qp *tx_qp;

	attr.send_cq = attr.recv_cq = priv->send_ring[tx->neigh->index].send_cq;

	if (dev->features & NETIF_F_SG)
		attr.cap.max_send_sge =
			min_t(u32, priv->ca->attrs.max_send_sge, MAX_SKB_FRAGS + 1);

	tx_qp = ib_create_qp(priv->pd, &attr);
	tx->max_send_sge = attr.cap.max_send_sge;
	return tx_qp;
}

/* Rearm Recv and Send CQ */
static void ipoib_arm_cq_rss(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	struct ipoib_recv_ring *recv_ring;
	struct ipoib_send_ring *send_ring;
	int i;

	recv_ring = priv->recv_ring;
	for (i = 0; i < priv->num_rx_queues; i++) {
		ib_req_notify_cq(recv_ring->recv_cq, IB_CQ_NEXT_COMP);
		recv_ring++;
	}

	send_ring = priv->send_ring;
	for (i = 0; i < priv->num_tx_queues; i++) {
		ib_req_notify_cq(send_ring->send_cq, IB_CQ_NEXT_COMP);
		send_ring++;
	}
}

static void ipoib_cm_tx_destroy_rss(struct ipoib_cm_tx *p)
{
	struct ipoib_dev_priv *priv = ipoib_priv(p->dev);
	struct ipoib_tx_buf *tx_req;
	unsigned long begin;
	int num_tries = 0;

	ipoib_dbg(priv, "Destroy active connection 0x%x head 0x%x tail 0x%x\n",
		  p->qp ? p->qp->qp_num : 0, p->tx_head, p->tx_tail);

	/* arming cq*/
	ipoib_arm_cq_rss(p->dev);

	if (p->id)
		ib_destroy_cm_id(p->id);

	if (p->qp) {
		if (ib_modify_qp(p->qp, &ipoib_cm_err_attr, IB_QP_STATE))
			ipoib_warn(priv, "%s: Failed to modify QP to ERROR state\n",
				   __func__);
	}

	if (p->tx_ring) {
		/* Wait for all sends to complete */
		begin = jiffies;
		while ((int) p->tx_tail - (int) p->tx_head < 0) {
			if (time_after(jiffies, begin + 5 * HZ)) {
				ipoib_warn(priv, "timing out; %d sends not completed\n",
					   p->tx_head - p->tx_tail);
				/*
				 * check if we are in napi_disable state
				 * (in port/module down etc.), if so we need
				 * to force drain over the qp in order to get
				 * all the wc's.
				 */
				if (!test_bit(IPOIB_FLAG_INITIALIZED, &priv->flags) ||
				    netif_queue_stopped(p->dev))
					ipoib_drain_cq_rss(p->dev);

				/* arming cq's*/
				ipoib_arm_cq_rss(p->dev);

				begin = jiffies;
				num_tries++;
				if (num_tries == 5) {
					ipoib_warn(priv, "%s: %d not completed for QP: 0x%x force cleanup.\n",
						   __func__, p->tx_head - p->tx_tail, p->qp->qp_num);
					goto timeout;
				}
			}

			msleep(5);
		}
	}

timeout:

	while ((int) p->tx_tail - (int) p->tx_head < 0) {
		struct ipoib_send_ring *send_ring;
		u16 queue_index;
		tx_req = &p->tx_ring[p->tx_tail & (priv->sendq_size - 1)];
		queue_index = skb_get_queue_mapping(tx_req->skb);
		send_ring = priv->send_ring + queue_index;
		/* Checking whether inline was used - nothing to unmap */
		if (!tx_req->is_inline)
			ipoib_dma_unmap_tx(priv, tx_req);
		dev_kfree_skb_any(tx_req->skb);
		tx_req->skb = NULL;
		netif_tx_lock_bh(p->dev);
		++p->tx_tail;
		atomic_dec(&send_ring->tx_outstanding);
		if (atomic_read(&send_ring->tx_outstanding) <= (priv->sendq_size >> 1) &&
		    __netif_subqueue_stopped(p->dev, queue_index) &&
		    test_bit(IPOIB_FLAG_ADMIN_UP, &priv->flags))
			netif_wake_subqueue(p->dev, queue_index);
		netif_tx_unlock_bh(p->dev);
	}

	if (p->qp)
		ib_destroy_qp(p->qp);

	vfree(p->tx_ring);
	kfree(p);
}

void ipoib_cm_rss_init_fp(struct ipoib_dev_priv *priv)
{
	if (priv->max_tx_queues > 1) {
		priv->fp.ipoib_cm_create_rx_qp = ipoib_cm_create_rx_qp_rss;
		priv->fp.ipoib_cm_create_tx_qp = ipoib_cm_create_tx_qp_rss;
		priv->fp.ipoib_cm_nonsrq_init_rx = ipoib_cm_nonsrq_init_rx_rss;
		priv->fp.ipoib_cm_tx_destroy =ipoib_cm_tx_destroy_rss;
		priv->fp.ipoib_cm_send = ipoib_cm_send_rss;
	} else {
		priv->fp.ipoib_cm_create_rx_qp = ipoib_cm_create_rx_qp;
		priv->fp.ipoib_cm_create_tx_qp = ipoib_cm_create_tx_qp;
		priv->fp.ipoib_cm_nonsrq_init_rx = ipoib_cm_nonsrq_init_rx;
		priv->fp.ipoib_cm_tx_destroy =ipoib_cm_tx_destroy;
		priv->fp.ipoib_cm_send = ipoib_cm_send;
	}
}
