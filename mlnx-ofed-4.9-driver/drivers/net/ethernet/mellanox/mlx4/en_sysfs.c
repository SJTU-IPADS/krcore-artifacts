/*
 * Copyright (c) 2015 Mellanox Technologies. All rights reserved.
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
 *
 */

#include <linux/device.h>
#include <linux/netdevice.h>

#include "mlx4_en.h"

#define to_en_priv(cd)	((struct mlx4_en_priv *)(netdev_priv(to_net_dev(cd))))

#ifdef CONFIG_SYSFS_QCN

#define MLX4_EN_NUM_QCN_PARAMS	12

static ssize_t mlx4_en_show_qcn(struct device *d,
				struct device_attribute *attr,
				char *buf)
{
	 struct mlx4_en_priv *priv = to_en_priv(d);
	int i;
	int len = 0;
	struct ieee_qcn qcn;
	int ret;

	ret = mlx4_en_dcbnl_ieee_getqcn(priv->dev, &qcn);
	if (ret)
		return ret;

	for (i = 0; i < MLX4_EN_NUM_TC; i++) {
		len += sprintf(buf + len, "%s %d %s", "priority", i, ": ");
		len += sprintf(buf + len, "%u ", qcn.rpg_enable[i]);
		len += sprintf(buf + len, "%u ", qcn.rppp_max_rps[i]);
		len += sprintf(buf + len, "%u ", qcn.rpg_time_reset[i]);
		len += sprintf(buf + len, "%u ", qcn.rpg_byte_reset[i]);
		len += sprintf(buf + len, "%u ", qcn.rpg_threshold[i]);
		len += sprintf(buf + len, "%u ", qcn.rpg_max_rate[i]);
		len += sprintf(buf + len, "%u ", qcn.rpg_ai_rate[i]);
		len += sprintf(buf + len, "%u ", qcn.rpg_hai_rate[i]);
		len += sprintf(buf + len, "%u ", qcn.rpg_gd[i]);
		len += sprintf(buf + len, "%u ", qcn.rpg_min_dec_fac[i]);
		len += sprintf(buf + len, "%u ", qcn.rpg_min_rate[i]);
		len += sprintf(buf + len, "%u ", qcn.cndd_state_machine[i]);
		len += sprintf(buf + len, "%s", "|");
	}
	len += sprintf(buf + len, "\n");

	return len;
}

static ssize_t mlx4_en_store_qcn(struct device *d,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	int ret;
	struct mlx4_en_priv *priv = to_en_priv(d);
	char save;
	int i = 0;
	int j = 0;
	struct ieee_qcn qcn;

	do {
		int len;
		u32 new_value;

		if (i >= (MLX4_EN_NUM_TC * MLX4_EN_NUM_QCN_PARAMS))
			goto bad_elem_count;

		len = strcspn(buf, " ");
		/* nul-terminate and parse */
		save = buf[len];
		((char *)buf)[len] = '\0';

		if (sscanf(buf, "%u", &new_value) != 1 ||
				new_value < 0) {
			en_err(priv, "bad qcn value: '%s'\n", buf);
			ret = -EINVAL;
			goto out;
		}
		switch (i % MLX4_EN_NUM_QCN_PARAMS) {
		case 0:
			qcn.rpg_enable[j] = new_value;
			break;
		case 1:
			qcn.rppp_max_rps[j] = new_value;
			break;
		case 2:
			qcn.rpg_time_reset[j] = new_value;
			break;
		case 3:
			qcn.rpg_byte_reset[j] = new_value;
			break;
		case 4:
			qcn.rpg_threshold[j] = new_value;
			break;
		case 5:
			qcn.rpg_max_rate[j] = new_value;
			break;
		case 6:
			qcn.rpg_ai_rate[j] = new_value;
			break;
		case 7:
			qcn.rpg_hai_rate[j] = new_value;
			break;
		case 8:
			qcn.rpg_gd[j] = new_value;
			break;
		case 9:
			qcn.rpg_min_dec_fac[j] = new_value;
			break;
		case 10:
			qcn.rpg_min_rate[j] = new_value;
			break;
		case 11:
			qcn.cndd_state_machine[j] = new_value;
			break;
		default:
			ret = -EINVAL;
			goto out;
		}

		buf += len+1;
		i++;
		if ((i % MLX4_EN_NUM_QCN_PARAMS) == 0)
			j++;
	} while (save == ' ');

	if (i != (MLX4_EN_NUM_TC * MLX4_EN_NUM_QCN_PARAMS))
		goto bad_elem_count;

	ret = mlx4_en_dcbnl_ieee_setqcn(priv->dev, &qcn);
	if (!ret)
		ret = count;

out:
	return ret;
bad_elem_count:
	en_err(priv, "bad number of elemets in qcn array\n");
	return -EINVAL;
}

