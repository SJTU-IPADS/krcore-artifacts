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

#include <linux/skbuff.h>
#include <linux/rtnetlink.h>
#include <linux/ip.h>
#include <linux/in.h>
#include <linux/igmp.h>
#include <linux/inetdevice.h>
#include <linux/delay.h>
#include <linux/completion.h>

#include <net/dst.h>

#include "ipoib.h"

#ifdef CONFIG_INFINIBAND_IPOIB_DEBUG
static int mcast_debug_level;

module_param(mcast_debug_level, int, 0644);
MODULE_PARM_DESC(mcast_debug_level,
		 "Enable multicast debug tracing if > 0");
#endif

static DEFINE_MUTEX(mcast_mutex);

struct ipoib_mcast_iter {
	struct net_device *dev;
	union ib_gid       mgid;
	unsigned long      created;
	unsigned int       queuelen;
	unsigned int       complete;
	unsigned int       send_only;
};

static void ipoib_mcast_free(struct ipoib_mcast *mcast)
{
	struct net_device *dev = mcast->dev;
	int tx_dropped = 0;
	struct ipoib_dev_priv *priv = netdev_priv(dev);

	ipoib_dbg_mcast(netdev_priv(dev),
			"deleting multicast group " IPOIB_GID_FMT "\n",
			IPOIB_GID_ARG(mcast->mcmember.mgid));


	/* remove all neigh connected to this mcast */
	ipoib_del_neighs_by_gid(dev, mcast->mcmember.mgid.raw);

	if (mcast->ah)
		ipoib_put_ah(mcast->ah);

	while (!skb_queue_empty(&mcast->pkt_queue)) {
		++tx_dropped;
		dev_kfree_skb_any(skb_dequeue(&mcast->pkt_queue));
	}

	netif_tx_lock_bh(dev);
	priv->stats.tx_dropped += tx_dropped;
	netif_tx_unlock_bh(dev);

	kfree(mcast);
}

static struct ipoib_mcast *ipoib_mcast_alloc(struct net_device *dev,
					     int can_sleep)
{
	struct ipoib_mcast *mcast;

	mcast = kzalloc(sizeof *mcast, can_sleep ? GFP_KERNEL : GFP_ATOMIC);
	if (!mcast)
		return NULL;

	mcast->dev = dev;
	mcast->created = jiffies;
	mcast->used = jiffies;
	mcast->backoff = 1;

	INIT_LIST_HEAD(&mcast->list);
	INIT_LIST_HEAD(&mcast->neigh_list);
	skb_queue_head_init(&mcast->pkt_queue);

	return mcast;
}

static struct ipoib_mcast *__ipoib_mcast_find(struct net_device *dev, void *mgid)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	struct rb_node *n = priv->multicast_tree.rb_node;

	while (n) {
		struct ipoib_mcast *mcast;
		int ret;

		mcast = rb_entry(n, struct ipoib_mcast, rb_node);

		ret = memcmp(mgid, mcast->mcmember.mgid.raw,
			     sizeof (union ib_gid));
		if (ret < 0)
			n = n->rb_left;
		else if (ret > 0)
			n = n->rb_right;
		else
			return mcast;
	}

	return NULL;
}

static int __ipoib_mcast_add(struct net_device *dev, struct ipoib_mcast *mcast)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	struct rb_node **n = &priv->multicast_tree.rb_node, *pn = NULL;

	while (*n) {
		struct ipoib_mcast *tmcast;
		int ret;

		pn = *n;
		tmcast = rb_entry(pn, struct ipoib_mcast, rb_node);

		ret = memcmp(mcast->mcmember.mgid.raw, tmcast->mcmember.mgid.raw,
			     sizeof (union ib_gid));
		if (ret < 0)
			n = &pn->rb_left;
		else if (ret > 0)
			n = &pn->rb_right;
		else
			return -EEXIST;
	}

	rb_link_node(&mcast->rb_node, pn, n);
	rb_insert_color(&mcast->rb_node, &priv->multicast_tree);

	return 0;
}

static int ipoib_mcast_join_finish(struct ipoib_mcast *mcast,
				   struct ib_sa_mcmember_rec *mcmember)
{
	struct net_device *dev = mcast->dev;
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	struct ipoib_ah *ah;
	int ret;
	int set_qkey = 0;

	mcast->mcmember = *mcmember;

	/* Set the cached Q_Key before we attach if it's the broadcast group */
	if (!memcmp(mcast->mcmember.mgid.raw, priv->dev->broadcast + 4,
		    sizeof (union ib_gid))) {
		spin_lock_irq(&priv->lock);
		if (!priv->broadcast) {
			spin_unlock_irq(&priv->lock);
			return -EAGAIN;
		}
		priv->qkey = be32_to_cpu(priv->broadcast->mcmember.qkey);
		spin_unlock_irq(&priv->lock);
		priv->tx_wr.wr.ud.remote_qkey = priv->qkey;
		set_qkey = 1;
	}

