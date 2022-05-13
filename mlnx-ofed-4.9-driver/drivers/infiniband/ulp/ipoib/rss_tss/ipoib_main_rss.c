/*
 * Copyright (c) 2004 Topspin Communications.  All rights reserved.
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

int ipoib_set_mode_rss(struct net_device *dev, const char *buf)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	struct ipoib_send_ring *send_ring;
	int i;

	if ((test_bit(IPOIB_FLAG_ADMIN_CM, &priv->flags) &&
	     !strcmp(buf, "connected\n")) ||
	     (!test_bit(IPOIB_FLAG_ADMIN_CM, &priv->flags) &&
	     !strcmp(buf, "datagram\n"))) {
		return 0;
	}

	/* flush paths if we switch modes so that connections are restarted */
	if (!strcmp(buf, "connected\n")) {
		if (IPOIB_CM_SUPPORTED(dev->dev_addr)) {
			set_bit(IPOIB_FLAG_ADMIN_CM, &priv->flags);
			ipoib_warn(priv, "enabling connected mode "
				   "will cause multicast packet drops\n");
			netdev_update_features(dev);
			dev_set_mtu(dev, ipoib_cm_max_mtu(dev));
			rtnl_unlock();

			send_ring = priv->send_ring;
			for (i = 0; i < priv->num_tx_queues; i++) {
				send_ring->tx_wr.wr.send_flags &= ~IB_SEND_IP_CSUM;
				send_ring->tx_wr.wr.opcode = IB_WR_SEND;
				send_ring++;
			}

			ipoib_flush_paths(dev);
			return (!rtnl_trylock()) ? -EBUSY : 0;
		} else {
			ipoib_warn(priv, "Setting Connected Mode failed, "
				   "not supported by this device");
			return -EINVAL;
		}
	}

	if (!strcmp(buf, "datagram\n")) {
		clear_bit(IPOIB_FLAG_ADMIN_CM, &priv->flags);
		netdev_update_features(dev);
		dev_set_mtu(dev, min(priv->mcast_mtu, dev->mtu));
		rtnl_unlock();
		ipoib_flush_paths(dev);
		return (!rtnl_trylock()) ? -EBUSY : 0;
	}

	return -EINVAL;
}

static u16 ipoib_select_queue_sw_rss(struct net_device *dev, struct sk_buff *skb,
				     struct net_device *sb_dev)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	struct ipoib_pseudo_header *phdr;
	struct ipoib_header *header;

	phdr = (struct ipoib_pseudo_header *) skb->data;

	/* (BC/MC) use designated QDISC -> parent QP */
	if (unlikely(phdr->hwaddr[4] == 0xff))
		return priv->tss_qp_num;

	/* is CM in use */
	if (IPOIB_CM_SUPPORTED(phdr->hwaddr)) {
		if (ipoib_cm_admin_enabled(dev)) {
			/* use remote QP for hash, so we use the same ring */
			u32 *d32 = (u32 *)phdr->hwaddr;
			u32 hv = jhash_1word(*d32 & cpu_to_be32(0xFFFFFF), 0);
			return hv % priv->tss_qp_num;
		}
		else
			/* the ADMIN CM might be up until transmit, and
			 * we might transmit on CM QP not from it's
			 * designated ring */
			phdr->hwaddr[0] &= ~IPOIB_FLAGS_RC;
	}

	/* Did neighbour advertise TSS support */
	if (unlikely(!IPOIB_TSS_SUPPORTED(phdr->hwaddr)))
		return priv->tss_qp_num;

	/* We are after ipoib_hard_header so skb->data is O.K. */
	header = (struct ipoib_header *) skb->data;
	header->tss_qpn_mask_sz |= priv->tss_qpn_mask_sz;

	/* don't use special ring in TX */
	return netdev_pick_tx(dev, skb, NULL) % priv->tss_qp_num;
}

static void ipoib_timeout_rss(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	struct ipoib_send_ring *send_ring;
	u16 index;

	ipoib_warn(priv, "transmit timeout: latency %d msecs\n",
		   jiffies_to_msecs(jiffies - dev_trans_start(dev)));

	for (index = 0; index < priv->num_tx_queues; index++) {
		if (__netif_subqueue_stopped(dev, index)) {
			send_ring = priv->send_ring + index;
			ipoib_warn(priv, "queue (%d) stopped, tx_head %u, tx_tail %u\n",
				   index, send_ring->tx_head,
				   send_ring->tx_tail);
		}
	}

	schedule_work(&priv->tx_timeout_work);
}

