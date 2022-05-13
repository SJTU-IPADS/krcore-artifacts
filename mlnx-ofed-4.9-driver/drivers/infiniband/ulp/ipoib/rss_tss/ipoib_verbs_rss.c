/*
 * Copyright (c) 2004, 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Mellanox Technologies. All rights reserved.
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
static int set_qps_qkey_rss(struct ipoib_dev_priv *priv)
{
	struct ib_qp_attr *qp_attr;
	struct ipoib_recv_ring *recv_ring;
	int ret = -ENOMEM;
	int i;

	for (i = 0; i < priv->num_tx_queues; i++)
		priv->send_ring[i].tx_wr.remote_qkey = priv->qkey;

	qp_attr = kmalloc(sizeof *qp_attr, GFP_KERNEL);
	if (!qp_attr)
		return -ENOMEM;

	qp_attr->qkey = priv->qkey;
	recv_ring = priv->recv_ring;
	for (i = 0; i < priv->num_rx_queues; ++i) {
		ret = ib_modify_qp(recv_ring->recv_qp, qp_attr, IB_QP_QKEY);
		if (ret)
			break;
		recv_ring++;
	}

	kfree(qp_attr);

	return ret;
}

int ipoib_mcast_attach_rss(struct net_device *dev, struct ib_device *hca,
			   union ib_gid *mgid, u16 mlid, int set_qkey, u32 qkey)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	struct ib_qp_attr *qp_attr = NULL;
	int ret;
	u16 pkey_index;

	if (ib_find_pkey(priv->ca, priv->port, priv->pkey, &pkey_index)) {
		clear_bit(IPOIB_PKEY_ASSIGNED, &priv->flags);
		ret = -ENXIO;
		goto out;
	}
	set_bit(IPOIB_PKEY_ASSIGNED, &priv->flags);

	if (set_qkey) {
		ret = set_qps_qkey_rss(priv);
		if (ret)
			goto out;
	}

	/* attach QP to multicast group */
	ret = ib_attach_mcast(priv->qp, mgid, mlid);
	if (ret)
		ipoib_warn(priv, "failed to attach to multicast group, ret = %d\n", ret);

out:
	kfree(qp_attr);
	return ret;
}

static int ipoib_init_one_qp(struct ipoib_dev_priv *priv, struct ib_qp *qp,
				int init_attr)
{
	int ret;
	struct ib_qp_attr qp_attr;
	int attr_mask;

	qp_attr.qp_state = IB_QPS_INIT;
	qp_attr.qkey = 0;
	qp_attr.port_num = priv->port;
	qp_attr.pkey_index = priv->pkey_index;
	attr_mask =
	    IB_QP_QKEY |
	    IB_QP_PORT |
	    IB_QP_PKEY_INDEX |
	    IB_QP_STATE | init_attr;

	ret = ib_modify_qp(qp, &qp_attr, attr_mask);
	if (ret) {
		ipoib_warn(priv, "failed to modify QP to INT, ret = %d\n", ret);
		goto out_fail;
	}

	qp_attr.qp_state = IB_QPS_RTR;
	/* Can't set this in a INIT->RTR transition */
	attr_mask &= ~(IB_QP_PORT | init_attr);
	ret = ib_modify_qp(qp, &qp_attr, attr_mask);
	if (ret) {
		ipoib_warn(priv, "failed to modify QP to RTR, ret = %d\n", ret);
		goto out_fail;
	}

	qp_attr.qp_state = IB_QPS_RTS;
	qp_attr.sq_psn = 0;
	attr_mask |= IB_QP_SQ_PSN;
	attr_mask &= ~IB_QP_PKEY_INDEX;
	ret = ib_modify_qp(qp, &qp_attr, attr_mask);
	if (ret) {
		ipoib_warn(priv, "failed to modify QP to RTS, ret = %d\n", ret);
		goto out_fail;
	}

	return 0;

out_fail:
	qp_attr.qp_state = IB_QPS_RESET;
	if (ib_modify_qp(qp, &qp_attr, IB_QP_STATE))
		ipoib_warn(priv, "Failed to modify QP to RESET state\n");

	return ret;
}