	if (!test_bit(IPOIB_MCAST_FLAG_SENDONLY, &mcast->flags)) {
		if (test_and_set_bit(IPOIB_MCAST_FLAG_ATTACHED, &mcast->flags)) {
			ipoib_warn(priv, "multicast group " IPOIB_GID_FMT
				   " already attached\n",
				   IPOIB_GID_ARG(mcast->mcmember.mgid));

			return 0;
		}

		ret = ipoib_mcast_attach(dev, be16_to_cpu(mcast->mcmember.mlid),
					 &mcast->mcmember.mgid, set_qkey);
		if (ret < 0) {
			ipoib_warn(priv, "couldn't attach QP to multicast group "
				   IPOIB_GID_FMT "\n",
				   IPOIB_GID_ARG(mcast->mcmember.mgid));

			clear_bit(IPOIB_MCAST_FLAG_ATTACHED, &mcast->flags);
			return ret;
		}
	}

	{
		struct ib_ah_attr av = {
			.dlid	       = be16_to_cpu(mcast->mcmember.mlid),
			.port_num      = priv->port,
			.sl	       = mcast->mcmember.sl,
			.ah_flags      = IB_AH_GRH,
			.static_rate   = mcast->mcmember.rate,
			.grh	       = {
				.flow_label    = be32_to_cpu(mcast->mcmember.flow_label),
				.hop_limit     = mcast->mcmember.hop_limit,
				.sgid_index    = 0,
				.traffic_class = mcast->mcmember.traffic_class
			}
		};
		av.grh.dgid = mcast->mcmember.mgid;

		ah = ipoib_create_ah(dev, priv->pd, &av);
		if (!ah) {
			ipoib_warn(priv, "ib_address_create failed\n");
		} else {
			spin_lock_irq(&priv->lock);
			mcast->ah = ah;
			spin_unlock_irq(&priv->lock);

			ipoib_dbg_mcast(priv, "MGID " IPOIB_GID_FMT
					" AV %p, LID 0x%04x, SL %d\n",
					IPOIB_GID_ARG(mcast->mcmember.mgid),
					mcast->ah->ah,
					be16_to_cpu(mcast->mcmember.mlid),
					mcast->mcmember.sl);
		}
	}

	/* actually send any queued packets */
	netif_tx_lock_bh(dev);
	while (!skb_queue_empty(&mcast->pkt_queue)) {
		struct sk_buff *skb = skb_dequeue(&mcast->pkt_queue);
		netif_tx_unlock_bh(dev);

		skb->dev = dev;
		ret = dev_queue_xmit(skb);
		if (ret)
			ipoib_warn(priv, "%s: dev_queue_xmit failed to "
					 "requeue packet(ret: %d)\n", __func__, ret);
		netif_tx_lock_bh(dev);
	}
	netif_tx_unlock_bh(dev);

	return 0;
}

static int
ipoib_mcast_sendonly_join_complete(int status,
				   struct ib_sa_multicast *multicast)
{
	struct ipoib_mcast *mcast = multicast->context;
	struct net_device *dev = mcast->dev;
	struct ipoib_dev_priv *priv = netdev_priv(dev);

	/* We trap for port events ourselves. */
	if (status == -ENETRESET)
		return 0;

	if (!status)
		status = ipoib_mcast_join_finish(mcast, &multicast->rec);

	if (status) {
		if (mcast->logcount++ < 20)
			ipoib_dbg_mcast(netdev_priv(dev), "multicast join failed for "
					IPOIB_GID_FMT ", status %d\n",
					IPOIB_GID_ARG(mcast->mcmember.mgid), status);

		/* Flush out any queued packets */
		netif_tx_lock_bh(dev);
		while (!skb_queue_empty(&mcast->pkt_queue)) {
			++priv->stats.tx_dropped;
			dev_kfree_skb_any(skb_dequeue(&mcast->pkt_queue));
		}
		netif_tx_unlock_bh(dev);

		/* Clear the busy flag so we try again */
		status = test_and_clear_bit(IPOIB_MCAST_FLAG_BUSY,
					    &mcast->flags);
	}
	return status;
}