static struct net_device_stats *ipoib_get_stats_rss(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	struct net_device_stats *stats = &dev->stats;
	struct net_device_stats local_stats;
	int i;

	if (!down_read_trylock(&priv->rings_rwsem))
		return stats;

	if (!priv->recv_ring || !priv->send_ring) {
		up_read(&priv->rings_rwsem);
		return stats;
	}

	memset(&local_stats, 0, sizeof(struct net_device_stats));

	for (i = 0; i < priv->num_rx_queues; i++) {
		struct ipoib_rx_ring_stats *rstats = &priv->recv_ring[i].stats;
		local_stats.rx_packets += rstats->rx_packets;
		local_stats.rx_bytes   += rstats->rx_bytes;
		local_stats.rx_errors  += rstats->rx_errors;
		local_stats.rx_dropped += rstats->rx_dropped;
	}

	for (i = 0; i < priv->num_tx_queues; i++) {
		struct ipoib_tx_ring_stats *tstats = &priv->send_ring[i].stats;
		local_stats.tx_packets += tstats->tx_packets;
		local_stats.tx_bytes   += tstats->tx_bytes;
		local_stats.tx_errors  += tstats->tx_errors;
		local_stats.tx_dropped += tstats->tx_dropped;
	}

	up_read(&priv->rings_rwsem);

	stats->rx_packets = local_stats.rx_packets;
	stats->rx_bytes   = local_stats.rx_bytes;
	stats->rx_errors  = local_stats.rx_errors;
	stats->rx_dropped = local_stats.rx_dropped;

	stats->tx_packets = local_stats.tx_packets;
	stats->tx_bytes   = local_stats.tx_bytes;
	stats->tx_errors  = local_stats.tx_errors;
	stats->tx_dropped = local_stats.tx_dropped;

	return stats;
}

static void ipoib_napi_add_rss(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	int i;

	for (i = 0; i < priv->num_rx_queues; i++)
		netif_napi_add(dev, &priv->recv_ring[i].napi,
			       ipoib_rx_poll_rss, IPOIB_NUM_WC);

	for (i = 0; i < priv->num_tx_queues; i++)
		netif_napi_add(dev, &priv->send_ring[i].napi,
			       ipoib_tx_poll_rss, MAX_SEND_CQE);
}

static void ipoib_napi_del_rss(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	int i;

	for (i = 0; i < priv->num_rx_queues; i++)
		netif_napi_del(&priv->recv_ring[i].napi);

	for (i = 0; i < priv->num_tx_queues; i++)
		netif_napi_del(&priv->send_ring[i].napi);
}

static struct ipoib_neigh *ipoib_neigh_ctor_rss(u8 *daddr,
						struct net_device *dev)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	struct ipoib_neigh *neigh;

	neigh = kzalloc(sizeof *neigh, GFP_ATOMIC);
	if (!neigh)
		return NULL;

	neigh->dev = dev;
	memcpy(&neigh->daddr, daddr, sizeof(neigh->daddr));
	skb_queue_head_init(&neigh->queue);
	INIT_LIST_HEAD(&neigh->list);
	ipoib_cm_set(neigh, NULL);
	/* one ref on behalf of the caller */
	atomic_set(&neigh->refcnt, 1);

	/*
	 * ipoib_neigh_alloc can be called from neigh_add_path without
	 * the protection of spin lock or from ipoib_mcast_send under
	 * spin lock protection. thus there is a need to use atomic
	 */
	if (priv->tss_qp_num > 0)
		neigh->index = atomic_inc_return(&priv->tx_ring_ind)
			       % priv->tss_qp_num;
	else
		neigh->index = 0;

	return neigh;
}