static int ipoib_init_rss_qps(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	struct ipoib_recv_ring *recv_ring;
	struct ib_qp_attr qp_attr;
	int i;
	int ret;

	recv_ring = priv->recv_ring;
	for (i = 0; i < priv->rss_qp_num; i++) {
		ret = ipoib_init_one_qp(priv, recv_ring->recv_qp, 0);
		if (ret) {
			ipoib_warn(priv,
				"failed to init rss qp, ind = %d, ret=%d\n",
				i, ret);
			goto out_free_reset_qp;
		}
		recv_ring++;
	}

	return 0;

out_free_reset_qp:
	for (--i; i >= 0; --i) {
		qp_attr.qp_state = IB_QPS_RESET;
		if (ib_modify_qp(priv->recv_ring[i].recv_qp,
				&qp_attr, IB_QP_STATE))
			ipoib_warn(priv,
				"Failed to modify QP to RESET state\n");
	}

	return ret;
}

static int ipoib_init_tss_qps(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	struct ipoib_send_ring *send_ring;
	struct ib_qp_attr qp_attr;
	int i;
	int ret;

	send_ring = priv->send_ring;
	/*
	 * Note if priv->tss_qdisc_num > priv->tss_qp_num then since
	 * the last QP is the parent QP and it will be initialize later
	 */
	for (i = 0; i < priv->tss_qp_num; i++) {
		ret = ipoib_init_one_qp(priv, send_ring->send_qp, 0);
		if (ret) {
			ipoib_warn(priv,
				"failed to init tss qp, ind = %d, ret=%d\n",
				i, ret);
			goto out_free_reset_qp;
		}
		send_ring++;
	}

	return 0;

out_free_reset_qp:
	for (--i; i >= 0; --i) {
		qp_attr.qp_state = IB_QPS_RESET;
		if (ib_modify_qp(priv->send_ring[i].send_qp,
				&qp_attr, IB_QP_STATE))
			ipoib_warn(priv,
				"Failed to modify QP to RESET state\n");
	}

	return ret;
}

int ipoib_init_qp_rss(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	struct ib_qp_attr qp_attr;
	int ret, i, attr;

	if (!test_bit(IPOIB_PKEY_ASSIGNED, &priv->flags)) {
		ipoib_warn(priv, "PKEY not assigned\n");
		return -1;
	}

	/* Init parent QP */
	/* If rss_qp_num = 0 then the parent QP is the RX QP */
	ret = ipoib_init_rss_qps(dev);
	if (ret)
		return ret;

	ret = ipoib_init_tss_qps(dev);
	if (ret)
		goto out_reset_tss_qp;

	/* Init the parent QP which can be the only QP */
	attr = priv->rss_qp_num > 0 ? IB_QP_GROUP_RSS : 0;
	ret = ipoib_init_one_qp(priv, priv->qp, attr);
	if (ret) {
		ipoib_warn(priv, "failed to init parent qp, ret=%d\n", ret);
		goto out_reset_rss_qp;
	}

	return 0;

out_reset_rss_qp:
	for (i = 0; i < priv->rss_qp_num; i++) {
		qp_attr.qp_state = IB_QPS_RESET;
		if (ib_modify_qp(priv->recv_ring[i].recv_qp,
				 &qp_attr, IB_QP_STATE))
			ipoib_warn(priv,
				   "Failed to modify QP to RESET state\n");
	}

out_reset_tss_qp:
	for (i = 0; i < priv->tss_qp_num; i++) {
		qp_attr.qp_state = IB_QPS_RESET;
		if (ib_modify_qp(priv->send_ring[i].send_qp,
				 &qp_attr, IB_QP_STATE))
			ipoib_warn(priv,
				   "Failed to modify QP to RESET state\n");
	}

	return ret;
}