static int ipoib_mcast_sendonly_join(struct ipoib_mcast *mcast)
{
	struct net_device *dev = mcast->dev;
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	struct ib_sa_mcmember_rec rec = {
#if 0				/* Some SMs don't support send-only yet */
		.join_state = 4
#else
		.join_state = 1
#endif
	};
	ib_sa_comp_mask comp_mask;
	int ret = 0;

	if (!test_bit(IPOIB_FLAG_OPER_UP, &priv->flags)) {
		ipoib_dbg_mcast(priv, "device shutting down, no multicast joins\n");
		return -ENODEV;
	}

	if (test_and_set_bit(IPOIB_MCAST_FLAG_BUSY, &mcast->flags)) {
		ipoib_dbg_mcast(priv, "multicast entry busy, skipping\n");
		return -EBUSY;
	}

	rec.mgid     = mcast->mcmember.mgid;
	rec.port_gid = priv->local_gid;
	rec.pkey     = cpu_to_be16(priv->pkey);

	comp_mask =
		IB_SA_MCMEMBER_REC_MGID		|
		IB_SA_MCMEMBER_REC_PORT_GID	|
		IB_SA_MCMEMBER_REC_PKEY		|
		IB_SA_MCMEMBER_REC_JOIN_STATE;

	if (priv->broadcast) {
		comp_mask |=
			IB_SA_MCMEMBER_REC_QKEY			|
			IB_SA_MCMEMBER_REC_MTU_SELECTOR		|
			IB_SA_MCMEMBER_REC_MTU			|
			IB_SA_MCMEMBER_REC_TRAFFIC_CLASS	|
			IB_SA_MCMEMBER_REC_RATE_SELECTOR	|
			IB_SA_MCMEMBER_REC_RATE			|
			IB_SA_MCMEMBER_REC_SL			|
			IB_SA_MCMEMBER_REC_FLOW_LABEL		|
			IB_SA_MCMEMBER_REC_HOP_LIMIT;

		rec.qkey	  = priv->broadcast->mcmember.qkey;
		rec.mtu_selector  = IB_SA_EQ;
		rec.mtu		  = priv->broadcast->mcmember.mtu;
		rec.traffic_class = priv->broadcast->mcmember.traffic_class;
		rec.rate_selector = IB_SA_EQ;
		rec.rate	  = priv->broadcast->mcmember.rate;
		rec.sl		  = priv->broadcast->mcmember.sl;
		rec.flow_label	  = priv->broadcast->mcmember.flow_label;
		rec.hop_limit	  = priv->broadcast->mcmember.hop_limit;
	}

	mcast->mc = ib_sa_join_multicast(&ipoib_sa_client, priv->ca,
					 priv->port, &rec,
					 comp_mask,
					 GFP_ATOMIC,
					 ipoib_mcast_sendonly_join_complete,
					 mcast);
	if (IS_ERR(mcast->mc)) {
		ret = PTR_ERR(mcast->mc);
		clear_bit(IPOIB_MCAST_FLAG_BUSY, &mcast->flags);
		ipoib_warn(priv, "ib_sa_join_multicast failed (ret = %d)\n",
			   ret);
	} else {
		ipoib_dbg_mcast(priv, "no multicast record for " IPOIB_GID_FMT
				", starting join\n",
				IPOIB_GID_ARG(mcast->mcmember.mgid));
	}

	return ret;
}

void ipoib_mcast_carrier_on_task(struct work_struct *work)
{
	struct ipoib_dev_priv *priv = container_of(work, struct ipoib_dev_priv,
						   carrier_on_task);
	struct ib_port_attr attr;

	mutex_lock(&priv->state_lock);
	if (!test_bit(IPOIB_FLAG_ADMIN_UP, &priv->flags)) {
		ipoib_dbg(priv, "Keeping carrier off - IPOIB_FLAG_ADMIN_UP not set.\n");
		goto out;
	}

	if (ib_query_port(priv->ca, priv->port, &attr) ||
	    attr.state != IB_PORT_ACTIVE) {
		ipoib_dbg(priv, "Keeping carrier off until IB port is active\n");
		goto out;
	}

	netif_carrier_on(priv->dev);

        /* enable auto-moderation */
        if (priv->ethtool.use_adaptive_rx_coalesce &&
            test_bit(IPOIB_FLAG_AUTO_MODER, &priv->flags))
                queue_delayed_work(ipoib_auto_moder_workqueue,
                                   &priv->adaptive_moder_task,
                                   ADAPT_MODERATION_DELAY);

out:
	mutex_unlock(&priv->state_lock);


}

static int ipoib_mcast_join_complete(int status,
				     struct ib_sa_multicast *multicast)
{
	struct ipoib_mcast *mcast = multicast->context;
	struct net_device *dev = mcast->dev;
	struct ipoib_dev_priv *priv = netdev_priv(dev);

	ipoib_dbg_mcast(priv, "join completion for " IPOIB_GID_FMT
			" (status %d)\n",
			IPOIB_GID_ARG(mcast->mcmember.mgid), status);

	/* We trap for port events ourselves. */
	if (status == -ENETRESET){
		status = 0;
		goto out;
	}

	if (!status)
		status = ipoib_mcast_join_finish(mcast, &multicast->rec);

	if (!status) {
		mcast->backoff = 1;
		mutex_lock(&mcast_mutex);
		if (test_bit(IPOIB_MCAST_RUN, &priv->flags))
			queue_delayed_work(ipoib_workqueue,
					   &priv->mcast_join_task, 0);
		mutex_unlock(&mcast_mutex);

		/*
		 * Defer carrier on work to ipoib_workqueue to avoid a
		 * deadlock on rtnl_lock here.
		 */
		if (mcast == priv->broadcast)
			queue_work(ipoib_workqueue, &priv->carrier_on_task);

		status = 0;
		goto out;
	}