int ipoib_dev_init_default_rss(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	struct ib_device *ca = priv->ca;
	struct ipoib_send_ring *send_ring;
	struct ipoib_recv_ring *recv_ring;
	int i, rx_allocated, tx_allocated;
	unsigned long alloc_size;

	down_write(&priv->rings_rwsem);

	/* Multi queue initialization */
	priv->recv_ring = kzalloc(priv->num_rx_queues * sizeof(*recv_ring),
				  GFP_KERNEL);

	if (!priv->recv_ring) {
		pr_warn("%s: failed to allocate RECV ring (%d entries)\n",
			ca->name, priv->num_rx_queues);
		goto out;
	}

	alloc_size = priv->recvq_size * sizeof(*recv_ring->rx_ring);
	rx_allocated = 0;
	recv_ring = priv->recv_ring;
	for (i = 0; i < priv->num_rx_queues; i++) {
		recv_ring->rx_ring = kzalloc(alloc_size, GFP_KERNEL);
		if (!recv_ring->rx_ring) {
			pr_warn("%s: failed to allocate RX ring (%d entries)\n",
				priv->ca->name, priv->recvq_size);
			goto out_recv_ring_cleanup;
		}
		recv_ring->dev = dev;
		recv_ring->index = i;
		recv_ring++;
		rx_allocated++;
	}

	priv->send_ring = kzalloc(priv->num_tx_queues * sizeof(*send_ring),
				  GFP_KERNEL);
	if (!priv->send_ring) {
		pr_warn("%s: failed to allocate SEND ring (%d entries)\n",
			ca->name, priv->num_tx_queues);
		goto out_recv_ring_cleanup;
	}

	alloc_size = priv->sendq_size * sizeof(*send_ring->tx_ring);
	tx_allocated = 0;
	send_ring = priv->send_ring;
	for (i = 0; i < priv->num_tx_queues; i++) {
		send_ring->tx_ring = vzalloc(alloc_size);
		if (!send_ring->tx_ring) {
			pr_warn("%s: failed to allocate TX ring (%d entries)\n",
				ca->name, priv->sendq_size);
			goto out_send_ring_cleanup;
		}
		send_ring->dev = dev;
		send_ring->index = i;
		/* priv->tx_head, tx_tail & tx_outstanding are already 0 */
		atomic_set(&send_ring->tx_outstanding, 0);
		send_ring++;
		tx_allocated++;
	}

	ipoib_napi_add_rss(dev);

	if (ipoib_transport_dev_init_rss(dev, priv->ca)) {
		pr_warn("%s: ipoib_transport_dev_init_rss failed\n", priv->ca->name);
		goto out_napi_delete;
	}

	up_write(&priv->rings_rwsem);

	/*
	* advertise that we are willing to accept from TSS sender
	* note that this only indicates that this side is willing to accept
	* TSS frames, it doesn't implies that it will use TSS since for
	* transmission the peer should advertise TSS as well
	*/
	priv->dev->dev_addr[0] |= IPOIB_FLAGS_TSS;
	priv->dev->dev_addr[1] = (priv->qp->qp_num >> 16) & 0xff;
	priv->dev->dev_addr[2] = (priv->qp->qp_num >>  8) & 0xff;
	priv->dev->dev_addr[3] = (priv->qp->qp_num) & 0xff;

	return 0;

out_napi_delete:
	ipoib_napi_del_rss(dev);

out_send_ring_cleanup:
	for (i = 0; i < tx_allocated; i++)
		vfree(priv->send_ring[i].tx_ring);
	kfree(priv->send_ring);

out_recv_ring_cleanup:
	for (i = 0; i < rx_allocated; i++)
		kfree(priv->recv_ring[i].rx_ring);
	kfree(priv->recv_ring);

out:
	priv->send_ring = NULL;
	priv->recv_ring = NULL;
	up_write(&priv->rings_rwsem);
	return -ENOMEM;
}

static void ipoib_dev_uninit_rss(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);

	if (dev->reg_state != NETREG_UNINITIALIZED)
		ipoib_neigh_hash_uninit(dev);

	ipoib_ib_dev_cleanup(dev);

	/* no more works over the priv->wq */
	if (priv->wq) {
		flush_workqueue(priv->wq);
		destroy_workqueue(priv->wq);
		priv->wq = NULL;
	}
}

static void ipoib_ndo_uninit_rss(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);

	ASSERT_RTNL();

	/*
	 * ipoib_remove_one guarantees the children are removed before the
	 * parent, and that is the only place where a parent can be removed.
	 */
	WARN_ON(!list_empty(&priv->child_intfs));

	if (priv->parent) {
		struct ipoib_dev_priv *ppriv = ipoib_priv(priv->parent);

		down_write(&ppriv->vlan_rwsem);
		list_del(&priv->list);
		up_write(&ppriv->vlan_rwsem);
	}

	ipoib_dev_uninit_rss(dev);

	if (priv->parent)
		dev_put(priv->parent);
}