static ssize_t mlx4_en_show_qcnstats(struct device *d,
					struct device_attribute *attr,
					char *buf)
{
	struct mlx4_en_priv *priv = to_en_priv(d);
	int i;
	int len = 0;
	struct ieee_qcn_stats qcn_stats;
	int ret;

	ret = mlx4_en_dcbnl_ieee_getqcnstats(priv->dev, &qcn_stats);
	if (ret)
		return ret;

	for (i = 0; i < MLX4_EN_NUM_TC; i++) {
		len += sprintf(buf + len, "%s %d %s", "priority", i, ": ");
		len += sprintf(buf + len, "%lld ", qcn_stats.rppp_rp_centiseconds[i]);
		len += sprintf(buf + len, "%u ", qcn_stats.rppp_created_rps[i]);
		len += sprintf(buf + len, "%u ", qcn_stats.ignored_cnm[i]);
		len += sprintf(buf + len, "%u ", qcn_stats.estimated_total_rate[i]);
		len += sprintf(buf + len, "%u ", qcn_stats.cnms_handled_successfully[i]);
		len += sprintf(buf + len, "%u ", qcn_stats.min_total_limiters_rate[i]);
		len += sprintf(buf + len, "%u ", qcn_stats.max_total_limiters_rate[i]);
		len += sprintf(buf + len, "%s", "|");
	}
	len += sprintf(buf + len, "\n");

	return len;
}

static DEVICE_ATTR(qcn, S_IRUGO | S_IWUSR,
		mlx4_en_show_qcn, mlx4_en_store_qcn);

static DEVICE_ATTR(qcn_stats, S_IRUGO,
			mlx4_en_show_qcnstats, NULL);
#endif

#ifdef CONFIG_SYSFS_MAXRATE
static ssize_t mlx4_en_show_maxrate(struct device *d,
					 struct device_attribute *attr,
					 char *buf)
{
	struct mlx4_en_priv *priv = to_en_priv(d);
	int i;
	int len = 0;
	struct ieee_maxrate maxrate;
	int ret;

	ret = mlx4_en_dcbnl_ieee_getmaxrate(priv->dev, &maxrate);
	if (ret)
		return ret;

	for (i = 0; i < MLX4_EN_NUM_TC; i++)
		len += sprintf(buf + len, "%lld ", maxrate.tc_maxrate[i]);
	len += sprintf(buf + len, "\n");

	return len;
}

static ssize_t mlx4_en_store_maxrate(struct device *d,
					  struct device_attribute *attr,
					  const char *buf, size_t count)
{
	int ret;
	struct mlx4_en_priv *priv = to_en_priv(d);
	char save;
	int i = 0;
	struct ieee_maxrate maxrate;

	do {
		int len;
		u64 new_value;

		if (i >= MLX4_EN_NUM_TC)
			goto bad_elem_count;

		len = strcspn(buf, " ");

		/* nul-terminate and parse */
		save = buf[len];
		((char *)buf)[len] = '\0';

		if (sscanf(buf, "%lld", &new_value) != 1 ||
				new_value < 0) {
			en_err(priv, "bad maxrate value: '%s'\n", buf);
			ret = -EINVAL;
			goto out;
		}
		maxrate.tc_maxrate[i] = new_value;

		buf += len+1;
		i++;
	} while (save == ' ');

	if (i != MLX4_EN_NUM_TC)
		goto bad_elem_count;

	ret = mlx4_en_dcbnl_ieee_setmaxrate(priv->dev, &maxrate);
	if (!ret)
		ret = count;

out:
	return ret;

bad_elem_count:
	en_err(priv, "bad number of elemets in maxrate array\n");
	return -EINVAL;
}