	if (mcast->logcount++ < 20) {
		if (status == -ETIMEDOUT || status == -EAGAIN) {
			ipoib_dbg_mcast(priv, "multicast join failed for " IPOIB_GID_FMT
					", status %d\n",
					IPOIB_GID_ARG(mcast->mcmember.mgid),
					status);
		} else {
			ipoib_warn(priv, "multicast join failed for "
				   IPOIB_GID_FMT ", status %d\n",
				   IPOIB_GID_ARG(mcast->mcmember.mgid),
				   status);
		}
	}

	mcast->backoff *= 2;
	if (mcast->backoff > IPOIB_MAX_BACKOFF_SECONDS)
		mcast->backoff = IPOIB_MAX_BACKOFF_SECONDS;

	/* Clear the busy flag so we try again */
	status = test_and_clear_bit(IPOIB_MCAST_FLAG_BUSY, &mcast->flags);

	mutex_lock(&mcast_mutex);
	spin_lock_irq(&priv->lock);
	if (test_bit(IPOIB_MCAST_RUN, &priv->flags))
		queue_delayed_work(ipoib_workqueue, &priv->mcast_join_task,
				   mcast->backoff * HZ);
	spin_unlock_irq(&priv->lock);
	mutex_unlock(&mcast_mutex);
out:
	complete(&mcast->done);
	return status;
}

static void ipoib_mcast_join(struct net_device *dev, struct ipoib_mcast *mcast,
			     int create)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	struct ib_sa_mcmember_rec rec = {
		.join_state = 1
	};
	ib_sa_comp_mask comp_mask;
	int ret = 0;

	ipoib_dbg_mcast(priv, "joining MGID " IPOIB_GID_FMT "\n",
			IPOIB_GID_ARG(mcast->mcmember.mgid));

	rec.mgid     = mcast->mcmember.mgid;
	rec.port_gid = priv->local_gid;
	rec.pkey     = cpu_to_be16(priv->pkey);

	comp_mask =
		IB_SA_MCMEMBER_REC_MGID		|
		IB_SA_MCMEMBER_REC_PORT_GID	|
		IB_SA_MCMEMBER_REC_PKEY		|
		IB_SA_MCMEMBER_REC_JOIN_STATE;

	if (create) {
		comp_mask |=
			IB_SA_MCMEMBER_REC_QKEY			|
			IB_SA_MCMEMBER_REC_MTU_SELECTOR		|
			IB_SA_MCMEMBER_REC_MTU			|
			IB_SA_MCMEMBER_REC_TRAFFIC_CLASS	|
			IB_SA_MCMEMBER_REC_RATE_SELECTOR	|
			IB_SA_MCMEMBER_REC_RATE			|
			IB_SA_MCMEMBER_REC_SL			|
			IB_SA_MCMEMBER_REC_FLOW_LABEL		|
			IB_SA_MCMEMBER_REC_HOP_LIMIT;

		rec.qkey	  = priv->broadcast->mcmember.qkey;
		rec.mtu_selector  = IB_SA_EQ;
		rec.mtu		  = priv->broadcast->mcmember.mtu;
		rec.traffic_class = priv->broadcast->mcmember.traffic_class;
		rec.rate_selector = IB_SA_EQ;
		rec.rate	  = priv->broadcast->mcmember.rate;
		rec.sl		  = priv->broadcast->mcmember.sl;
		rec.flow_label	  = priv->broadcast->mcmember.flow_label;
		rec.hop_limit	  = priv->broadcast->mcmember.hop_limit;
	}

	set_bit(IPOIB_MCAST_FLAG_BUSY, &mcast->flags);
	init_completion(&mcast->done);
	set_bit(IPOIB_MCAST_JOIN_STARTED, &mcast->flags);

	mcast->mc = ib_sa_join_multicast(&ipoib_sa_client, priv->ca, priv->port,
					 &rec, comp_mask, GFP_KERNEL,
					 ipoib_mcast_join_complete, mcast);
	if (IS_ERR(mcast->mc)) {
		clear_bit(IPOIB_MCAST_FLAG_BUSY, &mcast->flags);
		complete(&mcast->done);
		ret = PTR_ERR(mcast->mc);
		ipoib_warn(priv, "ib_sa_join_multicast failed, status %d\n", ret);

		mcast->backoff *= 2;
		if (mcast->backoff > IPOIB_MAX_BACKOFF_SECONDS)
			mcast->backoff = IPOIB_MAX_BACKOFF_SECONDS;

		mutex_lock(&mcast_mutex);
		if (test_bit(IPOIB_MCAST_RUN, &priv->flags))
			queue_delayed_work(ipoib_workqueue,
					   &priv->mcast_join_task,
					   mcast->backoff * HZ);
		mutex_unlock(&mcast_mutex);
	}
}