int ipoib_set_fp_rss(struct ipoib_dev_priv *priv, struct ib_device *hca)
{
	int ret;

	ret = ipoib_get_hca_features(priv, hca);
	if (ret)
		return ret;

	/* Initialize function pointers for RSS and non-RSS devices */
	ipoib_main_rss_init_fp(priv);
	ipoib_cm_rss_init_fp(priv);
	ipoib_ib_rss_init_fp(priv);

	return 0;
}

static int ipoib_get_hca_features(struct ipoib_dev_priv *priv, struct ib_device *hca)
{
	int num_cores, result;
	struct ib_exp_device_attr exp_device_attr;
	struct ib_udata uhw = {.outlen = 0, .inlen = 0};

	result = ib_exp_query_device(hca, &exp_device_attr, &uhw);
	if (result) {
		ipoib_warn(priv, "%s: ib_exp_query_device failed (ret = %d)\n",
			   hca->name, result);
		return result;
	}

	priv->hca_caps = hca->attrs.device_cap_flags;
	priv->hca_caps_exp = exp_device_attr.device_cap_flags2;

	num_cores = num_online_cpus();
	if (num_cores == 1 || !(priv->hca_caps_exp & IB_EXP_DEVICE_QPG)) {
		/* No additional QP, only one QP for RX & TX */
		priv->rss_qp_num = 0;
		priv->tss_qp_num = 0;
		priv->max_rx_queues = 1;
		priv->max_tx_queues = 1;
		priv->num_rx_queues = 1;
		priv->num_tx_queues = 1;
		return 0;
	}
	num_cores = roundup_pow_of_two(num_cores);
	if (priv->hca_caps_exp & IB_EXP_DEVICE_UD_RSS) {
		int max_rss_tbl_sz;
		max_rss_tbl_sz = exp_device_attr.max_rss_tbl_sz;
		max_rss_tbl_sz = min(IPOIB_MAX_RX_QUEUES, max_rss_tbl_sz);
		max_rss_tbl_sz = min(num_cores, max_rss_tbl_sz);
		max_rss_tbl_sz = rounddown_pow_of_two(max_rss_tbl_sz);
		priv->rss_qp_num    = max_rss_tbl_sz;
		priv->max_rx_queues = max_rss_tbl_sz;
	} else {
		/* No additional QP, only the parent QP for RX */
		priv->rss_qp_num = 0;
		priv->max_rx_queues = 1;
	}
	priv->num_rx_queues = priv->max_rx_queues;

	priv->tss_qp_num = min(IPOIB_MAX_TX_QUEUES, num_cores);
	/* TSS is not support by HW use the parent QP for ARP */
	priv->max_tx_queues = priv->tss_qp_num + 1;
	priv->num_tx_queues = priv->max_tx_queues;

	return 0;
}

void ipoib_dev_uninit_default_rss(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	int i;

	ipoib_transport_dev_cleanup_rss(dev);

	ipoib_napi_del_rss(dev);

	ipoib_cm_dev_cleanup(dev);

	down_write(&priv->rings_rwsem);

	for (i = 0; i < priv->num_tx_queues; i++)
		vfree(priv->send_ring[i].tx_ring);
	kfree(priv->send_ring);

	for (i = 0; i < priv->num_rx_queues; i++)
		kfree(priv->recv_ring[i].rx_ring);
	kfree(priv->recv_ring);

	priv->recv_ring = NULL;
	priv->send_ring = NULL;

	up_write(&priv->rings_rwsem);
}

int ipoib_reinit_rss(struct net_device *dev, int num_rx, int num_tx)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	int flags;
	int ret;

	flags = dev->flags;
	ipoib_stop(dev);
	if (!test_bit(IPOIB_FLAG_SUBINTERFACE, &priv->flags))
		ib_unregister_event_handler(&priv->event_handler);

	ipoib_dev_uninit_rss(dev);

	priv->num_rx_queues = num_rx;
	priv->num_tx_queues = num_tx;
	if (num_rx == 1)
		priv->rss_qp_num = 0;
	else
		priv->rss_qp_num = num_rx;

	priv->tss_qp_num = num_tx - 1;
	netif_set_real_num_tx_queues(dev, num_tx);
	netif_set_real_num_rx_queues(dev, num_rx);
	/*
	 * prevent ipoib_ib_dev_init call ipoib_ib_dev_open
	 * let ipoib_open do it
	 */
	dev->flags &= ~IFF_UP;

	ret = ipoib_dev_init(dev);
	if (ret) {
		pr_warn("%s: failed to reinitialize port %d (ret = %d)\n",
			priv->ca->name, priv->port, ret);
		return ret;
	}
	if (!test_bit(IPOIB_FLAG_SUBINTERFACE, &priv->flags)) {
		ib_register_event_handler(&priv->event_handler);
	}
	dev->flags = flags;
	/* if the device was up bring it up again */
	if (flags & IFF_UP) {
		ret = ipoib_open(dev);
		if (ret)
			pr_warn("%s: failed to reopen port %d (ret = %d)\n",
				priv->ca->name, priv->port, ret);
	}
	return ret;
}

