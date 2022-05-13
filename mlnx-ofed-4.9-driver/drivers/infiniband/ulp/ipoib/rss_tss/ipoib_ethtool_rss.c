/*
 * Copyright (c) 2007 Mellanox Technologies. All rights reserved.
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

static int ipoib_set_coalesce_rss(struct net_device *dev,
				  struct ethtool_coalesce *coal)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	int ret, i;

	/*
	 * These values are saved in the private data and returned
	 * when ipoib_get_coalesce() is called
	 */
	if (coal->rx_coalesce_usecs       > 0xffff ||
	    coal->rx_max_coalesced_frames > 0xffff)
		return -EINVAL;

	for (i = 0; i < priv->num_rx_queues; i++) {
		ret = rdma_set_cq_moderation(priv->recv_ring[i].recv_cq,
					     coal->rx_max_coalesced_frames,
					     coal->rx_coalesce_usecs);
		if (ret && ret != -ENOSYS) {
			ipoib_warn(priv, "failed modifying CQ (%d)\n", ret);
			return ret;
		}
	}

	priv->ethtool.coalesce_usecs       = coal->rx_coalesce_usecs;
	priv->ethtool.max_coalesced_frames = coal->rx_max_coalesced_frames;

	return 0;
}

static void ipoib_get_ethtool_stats_rss(struct net_device *dev,
					struct ethtool_stats __always_unused *stats,
					u64 *data)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	struct ipoib_recv_ring *recv_ring;
	struct ipoib_send_ring *send_ring;
	int index = 0;
	int i;

	/* Get per QP stats */
	recv_ring = priv->recv_ring;
	for (i = 0; i < priv->num_rx_queues; i++) {
		struct ipoib_rx_ring_stats *rx_stats = &recv_ring->stats;
		data[index++] = rx_stats->rx_packets;
		data[index++] = rx_stats->rx_bytes;
		data[index++] = rx_stats->rx_errors;
		data[index++] = rx_stats->rx_dropped;
		data[index++] = rx_stats->multicast;
		recv_ring++;
	}
	send_ring = priv->send_ring;
	for (i = 0; i < priv->num_tx_queues; i++) {
		struct ipoib_tx_ring_stats *tx_stats = &send_ring->stats;
		data[index++] = tx_stats->tx_packets;
		data[index++] = tx_stats->tx_bytes;
		data[index++] = tx_stats->tx_errors;
		data[index++] = tx_stats->tx_dropped;
		send_ring++;
	}
	/* Update multicast statistics */
	data[--index] += dev->stats.tx_dropped;
	data[--index] += dev->stats.tx_errors;
}

static void ipoib_get_strings_rss(struct net_device __always_unused *dev,
				  u32 stringset, u8 *data)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);
	int i, index = 0;

	switch (stringset) {
	case ETH_SS_STATS:
		for (i = 0; i < priv->num_rx_queues; i++) {
			sprintf(data + (index++) * ETH_GSTRING_LEN,
					"rx%d_packets", i);
			sprintf(data + (index++) * ETH_GSTRING_LEN,
					"rx%d_bytes", i);
			sprintf(data + (index++) * ETH_GSTRING_LEN,
					"rx%d_errors", i);
			sprintf(data + (index++) * ETH_GSTRING_LEN,
					"rx%d_dropped", i);
			sprintf(data + (index++) * ETH_GSTRING_LEN,
					"multicast");
		}
		for (i = 0; i < priv->num_tx_queues; i++) {
			sprintf(data + (index++) * ETH_GSTRING_LEN,
					"tx%d_packets", i);
			sprintf(data + (index++) * ETH_GSTRING_LEN,
					"tx%d_bytes", i);
			sprintf(data + (index++) * ETH_GSTRING_LEN,
					"tx%d_errors", i);
			sprintf(data + (index++) * ETH_GSTRING_LEN,
					"tx%d_dropped", i);
		}
		break;
	case ETH_SS_TEST:
	default:
		break;
	}
}

static int ipoib_get_sset_count_rss(struct net_device __always_unused *dev,
				    int sset)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);

	switch (sset) {
	case ETH_SS_STATS:
		return ((priv->num_rx_queues * 5) + (priv->num_tx_queues * 4));
	case ETH_SS_TEST:
	default:
		break;
	}
	return -EOPNOTSUPP;
}

static void ipoib_get_channels(struct net_device *dev,
			       struct ethtool_channels *channel)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);

	channel->max_rx = priv->max_rx_queues;
	channel->max_tx = priv->max_tx_queues;
	channel->max_other = 0;
	channel->max_combined = priv->max_rx_queues +
				priv->max_tx_queues;
	channel->rx_count = priv->num_rx_queues;
	channel->tx_count = priv->num_tx_queues;
	channel->other_count = 0;
	channel->combined_count = priv->num_rx_queues +
				  priv->num_tx_queues;
}

static int ipoib_set_channels(struct net_device *dev,
			struct ethtool_channels *channel)
{
	struct ipoib_dev_priv *priv = ipoib_priv(dev);

	if (channel->other_count)
		return -EINVAL;

	if (channel->combined_count !=
	    priv->num_rx_queues + priv->num_tx_queues)
		return -EINVAL;

	if (channel->rx_count == 0 ||
	    channel->rx_count > priv->max_rx_queues)
		return -EINVAL;

	if (!is_power_of_2(channel->rx_count))
		return -EINVAL;

	if (channel->tx_count == 0 ||
	    channel->tx_count > priv->max_tx_queues)
		return -EINVAL;

	/* Nothing to do ? */
	if (channel->rx_count == priv->num_rx_queues &&
	    channel->tx_count == priv->num_tx_queues)
		return 0;

	/* 1 is always O.K. */
	if (channel->tx_count > 1) {
		/* with SW TSS tx_count = 1 + 2 ^ N
		 * 2 is not allowed, make no sense.
		 * if want to disable TSS use 1.
		 */
		if (!is_power_of_2(channel->tx_count - 1) ||
		    channel->tx_count == 2)
			return -EINVAL;
	}

	return ipoib_reinit_rss(dev, channel->rx_count, channel->tx_count);
}

static const struct ethtool_ops ipoib_ethtool_ops_rss = {
	.get_link_ksettings	= ipoib_get_link_ksettings,
	.get_drvinfo		= ipoib_get_drvinfo,
	.get_coalesce		= ipoib_get_coalesce,
	.set_coalesce		= ipoib_set_coalesce_rss,
	.get_link		= ethtool_op_get_link,
	.get_strings		= ipoib_get_strings_rss,
	.get_ethtool_stats	= ipoib_get_ethtool_stats_rss,
	.get_sset_count		= ipoib_get_sset_count_rss,
	.get_channels		= ipoib_get_channels,
	.set_channels		= ipoib_set_channels,
	.set_ringparam		= ipoib_set_ring_param,
	.get_ringparam		= ipoib_get_ring_param,
};

void ipoib_set_ethtool_ops_rss(struct net_device *dev)
{
	dev->ethtool_ops = &ipoib_ethtool_ops_rss;
}