static DEVICE_ATTR(maxrate, S_IRUGO | S_IWUSR,
		   mlx4_en_show_maxrate, mlx4_en_store_maxrate);
#endif

#ifdef CONFIG_SYSFS_MQPRIO

#define MLX4_EN_NUM_SKPRIO		16

static ssize_t mlx4_en_show_tc_num(struct device *d,
				   struct device_attribute *attr,
				   char *buf)
{
	struct mlx4_en_priv *priv = to_en_priv(d);
	struct net_device *netdev = priv->dev;
	int len = 0;

	len = sprintf(buf + len,  "%d\n", netdev_get_num_tc(netdev));

	return len;
}

static ssize_t mlx4_en_store_tc_num(struct device *d,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct mlx4_en_priv *priv = to_en_priv(d);
	struct net_device *netdev = priv->dev;
	int tc_num;
	int err = 0;

	err = sscanf(buf, "%d", &tc_num);

	if (err != 1)
		return -EINVAL;

	rtnl_lock();
	err = mlx4_en_setup_tc(netdev, tc_num);
	rtnl_unlock();

	if (err)
		return err;

	return count;

}

static DEVICE_ATTR(tc_num, S_IRUGO | S_IWUSR,
		   mlx4_en_show_tc_num, mlx4_en_store_tc_num);
#endif

#ifdef CONFIG_SYSFS_INDIR_SETTING
static ssize_t mlx4_en_show_rxfh_indir(struct device *d,
				       struct device_attribute *attr,
				       char *buf)
{
	struct net_device *dev = to_net_dev(d);
	int i, err;
	int len = 0;
	int ring_num;
	u32 *ring_index;

	ring_num = mlx4_en_get_rxfh_indir_size(dev);
	if (ring_num < 0)
		return -EINVAL;

	ring_index = kzalloc(sizeof(u32) * ring_num, GFP_KERNEL);
	if (!ring_index)
		return -ENOMEM;

	err = mlx4_en_get_rxfh_indir(dev, ring_index);
	if (err)
		goto err;

	for (i = 0; i < ring_num; i++)
		len += sprintf(buf + len, "%d\n", ring_index[i]);

	err = len;
err:
	kfree(ring_index);

	return err;
}

static ssize_t mlx4_en_store_rxfh_indir(struct device *d,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct net_device *dev = to_net_dev(d);
	char *endp;
	unsigned long new;
	int i, err;
	int ring_num;
	u32 *ring_index;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	new = simple_strtoul(buf, &endp, 0);
	if (endp == buf)
		return -EINVAL;

	if (!is_power_of_2(new))
		return -EINVAL;

	ring_num = mlx4_en_get_rxfh_indir_size(dev);
	if (ring_num < 0)
		return -EINVAL;

	ring_index = kzalloc(sizeof(u32) * ring_num, GFP_KERNEL);
	if (!ring_index)
		return -ENOMEM;

	for (i = 0; i < ring_num; i++)
		ring_index[i] = i % new;

	err = mlx4_en_set_rxfh_indir(dev, ring_index);
	if (err)
		goto err;

	err = count;

err:
	kfree(ring_index);

	return err;
}

static DEVICE_ATTR(rxfh_indir, S_IRUGO | S_IWUSR,
		   mlx4_en_show_rxfh_indir, mlx4_en_store_rxfh_indir);
#endif

#ifdef CONFIG_SYSFS_NUM_CHANNELS
static ssize_t mlx4_en_show_channels(struct device *d,
		struct device_attribute *attr,
		char *buf, int is_tx)
{
	struct net_device *dev = to_net_dev(d);
	struct ethtool_channels channel;
	int len = 0;

	mlx4_en_get_channels(dev, &channel);

	len += sprintf(buf + len, "%d\n",
			is_tx ? channel.tx_count : channel.rx_count);

	return len;
}