static ssize_t get_rx_chan(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	struct ipoib_dev_priv *priv = ipoib_priv(to_net_dev(dev));

	return sprintf(buf, "%d\n", priv->num_rx_queues);
}

static ssize_t set_rx_chan(struct device *dev,
			   struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct net_device *ndev = to_net_dev(dev);
	struct ipoib_dev_priv *priv = ipoib_priv(ndev);
	int val, ret;

	ret = sscanf(buf, "%d", &val);
	if (ret != 1)
		return -EINVAL;
	if (val == 0 || val > priv->max_rx_queues) {
		ipoib_warn(priv,
			   "Trying to set invalid rx_channels value (%d), max_rx_queues (%d)\n",
			   val, priv->max_rx_queues);
		return -EINVAL;
	}
	/* Nothing to do ? */
	if (val == priv->num_rx_queues)
		return count;
	if (!is_power_of_2(val)) {
		ipoib_warn(priv,
			   "Trying to set invalid rx_channels value (%d), has to be power of 2\n",
			   val);
		return -EINVAL;
	}

	if (!rtnl_trylock())
		return restart_syscall();

	ret = ipoib_reinit_rss(ndev, val, priv->num_tx_queues);

	rtnl_unlock();

	if (ret)
		return ret;

	return count;
}

static DEVICE_ATTR(rx_channels, S_IWUSR | S_IRUGO, get_rx_chan, set_rx_chan);

static ssize_t get_rx_max_channel(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct ipoib_dev_priv *priv = ipoib_priv(to_net_dev(dev));

	return sprintf(buf, "%d\n", priv->max_rx_queues);
}

static DEVICE_ATTR(rx_max_channels, S_IRUGO, get_rx_max_channel, NULL);

static ssize_t get_tx_chan(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	struct ipoib_dev_priv *priv = ipoib_priv(to_net_dev(dev));

	return sprintf(buf, "%d\n", priv->num_tx_queues);
}

static ssize_t set_tx_chan(struct device *dev,
			   struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct net_device *ndev = to_net_dev(dev);
	struct ipoib_dev_priv *priv = ipoib_priv(ndev);
	int val, ret;

	ret = sscanf(buf, "%d", &val);
	if (ret != 1)
		return -EINVAL;
	if (val == 0 || val > priv->max_tx_queues) {
		ipoib_warn(priv,
			   "Trying to set invalid tx_channels value (%d), max_tx_queues (%d)\n",
			   val, priv->max_tx_queues);
		return -EINVAL;
	}
	/* Nothing to do ? */
	if (val == priv->num_tx_queues)
		return count;

	/* 1 is always O.K. */
	if (val > 1) {
		/*
		 * with SW TSS tx_count = 1 + 2 ^ N.
		 * 2 is not allowed, makes no sense,
		 * if want to disable TSS use 1.
		 */
		if (!is_power_of_2(val - 1) || val == 2) {
			ipoib_warn(priv,
				   "Trying to set invalid tx_channels value (%d), has to be (x^2 + 1)\n",
				   val);
			return -EINVAL;
		}
	}

	if (!rtnl_trylock())
		return restart_syscall();

	ret = ipoib_reinit_rss(ndev, priv->num_rx_queues, val);

	rtnl_unlock();

	if (ret)
		return ret;

	return count;
}

static DEVICE_ATTR(tx_channels, S_IWUSR | S_IRUGO, get_tx_chan, set_tx_chan);

static ssize_t get_tx_max_channel(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct ipoib_dev_priv *priv = ipoib_priv(to_net_dev(dev));

	return sprintf(buf, "%d\n", priv->max_tx_queues);
}

static DEVICE_ATTR(tx_max_channels, S_IRUGO, get_tx_max_channel, NULL);