void ipoib_mcast_join_task(struct work_struct *work)
{
	struct ipoib_dev_priv *priv =
		container_of(work, struct ipoib_dev_priv, mcast_join_task.work);
	struct net_device *dev = priv->dev;
	struct ib_port_attr attr;

	if (!test_bit(IPOIB_MCAST_RUN, &priv->flags))
		return;

	if (ib_query_port(priv->ca, priv->port, &attr) ||
	    attr.state != IB_PORT_ACTIVE) {
		ipoib_dbg(priv, "%s: port state is not ACTIVE (state = %d) suspend task.\n",
			  __func__, attr.state);
		return;
	}

	if (ib_query_gid(priv->ca, priv->port, 0, &priv->local_gid, NULL))
		ipoib_warn(priv, "ib_query_gid() failed\n");
	else
		memcpy(priv->dev->dev_addr + 4, priv->local_gid.raw, sizeof (union ib_gid));

	{
		struct ib_port_attr attr;

		if (!ib_query_port(priv->ca, priv->port, &attr))
			priv->local_lid = attr.lid;
		else
			ipoib_warn(priv, "ib_query_port failed\n");
	}

	if (!priv->broadcast) {
		struct ipoib_mcast *broadcast;

		if (!test_bit(IPOIB_FLAG_ADMIN_UP, &priv->flags))
			return;

		broadcast = ipoib_mcast_alloc(dev, 1);
		if (!broadcast) {
			ipoib_warn(priv, "failed to allocate broadcast group\n");
			mutex_lock(&mcast_mutex);
			if (test_bit(IPOIB_MCAST_RUN, &priv->flags))
				queue_delayed_work(ipoib_workqueue,
						   &priv->mcast_join_task, HZ);
			mutex_unlock(&mcast_mutex);
			return;
		}

		spin_lock_irq(&priv->lock);
		memcpy(broadcast->mcmember.mgid.raw, priv->dev->broadcast + 4,
		       sizeof (union ib_gid));
		priv->broadcast = broadcast;

		__ipoib_mcast_add(dev, priv->broadcast);
		spin_unlock_irq(&priv->lock);
	}

	if (priv->broadcast &&
	    !test_bit(IPOIB_MCAST_FLAG_ATTACHED, &priv->broadcast->flags)) {
		if (priv->broadcast &&
		    !test_bit(IPOIB_MCAST_FLAG_BUSY, &priv->broadcast->flags))
			ipoib_mcast_join(dev, priv->broadcast, 0);
		return;
	}

	while (1) {
		struct ipoib_mcast *mcast = NULL;

		spin_lock_irq(&priv->lock);
		list_for_each_entry(mcast, &priv->multicast_list, list) {
			if (!test_bit(IPOIB_MCAST_FLAG_SENDONLY, &mcast->flags)
			    && !test_bit(IPOIB_MCAST_FLAG_BUSY, &mcast->flags)
			    && !test_bit(IPOIB_MCAST_FLAG_ATTACHED, &mcast->flags)) {
				/* Found the next unjoined group */
				break;
			}
		}
		spin_unlock_irq(&priv->lock);

		if (&mcast->list == &priv->multicast_list) {
			/* All done */
			break;
		}

		ipoib_mcast_join(dev, mcast, 1);
		return;
	}

	spin_lock_irq(&priv->lock);
	if (priv->broadcast)
		priv->mcast_mtu = IPOIB_UD_MTU(ib_mtu_enum_to_int(priv->broadcast->mcmember.mtu));
	else
		priv->mcast_mtu = priv->admin_mtu;
	spin_unlock_irq(&priv->lock);

	if (!ipoib_cm_admin_enabled(dev)) {
		rtnl_lock();
		dev_set_mtu(dev, min(priv->mcast_mtu, priv->admin_mtu));
		rtnl_unlock();
	}

	ipoib_dbg_mcast(priv, "successfully joined all multicast groups\n");

	clear_bit(IPOIB_MCAST_RUN, &priv->flags);
}

int ipoib_mcast_start_thread(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);

	ipoib_dbg_mcast(priv, "starting multicast thread\n");

	mutex_lock(&mcast_mutex);
	if (!test_and_set_bit(IPOIB_MCAST_RUN, &priv->flags))
		queue_delayed_work(ipoib_workqueue, &priv->mcast_join_task, 0);
	if (!test_and_set_bit(IPOIB_MCAST_RUN_GC, &priv->flags))
		queue_delayed_work(ipoib_workqueue, &priv->mcast_leave_task, 0);
	mutex_unlock(&mcast_mutex);

	return 0;
}

int ipoib_mcast_stop_thread(struct net_device *dev, int flush)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);

	ipoib_dbg_mcast(priv, "stopping multicast thread\n");

	mutex_lock(&mcast_mutex);
	clear_bit(IPOIB_MCAST_RUN, &priv->flags);
	clear_bit(IPOIB_MCAST_RUN_GC, &priv->flags);
	cancel_delayed_work(&priv->mcast_join_task);
	cancel_delayed_work(&priv->mcast_leave_task);
	mutex_unlock(&mcast_mutex);

	if (flush)
		flush_workqueue(ipoib_workqueue);

	return 0;
}