static int ipoib_transport_cq_init_rss(struct net_device *dev,
				       int size)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	struct ipoib_recv_ring *recv_ring;
	struct ipoib_send_ring *send_ring;
	struct ib_cq *cq;
	int i, allocated_rx, allocated_tx, req_vec, ret;
	struct ib_cq_init_attr cq_attr = {};

	allocated_rx = 0;
	allocated_tx = 0;

	/* We over subscribed the CPUS, ports start from 1 */
	req_vec = (priv->port - 1) * roundup_pow_of_two(num_online_cpus());
	recv_ring = priv->recv_ring;
	for (i = 0; i < priv->num_rx_queues; i++) {
		/* Try to spread vectors based on port and ring numbers */
		cq_attr.cqe = size;
		cq_attr.comp_vector = req_vec % priv->ca->num_comp_vectors;
		cq = ib_create_cq(priv->ca, ipoib_ib_completion_rss, NULL,
				  recv_ring, &cq_attr);
		if (IS_ERR(cq)) {
			printk(KERN_WARNING "%s: failed to create recv CQ\n",
			       priv->ca->name);
			goto out_free_recv_cqs;
		}
		recv_ring->recv_cq = cq;
		allocated_rx++;
		req_vec++;
		if (ib_req_notify_cq(recv_ring->recv_cq, IB_CQ_NEXT_COMP)) {
			printk(KERN_WARNING "%s: req notify recv CQ\n",
			       priv->ca->name);
			goto out_free_recv_cqs;
		}
		recv_ring++;
	}

	/* We over subscribed the CPUS, ports start from 1 */
	req_vec = (priv->port - 1) * roundup_pow_of_two(num_online_cpus());
	send_ring = priv->send_ring;
	for (i = 0; i < priv->num_tx_queues; i++) {
		cq_attr.cqe = priv->sendq_size;
		cq_attr.comp_vector = req_vec % priv->ca->num_comp_vectors;
		cq = ib_create_cq(priv->ca,
				  ipoib_ib_tx_completion_rss, NULL,
				  send_ring, &cq_attr);
		if (IS_ERR(cq)) {
			printk(KERN_WARNING "%s: failed to create send CQ\n",
			       priv->ca->name);
			goto out_free_send_cqs;
		}
		send_ring->send_cq = cq;
		allocated_tx++;
		req_vec++;

		ret = ib_req_notify_cq(send_ring->send_cq, IB_CQ_NEXT_COMP);
		if (ret)
			pr_warn("%s: ib_req_notify_cq failed on index %u (%d)\n",
				__func__, send_ring->index, ret);

		send_ring++;
	}

	return 0;

out_free_send_cqs:
	for (i = 0 ; i < allocated_tx ; i++) {
		ib_destroy_cq(priv->send_ring[i].send_cq);
		priv->send_ring[i].send_cq = NULL;
	}

out_free_recv_cqs:
	for (i = 0 ; i < allocated_rx ; i++) {
		ib_destroy_cq(priv->recv_ring[i].recv_cq);
		priv->recv_ring[i].recv_cq = NULL;
	}

	return -ENODEV;
}

static int ipoib_create_parent_qp(struct net_device *dev,
				  struct ib_device *ca)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	struct ib_exp_qp_init_attr init_attr = {
		.sq_sig_type = IB_SIGNAL_ALL_WR,
		.qp_type     = IB_EXP_UD_RSS_TSS,
		.cap.max_inline_data    = IPOIB_MAX_INLINE_SIZE,
		.cap.max_send_wr	= priv->sendq_size,
		.cap.max_send_sge 	= min_t(u32, priv->ca->attrs.max_send_sge,
						MAX_SKB_FRAGS + 1),
	};
	struct ib_qp *qp;

	if (priv->hca_caps & IB_DEVICE_UD_TSO)
		init_attr.create_flags |= IB_QP_CREATE_IPOIB_UD_LSO;

	if (priv->hca_caps & IB_DEVICE_BLOCK_MULTICAST_LOOPBACK)
		init_attr.create_flags |= IB_QP_CREATE_BLOCK_MULTICAST_LOOPBACK;

	if (dev->features & NETIF_F_SG)
		init_attr.cap.max_send_sge = priv->max_send_sge;

	if (priv->tss_qp_num == 0 && priv->rss_qp_num == 0)
		/* Legacy mode */
		init_attr.qpg_type = IB_QPG_NONE;
	else {
		init_attr.qpg_type = IB_QPG_PARENT;
		init_attr.parent_attrib.tss_child_count = priv->tss_qp_num;
		init_attr.parent_attrib.rss_child_count = priv->rss_qp_num;
	}

	if (priv->hca_caps & IB_DEVICE_MANAGED_FLOW_STEERING)
		init_attr.create_flags |= IB_QP_CREATE_NETIF_QP;

	/* No RSS parent QP will be used for RX */
	if (priv->rss_qp_num == 0) {
		init_attr.cap.max_recv_wr  = priv->recvq_size;
		init_attr.cap.max_recv_sge = IPOIB_UD_RX_SG;
	}

	/* Note that if parent QP is not used for RX/TX then this is harmless */
	init_attr.recv_cq = priv->recv_ring[0].recv_cq;
	init_attr.send_cq = priv->send_ring[priv->tss_qp_num].send_cq;

	qp = ib_create_qp(priv->pd, (struct ib_qp_init_attr *)&init_attr);
	if (IS_ERR(qp))
		return IS_ERR(qp); /* qp is an error value and will be checked */

	priv->qp = qp;

	/* check inline availablilty */
	if (!init_attr.cap.max_inline_data) {
		/* mark it as not-available */
		ipoib_inline_thold = 0;
		pr_info("card: %s doesn't support inline for QP: 0x%x\n",
			ca->name, priv->qp->qp_num);
	} else {
		ipoib_inline_thold = min(ipoib_inline_thold,
					 init_attr.cap.max_inline_data);
		pr_info("card: %s, QP: 0x%x, inline size: %d\n", ca->name,
			priv->qp->qp_num, ipoib_inline_thold);
	}

	/* TSS is not supported in HW or NO TSS (tss_qp_num = 0) */
	if (priv->num_tx_queues > priv->tss_qp_num)
		priv->send_ring[priv->tss_qp_num].send_qp = qp;

	/* No RSS parent QP will be used for RX */
	if (priv->rss_qp_num == 0)
		priv->recv_ring[0].recv_qp = qp;

	/* only with SW TSS there is a need for a mask */
	if (priv->tss_qp_num == 0)
		/* TSS is supported by HW or no TSS at all */
		priv->tss_qpn_mask_sz = 0;
	else {
		/* SW TSS, get mask back from HW, put in the upper nibble */
		u16 tmp = (u16)init_attr.cap.qpg_tss_mask_sz;
		priv->tss_qpn_mask_sz = cpu_to_be16((tmp << 12));
	}

	return 0;
}