static ssize_t mlx4_en_store_channels(struct device *d,
		struct device_attribute *attr,
		const char *buf, size_t count, int is_tx)
{
	struct net_device *dev = to_net_dev(d);
	char *endp;
	struct ethtool_channels channel;
	int ret = -EINVAL;

	mlx4_en_get_channels(dev, &channel);

	if (is_tx)
		channel.tx_count = simple_strtoul(buf, &endp, 0);
	else
		channel.rx_count = simple_strtoul(buf, &endp, 0);
	if (endp == buf)
		goto err;

	rtnl_lock();
	ret = mlx4_en_set_channels(dev, &channel);
	rtnl_unlock();
	if (ret)
		goto err;

	ret = count;
err:
	return ret;
}

static ssize_t mlx4_en_show_tx_channels(struct device *d,
		struct device_attribute *attr,
		char *buf)
{
	return mlx4_en_show_channels(d, attr, buf, 1);
}

static ssize_t mlx4_en_store_tx_channels(struct device *d,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	return mlx4_en_store_channels(d, attr, buf, count, 1);
}

static ssize_t mlx4_en_show_rx_channels(struct device *d,
		struct device_attribute *attr,
		char *buf)
{
	return mlx4_en_show_channels(d, attr, buf, 0);
}

static ssize_t mlx4_en_store_rx_channels(struct device *d,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	return mlx4_en_store_channels(d, attr, buf, count, 0);
}

static DEVICE_ATTR(tx_channels, S_IRUGO | S_IWUSR,
                  mlx4_en_show_tx_channels, mlx4_en_store_tx_channels);

static DEVICE_ATTR(rx_channels, S_IRUGO | S_IWUSR,
                  mlx4_en_show_rx_channels, mlx4_en_store_rx_channels);

#endif

#ifdef CONFIG_SYSFS_LOOPBACK
static ssize_t mlx4_en_show_loopback(struct device *d,
		struct device_attribute *attr,
		char *buf)
{
	struct net_device *dev = to_net_dev(d);
	int len = 0;

	len += sprintf(buf + len, "%d\n", !!(dev->features & NETIF_F_LOOPBACK));

	return len;
}

static ssize_t mlx4_en_store_loopback(struct device *d,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct net_device *dev = to_net_dev(d);
	char *endp;
	unsigned long new;
	int ret = -EINVAL;
#ifdef HAVE_NET_DEVICE_OPS_EXT
	u32 features = dev->features;
#else
	netdev_features_t features = dev->features;
#endif

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	new = simple_strtoul(buf, &endp, 0);
	if (endp == buf)
		goto err;

	if (new)
		features |= NETIF_F_LOOPBACK;
	else
		features &= ~NETIF_F_LOOPBACK;

	rtnl_lock();
	mlx4_en_set_features(dev, features);
	dev->features = features;
	rtnl_unlock();

	ret = count;

err:
	return ret;
}

static DEVICE_ATTR(loopback, S_IRUGO | S_IWUSR,
                  mlx4_en_show_loopback, mlx4_en_store_loopback);
#endif

static struct attribute *mlx4_en_qos_attrs[] = {
#ifdef CONFIG_SYSFS_MAXRATE
	&dev_attr_maxrate.attr,
#endif
#ifdef CONFIG_SYSFS_MQPRIO
	&dev_attr_tc_num.attr,
#endif
#ifdef CONFIG_SYSFS_INDIR_SETTING
	&dev_attr_rxfh_indir.attr,
#endif
#ifdef CONFIG_SYSFS_NUM_CHANNELS
	&dev_attr_tx_channels.attr,
	&dev_attr_rx_channels.attr,
#endif
#ifdef CONFIG_SYSFS_LOOPBACK
	&dev_attr_loopback.attr,
#endif
#ifdef CONFIG_SYSFS_QCN
	&dev_attr_qcn.attr,
	&dev_attr_qcn_stats.attr,
#endif
	NULL,
};

static struct attribute_group qos_group = {
	.name = "qos",
	.attrs = mlx4_en_qos_attrs,
};

int mlx4_en_sysfs_create(struct net_device *dev)
{
	return sysfs_create_group(&(dev->dev.kobj), &qos_group);
}

void mlx4_en_sysfs_remove(struct net_device *dev)
{
	sysfs_remove_group(&(dev->dev.kobj), &qos_group);
}