static int ipoib_mcast_leave(struct net_device *dev, struct ipoib_mcast *mcast)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	int ret = 0;

	if (test_and_clear_bit(IPOIB_MCAST_FLAG_BUSY, &mcast->flags))
		ib_sa_free_multicast(mcast->mc);

	if (test_and_clear_bit(IPOIB_MCAST_FLAG_ATTACHED, &mcast->flags)) {
		ipoib_dbg_mcast(priv, "leaving MGID " IPOIB_GID_FMT "\n",
				IPOIB_GID_ARG(mcast->mcmember.mgid));

		/* Remove ourselves from the multicast group */
		ret = ib_detach_mcast(priv->qp, &mcast->mcmember.mgid,
				      be16_to_cpu(mcast->mcmember.mlid));
		if (ret)
			ipoib_warn(priv, "ib_detach_mcast failed (result = %d)\n", ret);
	}

	return 0;
}

void ipoib_mcast_send(struct net_device *dev, u8 *daddr, struct sk_buff *skb)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	struct ipoib_mcast *mcast;
	unsigned long flags;
        void *mgid = daddr + 4;

	spin_lock_irqsave(&priv->lock, flags);

	if (!test_bit(IPOIB_FLAG_OPER_UP, &priv->flags)		||
	    !priv->broadcast					||
	    !test_bit(IPOIB_MCAST_FLAG_ATTACHED, &priv->broadcast->flags)) {
		++priv->stats.tx_dropped;
		dev_kfree_skb_any(skb);
		goto unlock;
	}

	mcast = __ipoib_mcast_find(dev, mgid);
	if (!mcast) {
		/* Let's create a new send only group now */
		ipoib_dbg_mcast(priv, "setting up send only multicast group for "
				IPOIB_GID_FMT "\n", IPOIB_GID_RAW_ARG(mgid));

		mcast = ipoib_mcast_alloc(dev, 0);
		if (!mcast) {
			ipoib_warn(priv, "unable to allocate memory for "
				   "multicast structure\n");
			++priv->stats.tx_dropped;
			dev_kfree_skb_any(skb);
			goto out;
		}

		set_bit(IPOIB_MCAST_FLAG_SENDONLY, &mcast->flags);
		memcpy(mcast->mcmember.mgid.raw, mgid, sizeof (union ib_gid));

		/*
		 * Check if user-space already attached to that mcg.
		 * if yes, marks the mcg as user-space-attached, and when
		 * the kernel will call ipoib to add it as full memeber
		 * in set_mc_list callback, ipoib ignores that mcg.
		 */
		if (test_bit(IPOIB_FLAG_UMCAST, &priv->flags)) {
			union ib_gid sa_mgid;
			struct ib_sa_mcmember_rec rec;

			memcpy(sa_mgid.raw, mgid, sizeof sa_mgid);
			if (!ib_sa_get_mcmember_rec(priv->ca, priv->port, &sa_mgid, &rec)) {
				ipoib_dbg_mcast(priv, "Found send-only that already attached"
						      " by user-space mgid "IPOIB_GID_FMT"\n" ,IPOIB_GID_ARG(sa_mgid));
				set_bit(IPOIB_MCAST_UMCAST_ATTACHED, &mcast->flags);
			}
		}

		__ipoib_mcast_add(dev, mcast);
		list_add_tail(&mcast->list, &priv->multicast_list);
	}

	if (!mcast->ah) {
		if (skb_queue_len(&mcast->pkt_queue) < IPOIB_MAX_MCAST_QUEUE)
			skb_queue_tail(&mcast->pkt_queue, skb);
		else {
			++priv->stats.tx_dropped;
			dev_kfree_skb_any(skb);
		}

		if (test_bit(IPOIB_MCAST_FLAG_BUSY, &mcast->flags))
			ipoib_dbg_mcast(priv, "no address vector, "
					"but multicast join already started\n");
		else if (test_bit(IPOIB_MCAST_FLAG_SENDONLY, &mcast->flags))
			ipoib_mcast_sendonly_join(mcast);

		/*
		 * If lookup completes between here and out:, don't
		 * want to send packet twice.
		 */
		mcast = NULL;
	}

out:
	if (mcast && mcast->ah) {
                struct ipoib_neigh *neigh;

                spin_unlock_irqrestore(&priv->lock, flags);
                neigh = ipoib_neigh_get(dev, daddr);
                spin_lock_irqsave(&priv->lock, flags);
                if (!neigh) {
                        neigh = ipoib_neigh_alloc(daddr, dev);
                        if (neigh) {
                                kref_get(&mcast->ah->ref);
                                neigh->ah       = mcast->ah;
                                list_add_tail(&neigh->list, &mcast->neigh_list);
                        }
                }
                spin_unlock_irqrestore(&priv->lock, flags);
		mcast->used = jiffies;
                ipoib_send(dev, skb, mcast->ah, IB_MULTICAST_QPN);
                if (neigh)
                        ipoib_neigh_put(neigh);
                return;

	}

unlock:
	spin_unlock_irqrestore(&priv->lock, flags);
}