static struct ib_qp *ipoib_create_tss_qp(struct net_device *dev,
					 struct ib_device *ca,
					 int ind)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	struct ib_exp_qp_init_attr init_attr = {
		.cap = {
			.max_send_wr  = priv->sendq_size,
			.max_recv_wr  = priv->recvq_size,
			.max_send_sge = min_t(u32, priv->ca->attrs.max_send_sge,
					      MAX_SKB_FRAGS + 1),
			.max_inline_data = IPOIB_MAX_INLINE_SIZE,
		},
		.sq_sig_type = IB_SIGNAL_ALL_WR,
		.qp_type     = IB_EXP_UD_RSS_TSS
	};
	struct ib_qp *qp;

	if (priv->hca_caps & IB_DEVICE_UD_TSO)
		init_attr.create_flags |= IB_QP_CREATE_IPOIB_UD_LSO;

	if (dev->features & NETIF_F_SG)
		init_attr.cap.max_send_sge = MAX_SKB_FRAGS + 1;

	init_attr.qpg_type = IB_QPG_CHILD_TX;
	init_attr.qpg_parent = priv->qp;

	init_attr.send_cq = init_attr.recv_cq = priv->send_ring[ind].send_cq;

	qp = ib_create_qp(priv->pd, (struct ib_qp_init_attr *)&init_attr);
	if (IS_ERR(qp)) {
		pr_warn("%s: failed to create TSS QP(%d)\n", ca->name, ind);
		return qp; /* qp is an error value and will be checked */
	}

	return qp;
}

static struct ib_qp *ipoib_create_rss_qp(struct net_device *dev,
					 struct ib_device *ca,
					 int ind)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	struct ib_exp_qp_init_attr init_attr = {
		.cap = {
			.max_recv_wr  = priv->recvq_size,
			.max_recv_sge = IPOIB_UD_RX_SG
		},
		.sq_sig_type = IB_SIGNAL_ALL_WR,
		.qp_type     = IB_EXP_UD_RSS_TSS
	};
	struct ib_qp *qp;

	init_attr.qpg_type = IB_QPG_CHILD_RX;
	init_attr.qpg_parent = priv->qp;

	init_attr.send_cq = init_attr.recv_cq = priv->recv_ring[ind].recv_cq;

	qp = ib_create_qp(priv->pd, (struct ib_qp_init_attr *)&init_attr);
	if (IS_ERR(qp)) {
		pr_warn("%s: failed to create RSS QP(%d)\n", ca->name, ind);
		return qp; /* qp is an error value and will be checked */
	}

	return qp;
}