int ipoib_set_rss_sysfs(struct ipoib_dev_priv *priv)
{
	int ret;

	ret = device_create_file(&priv->dev->dev, &dev_attr_tx_max_channels);
	if (ret)
		goto sysfs_failed;
	ret = device_create_file(&priv->dev->dev, &dev_attr_rx_max_channels);
	if (ret)
		goto sysfs_failed;
	ret = device_create_file(&priv->dev->dev, &dev_attr_tx_channels);
	if (ret)
		goto sysfs_failed;
	ret = device_create_file(&priv->dev->dev, &dev_attr_rx_channels);
	if (ret)
		goto sysfs_failed;

sysfs_failed:
	return ret;
}

static const struct net_device_ops ipoib_netdev_default_pf_rss = {
	.ndo_init		 = ipoib_dev_init_default_rss,
	.ndo_uninit		 = ipoib_dev_uninit_default_rss,
	.ndo_open		 = ipoib_ib_dev_open_default_rss,
	.ndo_stop		 = ipoib_ib_dev_stop_default_rss,
};

static const struct net_device_ops ipoib_netdev_ops_pf_sw_tss = {
	.ndo_init		 = ipoib_ndo_init,
	.ndo_uninit		 = ipoib_ndo_uninit_rss,
	.ndo_open		 = ipoib_open,
	.ndo_stop		 = ipoib_stop,
	.ndo_change_mtu		 = ipoib_change_mtu,
	.ndo_fix_features	 = ipoib_fix_features,
	.ndo_start_xmit		 = ipoib_start_xmit,
	.ndo_select_queue	 = ipoib_select_queue_sw_rss,
	.ndo_tx_timeout		 = ipoib_timeout_rss,
	.ndo_get_stats		 = ipoib_get_stats_rss,
	.ndo_set_rx_mode	 = ipoib_set_mcast_list,
	.ndo_get_iflink		 = ipoib_get_iflink,
	.ndo_set_vf_link_state	 = ipoib_set_vf_link_state,
	.ndo_get_vf_config	 = ipoib_get_vf_config,
	.ndo_get_vf_stats	 = ipoib_get_vf_stats,
	.ndo_set_vf_guid	 = ipoib_set_vf_guid,
	.ndo_set_mac_address	 = ipoib_set_mac,
};

static const struct net_device_ops ipoib_netdev_ops_vf_sw_tss = {
	.ndo_init		 = ipoib_ndo_init,
	.ndo_uninit		 = ipoib_ndo_uninit_rss,
	.ndo_open		 = ipoib_open,
	.ndo_stop		 = ipoib_stop,
	.ndo_change_mtu		 = ipoib_change_mtu,
	.ndo_fix_features	 = ipoib_fix_features,
	.ndo_start_xmit	 	 = ipoib_start_xmit,
	.ndo_select_queue 	 = ipoib_select_queue_sw_rss,
	.ndo_tx_timeout		 = ipoib_timeout_rss,
	.ndo_get_stats		 = ipoib_get_stats_rss,
	.ndo_set_rx_mode	 = ipoib_set_mcast_list,
	.ndo_get_iflink		 = ipoib_get_iflink,
};

const struct net_device_ops *ipoib_get_netdev_ops(struct ipoib_dev_priv *priv)
{
	if (priv->hca_caps & IB_DEVICE_VIRTUAL_FUNCTION)
		return priv->max_tx_queues > 1 ?
			&ipoib_netdev_ops_vf_sw_tss : &ipoib_netdev_ops_vf;
	else
		return priv->max_tx_queues > 1 ?
			&ipoib_netdev_ops_pf_sw_tss : &ipoib_netdev_ops_pf;
}

const struct net_device_ops *ipoib_get_rn_ops(struct ipoib_dev_priv *priv)
{
	return priv->max_tx_queues > 1 ?
		&ipoib_netdev_default_pf_rss : &ipoib_netdev_default_pf;
}

void ipoib_main_rss_init_fp(struct ipoib_dev_priv *priv)
{
	if (priv->max_tx_queues > 1) {
		priv->fp.ipoib_set_mode = ipoib_set_mode_rss;
		priv->fp.ipoib_neigh_ctor = ipoib_neigh_ctor_rss;
	} else {
		priv->fp.ipoib_set_mode = ipoib_set_mode;
		priv->fp.ipoib_neigh_ctor = ipoib_neigh_ctor;
	}
}