void ipoib_mcast_dev_flush(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = netdev_priv(dev);
	LIST_HEAD(remove_list);
	struct ipoib_mcast *mcast, *tmcast;
	unsigned long flags;

	ipoib_dbg_mcast(priv, "flushing multicast list\n");

	spin_lock_irqsave(&priv->lock, flags);

	list_for_each_entry_safe(mcast, tmcast, &priv->multicast_list, list) {
		list_del(&mcast->list);
		rb_erase(&mcast->rb_node, &priv->multicast_tree);
		list_add_tail(&mcast->list, &remove_list);
	}

	if (priv->broadcast) {
		rb_erase(&priv->broadcast->rb_node, &priv->multicast_tree);
		list_add_tail(&priv->broadcast->list, &remove_list);
		priv->broadcast = NULL;
	}

	spin_unlock_irqrestore(&priv->lock, flags);

	/*seperate between the wait to the leav.*/
	list_for_each_entry_safe(mcast, tmcast, &remove_list, list)
		if (test_bit(IPOIB_MCAST_JOIN_STARTED, &mcast->flags))
			wait_for_completion(&mcast->done);

	list_for_each_entry_safe(mcast, tmcast, &remove_list, list) {
		ipoib_mcast_leave(dev, mcast);
		ipoib_mcast_free(mcast);
	}
}

static int ipoib_mcast_addr_is_valid(const u8 *addr, unsigned int addrlen,
				     const u8 *broadcast)
{
	if (addrlen != INFINIBAND_ALEN)
		return 0;
	/* reserved QPN, prefix, scope */
	if (memcmp(addr, broadcast, 5))
		return 0;
	/* signature lower */
	if (addr[7] != broadcast[7])
		return 0;
	return 1;
}

void ipoib_mcast_restart_task(struct work_struct *work)
{
	struct ipoib_dev_priv *priv =
		container_of(work, struct ipoib_dev_priv, restart_task);
	struct net_device *dev = priv->dev;
	struct dev_mc_list *mclist;
	struct ipoib_mcast *mcast, *tmcast;
	LIST_HEAD(remove_list);
	unsigned long flags;
	struct ib_sa_mcmember_rec rec;

	ipoib_dbg_mcast(priv, "restarting multicast task\n");

	ipoib_mcast_stop_thread(dev, 0);

	local_irq_save(flags);
	netif_tx_lock(dev);
	spin_lock(&priv->lock);

	/*
	 * Unfortunately, the networking core only gives us a list of all of
	 * the multicast hardware addresses. We need to figure out which ones
	 * are new and which ones have been removed
	 */

	/* Clear out the found flag */
	list_for_each_entry(mcast, &priv->multicast_list, list)
		clear_bit(IPOIB_MCAST_FLAG_FOUND, &mcast->flags);

	/* Mark all of the entries that are found or don't exist */
	for (mclist = dev->mc_list; mclist; mclist = mclist->next) {
		union ib_gid mgid;

		if (!ipoib_mcast_addr_is_valid(mclist->dmi_addr,
					       mclist->dmi_addrlen,
					       dev->broadcast))
			continue;

		memcpy(mgid.raw, mclist->dmi_addr + 4, sizeof mgid);

		/* update scope */
		mgid.raw[1] = 0x10 | (dev->broadcast[5] & 0xF);
		/* Add in the P_Key */
		mgid.raw[4] = dev->broadcast[8];
		mgid.raw[5] = dev->broadcast[9];
		mcast = __ipoib_mcast_find(dev, &mgid);
		if (!mcast || test_bit(IPOIB_MCAST_FLAG_SENDONLY, &mcast->flags)) {
			struct ipoib_mcast *nmcast;

			/* ignore group which is directly joined by userspace */
			if ((!mcast && test_bit(IPOIB_FLAG_UMCAST, &priv->flags) &&
			    !ib_sa_get_mcmember_rec(priv->ca, priv->port, &mgid, &rec)) ||
			    (mcast && test_bit(IPOIB_MCAST_UMCAST_ATTACHED, &mcast->flags))) {
				ipoib_dbg_mcast(priv, "ignoring multicast entry for mgid "
						IPOIB_GID_FMT "\n", IPOIB_GID_ARG(mgid));
				continue;
			}

			/* Not found or send-only group, let's add a new entry */
			ipoib_dbg_mcast(priv, "adding multicast entry for mgid "
					IPOIB_GID_FMT "\n", IPOIB_GID_ARG(mgid));

			nmcast = ipoib_mcast_alloc(dev, 0);
			if (!nmcast) {
				ipoib_warn(priv, "unable to allocate memory for multicast structure\n");
				continue;
			}

			set_bit(IPOIB_MCAST_FLAG_FOUND, &nmcast->flags);

			nmcast->mcmember.mgid = mgid;

			if (mcast) {
				/* Destroy the send only entry */
				list_move_tail(&mcast->list, &remove_list);

				rb_replace_node(&mcast->rb_node,
						&nmcast->rb_node,
						&priv->multicast_tree);
			} else
				__ipoib_mcast_add(dev, nmcast);

			list_add_tail(&nmcast->list, &priv->multicast_list);
		}

		if (mcast)
			set_bit(IPOIB_MCAST_FLAG_FOUND, &mcast->flags);
	}

	/* Remove all of the entries don't exist anymore */
	list_for_each_entry_safe(mcast, tmcast, &priv->multicast_list, list) {
		if (!test_bit(IPOIB_MCAST_FLAG_FOUND, &mcast->flags) &&
		    !test_bit(IPOIB_MCAST_FLAG_SENDONLY, &mcast->flags)) {
			ipoib_dbg_mcast(priv, "deleting multicast group " IPOIB_GID_FMT "\n",
					IPOIB_GID_ARG(mcast->mcmember.mgid));

			rb_erase(&mcast->rb_node, &priv->multicast_tree);

			/* Move to the remove list */
			list_move_tail(&mcast->list, &remove_list);
		}
	}

	spin_unlock(&priv->lock);
	netif_tx_unlock(dev);
	local_irq_restore(flags);

	/* We have to cancel outside of the spinlock */
	list_for_each_entry_safe(mcast, tmcast, &remove_list, list) {
		ipoib_mcast_leave(mcast->dev, mcast);
		ipoib_mcast_free(mcast);
	}

	if (test_bit(IPOIB_FLAG_ADMIN_UP, &priv->flags))
		ipoib_mcast_start_thread(dev);
}