static int ipoib_create_other_qps(struct net_device *dev,
				  struct ib_device *ca)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	struct ipoib_send_ring *send_ring;
	struct ipoib_recv_ring *recv_ring;
	int i, rss_created, tss_created;
	struct ib_qp *qp;

	tss_created = 0;
	send_ring = priv->send_ring;
	for (i = 0; i < priv->tss_qp_num; i++) {
		qp = ipoib_create_tss_qp(dev, ca, i);
		if (IS_ERR(qp)) {
			printk(KERN_WARNING "%s: failed to create QP\n",
			       ca->name);
			goto out_free_send_qp;
		}
		send_ring->send_qp = qp;
		send_ring++;
		tss_created++;
	}

	rss_created = 0;
	recv_ring = priv->recv_ring;
	for (i = 0; i < priv->rss_qp_num; i++) {
		qp = ipoib_create_rss_qp(dev, ca, i);
		if (IS_ERR(qp)) {
			printk(KERN_WARNING "%s: failed to create QP\n",
			       ca->name);
			goto out_free_recv_qp;
		}
		recv_ring->recv_qp = qp;
		recv_ring++;
		rss_created++;
	}

	return 0;

out_free_recv_qp:
	for (i = 0; i < rss_created; i++) {
		ib_destroy_qp(priv->recv_ring[i].recv_qp);
		priv->recv_ring[i].recv_qp = NULL;
	}

out_free_send_qp:
	for (i = 0; i < tss_created; i++) {
		ib_destroy_qp(priv->send_ring[i].send_qp);
		priv->send_ring[i].send_qp = NULL;
	}

	return -ENODEV;
}

int ipoib_transport_dev_init_rss(struct net_device *dev, struct ib_device *ca)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	struct ipoib_send_ring *send_ring;
	struct ipoib_recv_ring *recv_ring, *first_recv_ring;
	int ret, size;
	int i, j;

	if (min_t(u32, priv->ca->attrs.max_send_sge, MAX_SKB_FRAGS + 1) > 1)
		dev->features |= NETIF_F_SG;

	priv->max_send_sge = min_t(u32, priv->ca->attrs.max_send_sge,
				   MAX_SKB_FRAGS + 1);

	size = priv->recvq_size + 1;
	ret = ipoib_cm_dev_init(dev);
	if (!ret) {
		size += priv->sendq_size;
		if (ipoib_cm_has_srq(dev))
			size += priv->recvq_size + 1; /* 1 extra for rx_drain_qp */
		else
			size += priv->recvq_size * ipoib_max_conn_qp;
	} else
		if (ret != -ENOSYS)
			return -ENODEV;

	/* Create CQ(s) */
	ret = ipoib_transport_cq_init_rss(dev, size);
	if (ret) {
		pr_warn("%s: ipoib_transport_cq_init_rss failed\n", ca->name);
		goto out_cm_dev_cleanup;
	 }

	/* Init the parent QP */
	ret = ipoib_create_parent_qp(dev, ca);
	if (ret) {
		pr_warn("%s: failed to create parent QP (ret = %d)\n",
			ca->name, ret);
		if (ret == -ENOMEM)
			printk(KERN_ERR "IPoIB ERROR - check send_queue_size module parameter\n");
		goto out_free_cqs;
	}

	/* create TSS & RSS QPs */
	ret = ipoib_create_other_qps(dev, ca);
	if (ret) {
		pr_warn("%s: failed to create QP(s)\n", ca->name);
		goto out_free_parent_qp;
	}

	send_ring = priv->send_ring;
	for (j = 0; j < priv->num_tx_queues; j++) {
		for (i = 0; i < MAX_SKB_FRAGS + 1; ++i)
			send_ring->tx_sge[i].lkey = priv->pd->local_dma_lkey;

		send_ring->tx_wr.wr.opcode	= IB_WR_SEND;
		send_ring->tx_wr.wr.sg_list	= send_ring->tx_sge;
		send_ring->tx_wr.wr.send_flags	= IB_SEND_SIGNALED;
		send_ring++;
	}
	recv_ring = priv->recv_ring;
	recv_ring->rx_sge[0].lkey = priv->pd->local_dma_lkey;

	recv_ring->rx_sge[0].length = IPOIB_UD_BUF_SIZE(priv->max_ib_mtu);
	recv_ring->rx_wr.num_sge = 1;

	recv_ring->rx_wr.next = NULL;
	recv_ring->rx_wr.sg_list = recv_ring->rx_sge;

	/* Copy first RX ring sge and wr parameters to the rest RX ring */
	first_recv_ring = recv_ring;
	recv_ring++;
	for (i = 1; i < priv->num_rx_queues; i++) {
		recv_ring->rx_sge[0] = first_recv_ring->rx_sge[0];
		recv_ring->rx_sge[1] = first_recv_ring->rx_sge[1];
		recv_ring->rx_wr = first_recv_ring->rx_wr;
		/* This field in per ring */
		recv_ring->rx_wr.sg_list = recv_ring->rx_sge;
		recv_ring++;
	}

	return 0;