void ipoib_mcast_leave_task(struct work_struct *work)
{
	struct ipoib_dev_priv *priv =
		container_of(work, struct ipoib_dev_priv, mcast_leave_task.work);
	struct net_device *dev = priv->dev;
	struct ipoib_mcast *mcast, *tmcast;
	LIST_HEAD(remove_list);

	if (!test_bit(IPOIB_MCAST_RUN_GC, &priv->flags))
		return;

	if (ipoib_mc_sendonly_timeout > 0) {
		list_for_each_entry_safe(mcast, tmcast, &priv->multicast_list, list) {
			if (test_bit(IPOIB_MCAST_FLAG_SENDONLY, &mcast->flags) &&
			    time_before(mcast->used, jiffies - ipoib_mc_sendonly_timeout * HZ)) {
				rb_erase(&mcast->rb_node, &priv->multicast_tree);
				list_move_tail(&mcast->list, &remove_list);
			}
		}

		list_for_each_entry_safe(mcast, tmcast, &remove_list, list) {
			ipoib_mcast_leave(dev, mcast);
			ipoib_mcast_free(mcast);
		}
	}

	mutex_lock(&mcast_mutex);
	if (test_bit(IPOIB_MCAST_RUN_GC, &priv->flags))
		queue_delayed_work(ipoib_workqueue, &priv->mcast_leave_task, 60 * HZ);
	mutex_unlock(&mcast_mutex);
}

#ifdef CONFIG_INFINIBAND_IPOIB_DEBUG

struct ipoib_mcast_iter *ipoib_mcast_iter_init(struct net_device *dev)
{
	struct ipoib_mcast_iter *iter;

	iter = kmalloc(sizeof *iter, GFP_KERNEL);
	if (!iter)
		return NULL;

	iter->dev = dev;
	memset(iter->mgid.raw, 0, 16);

	if (ipoib_mcast_iter_next(iter)) {
		kfree(iter);
		return NULL;
	}

	return iter;
}

int ipoib_mcast_iter_next(struct ipoib_mcast_iter *iter)
{
	struct ipoib_dev_priv *priv = netdev_priv(iter->dev);
	struct rb_node *n;
	struct ipoib_mcast *mcast;
	int ret = 1;

	spin_lock_irq(&priv->lock);

	n = rb_first(&priv->multicast_tree);

	while (n) {
		mcast = rb_entry(n, struct ipoib_mcast, rb_node);

		if (memcmp(iter->mgid.raw, mcast->mcmember.mgid.raw,
			   sizeof (union ib_gid)) < 0) {
			iter->mgid      = mcast->mcmember.mgid;
			iter->created   = mcast->created;
			iter->queuelen  = skb_queue_len(&mcast->pkt_queue);
			iter->complete  = !!mcast->ah;
			iter->send_only = !!(mcast->flags & (1 << IPOIB_MCAST_FLAG_SENDONLY));

			ret = 0;

			break;
		}

		n = rb_next(n);
	}

	spin_unlock_irq(&priv->lock);

	return ret;
}

void ipoib_mcast_iter_read(struct ipoib_mcast_iter *iter,
			   union ib_gid *mgid,
			   unsigned long *created,
			   unsigned int *queuelen,
			   unsigned int *complete,
			   unsigned int *send_only)
{
	*mgid      = iter->mgid;
	*created   = iter->created;
	*queuelen  = iter->queuelen;
	*complete  = iter->complete;
	*send_only = iter->send_only;
}

#endif /* CONFIG_INFINIBAND_IPOIB_DEBUG */