out_free_parent_qp:
	ib_destroy_qp(priv->qp);
	priv->qp = NULL;

out_free_cqs:
	for (i = 0; i < priv->num_rx_queues; i++) {
		ib_destroy_cq(priv->recv_ring[i].recv_cq);
		priv->recv_ring[i].recv_cq = NULL;
	}
	for (i = 0; i < priv->num_tx_queues; i++) {
		ib_destroy_cq(priv->send_ring[i].send_cq);
		priv->send_ring[i].send_cq = NULL;
	}

out_cm_dev_cleanup:
	ipoib_cm_dev_cleanup(dev);

	return -ENODEV;
}

static void ipoib_destroy_tx_qps(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	struct ipoib_send_ring *send_ring;
	int i;

	if (NULL == priv->send_ring)
		return;

	send_ring = priv->send_ring;
	for (i = 0; i < priv->tss_qp_num; i++) {
		if (send_ring->send_qp) {
			if (ib_destroy_qp(send_ring->send_qp))
				ipoib_warn(priv, "ib_destroy_qp (send) failed\n");
			send_ring->send_qp = NULL;
		}
		send_ring++;
	}

	/*
	 * No support of TSS in HW
	 * so there is an extra QP but it is freed later
	 */
	if (priv->num_tx_queues > priv->tss_qp_num)
		send_ring->send_qp = NULL;
}

static void ipoib_destroy_rx_qps(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	struct ipoib_recv_ring *recv_ring;
	int i;

	if (NULL == priv->recv_ring)
		return;

	recv_ring = priv->recv_ring;
	for (i = 0; i < priv->rss_qp_num; i++) {
		if (recv_ring->recv_qp) {
			if (ib_destroy_qp(recv_ring->recv_qp))
				ipoib_warn(priv, "ib_destroy_qp (recv) failed\n");
			recv_ring->recv_qp = NULL;
		}
		recv_ring++;
	}
}

static void ipoib_destroy_tx_cqs(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	struct ipoib_send_ring *send_ring;
	int i;

	if (NULL == priv->send_ring)
		return;

	send_ring = priv->send_ring;
	for (i = 0; i < priv->num_tx_queues; i++) {
		if (send_ring->send_cq) {
			if (ib_destroy_cq(send_ring->send_cq))
				ipoib_warn(priv, "ib_destroy_cq (send) failed\n");
			send_ring->send_cq = NULL;
		}
		send_ring++;
	}
}

static void ipoib_destroy_rx_cqs(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	struct ipoib_recv_ring *recv_ring;
	int i;

	if (NULL == priv->recv_ring)
		return;

	recv_ring = priv->recv_ring;
	for (i = 0; i < priv->num_rx_queues; i++) {
		if (recv_ring->recv_cq) {
			if (ib_destroy_cq(recv_ring->recv_cq))
				ipoib_warn(priv, "ib_destroy_cq (recv) failed\n");
			recv_ring->recv_cq = NULL;
		}
		recv_ring++;
	}
}

void ipoib_transport_dev_cleanup_rss(struct net_device *dev)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	int ret;

	ipoib_destroy_rx_qps(dev);
	ipoib_destroy_tx_qps(dev);

	/* Destroy parent or only QP */
	if (priv->qp) {
		ret = ib_destroy_qp(priv->qp);
		if (ret)
			ipoib_warn(priv, "ib_qp_destroy failed (ret = %d)\n",
				   ret);

		priv->qp = NULL;
	}

	ipoib_destroy_rx_cqs(dev);
	ipoib_destroy_tx_cqs(dev);
}
