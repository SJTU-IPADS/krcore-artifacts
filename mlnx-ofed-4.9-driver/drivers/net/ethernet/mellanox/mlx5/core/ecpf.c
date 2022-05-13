/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2019 Mellanox Technologies. */

#include "ecpf.h"
#include <linux/mlx5/driver.h>
#include "mlx5_core.h"
#include "eswitch.h"
#include "en.h"

bool mlx5_read_embedded_cpu(struct mlx5_core_dev *dev)
{
	return (ioread32be(&dev->iseg->initializing) >> MLX5_ECPU_BIT_NUM) & 1;
}

static int mlx5_peer_pf_enable_hca(struct mlx5_core_dev *dev)
{
	u32 out[MLX5_ST_SZ_DW(enable_hca_out)] = {};
	u32 in[MLX5_ST_SZ_DW(enable_hca_in)]   = {};

	MLX5_SET(enable_hca_in, in, opcode, MLX5_CMD_OP_ENABLE_HCA);
	MLX5_SET(enable_hca_in, in, function_id, 0);
	MLX5_SET(enable_hca_in, in, embedded_cpu_function, 0);
	return mlx5_cmd_exec(dev, &in, sizeof(in), &out, sizeof(out));
}

static int mlx5_peer_pf_disable_hca(struct mlx5_core_dev *dev)
{
	u32 out[MLX5_ST_SZ_DW(disable_hca_out)] = {};
	u32 in[MLX5_ST_SZ_DW(disable_hca_in)]   = {};

	MLX5_SET(disable_hca_in, in, opcode, MLX5_CMD_OP_DISABLE_HCA);
	MLX5_SET(disable_hca_in, in, function_id, 0);
	MLX5_SET(disable_hca_in, in, embedded_cpu_function, 0);
	return mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
}

static int mlx5_peer_pf_init(struct mlx5_core_dev *dev)
{
	int err;

	err = mlx5_peer_pf_enable_hca(dev);
	if (err)
		mlx5_core_err(dev, "Failed to enable peer PF HCA err(%d)\n",
			      err);

	return err;
}

static void mlx5_peer_pf_cleanup(struct mlx5_core_dev *dev)
{
	int err;

	err = mlx5_peer_pf_disable_hca(dev);
	if (err) {
		mlx5_core_err(dev, "Failed to disable peer PF HCA err(%d)\n",
			      err);
		return;
	}

	err = mlx5_wait_for_pages(dev, &dev->priv.peer_pf_pages);
	if (err)
		mlx5_core_warn(dev, "Timeout reclaiming peer PF pages err(%d)\n",
			       err);
}

int mlx5_ec_init(struct mlx5_core_dev *dev)
{
	int err = 0;

	if (!mlx5_core_is_ecpf(dev))
		return 0;

	/* ECPF shall enable HCA for peer PF in the same way a PF
	 * does this for its VFs.
	 */
	err = mlx5_peer_pf_init(dev);
	if (err)
		return err;

	return 0;
}

void mlx5_ec_cleanup(struct mlx5_core_dev *dev)
{
	if (!mlx5_core_is_ecpf(dev))
		return;

	mlx5_peer_pf_cleanup(dev);
}

static ssize_t max_tx_rate_store(struct kobject *kobj,
				 struct kobj_attribute *attr,
				 const char *buf,
				 size_t count)
{
	struct mlx5_smart_nic_vport *tmp =
		container_of(kobj, struct mlx5_smart_nic_vport, kobj);
	struct mlx5_eswitch *esw = tmp->esw;
	u32 max_tx_rate;
	u32 min_tx_rate;
	int err;

	mutex_lock(&esw->state_lock);
	min_tx_rate = esw->vports[0].info.min_rate;
	mutex_unlock(&esw->state_lock);

	err = kstrtou32(buf, 0, &max_tx_rate);
	if (err)
		return err;

	err = mlx5_eswitch_set_vport_rate(esw, tmp->vport,
					  max_tx_rate, min_tx_rate);

	return err ? err : count;
}

static ssize_t max_tx_rate_show(struct kobject *kobj,
				struct kobj_attribute *attr,
				char *buf)
{
	return sprintf(buf,
		       "usage: write <Rate (Mbit/s)> to set max transmit rate\n");
}

static ssize_t mac_store(struct kobject *kobj,
			 struct kobj_attribute *attr,
			 const char *buf,
			 size_t count)
{
	struct mlx5_smart_nic_vport *tmp =
		container_of(kobj, struct mlx5_smart_nic_vport, kobj);
	struct mlx5_eswitch *esw = tmp->esw;
	u8 mac[ETH_ALEN];
	int err;

	err = sscanf(buf, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
		     &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);
	if (err == 6)
		goto set_mac;

	if (sysfs_streq(buf, "Random"))
		eth_random_addr(mac);
	else
		return -EINVAL;

set_mac:
	err = mlx5_eswitch_set_vport_mac(esw, tmp->vport, mac);
	return err ? err : count;
}

static ssize_t mac_show(struct kobject *kobj,
			struct kobj_attribute *attr,
			char *buf)
{
	return sprintf(buf,
		       "usage: write <LLADDR|Random> to set Mac Address\n");
}

static int strpolicy(const char *buf, enum port_state_policy *policy)
{
	if (sysfs_streq(buf, "Down")) {
		*policy = MLX5_POLICY_DOWN;
		return 0;
	}

	if (sysfs_streq(buf, "Up")) {
		*policy = MLX5_POLICY_UP;
		return 0;
	}

	if (sysfs_streq(buf, "Follow")) {
		*policy = MLX5_POLICY_FOLLOW;
		return 0;
	}
	return -EINVAL;
}

static ssize_t vport_state_store(struct kobject *kobj,
				 struct kobj_attribute *attr,
				 const char *buf,
				 size_t count)
{
	struct mlx5_smart_nic_vport *tmp =
		container_of(kobj, struct mlx5_smart_nic_vport, kobj);
	struct mlx5_eswitch *esw = tmp->esw;
	enum port_state_policy policy;
	int err;

	err = strpolicy(buf, &policy);
	if (err)
		return err;

	err = mlx5_eswitch_set_vport_state(esw, tmp->vport, policy);
	return err ? err : count;
}

static ssize_t vport_state_show(struct kobject *kobj,
				struct kobj_attribute *attr,
				char *buf)
{
	return sprintf(buf, "usage: write <Up|Down|Follow> to set VF State\n");
}

static const char *policy_str(enum port_state_policy policy)
{
	switch (policy) {
	case MLX5_POLICY_DOWN:		return "Down\n";
	case MLX5_POLICY_UP:		return "Up\n";
	case MLX5_POLICY_FOLLOW:	return "Follow\n";
	default:			return "Invalid policy\n";
	}
}

#define _sprintf(p, buf, format, arg...)                               \
       ((PAGE_SIZE - (int)(p - buf)) <= 0 ? 0 :                        \
       scnprintf(p, PAGE_SIZE - (int)(p - buf), format, ## arg))

static ssize_t config_show(struct kobject *kobj,
			   struct kobj_attribute *attr,
			   char *buf)
{
	struct mlx5_smart_nic_vport *tmp =
		container_of(kobj, struct mlx5_smart_nic_vport, kobj);
	struct mlx5_eswitch *esw = tmp->esw;
	struct mlx5_vport_info *ivi;
	int vport = tmp->vport;
	char *p = buf;

	mutex_lock(&esw->state_lock);
	ivi = &esw->vports[vport].info;
	p += _sprintf(p, buf, "MAC        : %pM\n", ivi->mac);
	p += _sprintf(p, buf, "MaxTxRate  : %d\n", ivi->max_rate);
	p += _sprintf(p, buf, "State      : %s\n", policy_str(ivi->link_state));
	mutex_unlock(&esw->state_lock);

	return (ssize_t)(p - buf);
}

static ssize_t smart_nic_attr_show(struct kobject *kobj,
				   struct attribute *attr, char *buf)
{
	struct kobj_attribute *kattr;
	ssize_t ret = -EIO;

	kattr = container_of(attr, struct kobj_attribute, attr);
	if (kattr->show)
		ret = kattr->show(kobj, kattr, buf);
	return ret;
}

static ssize_t smart_nic_attr_store(struct kobject *kobj,
				    struct attribute *attr,
				    const char *buf, size_t count)
{
	struct kobj_attribute *kattr;
	ssize_t ret = -EIO;

	kattr = container_of(attr, struct kobj_attribute, attr);
	if (kattr->store)
		ret = kattr->store(kobj, kattr, buf, count);
	return ret;
}

static struct kobj_attribute attr_max_tx_rate = {
	.attr = {.name = "max_tx_rate",
		 .mode = 0644 },
	.show = max_tx_rate_show,
	.store = max_tx_rate_store,
};

static struct kobj_attribute attr_mac = {
	.attr = {.name = "mac",
		 .mode = 0644 },
	.show = mac_show,
	.store = mac_store,
};

static struct kobj_attribute attr_vport_state = {
	.attr = {.name = "vport_state",
		 .mode = 0644 },
	.show = vport_state_show,
	.store = vport_state_store,
};

static struct kobj_attribute attr_config = {
	.attr = {.name = "config",
		 .mode = 0444 },
	.show = config_show,
};

static struct attribute *smart_nic_attrs[] = {
	&attr_config.attr,
	&attr_max_tx_rate.attr,
	&attr_mac.attr,
	&attr_vport_state.attr,
	NULL,
};

static const struct sysfs_ops smart_nic_sysfs_ops = {
	.show   = smart_nic_attr_show,
	.store  = smart_nic_attr_store
};

static struct kobj_type smart_nic_type = {
	.sysfs_ops     = &smart_nic_sysfs_ops,
	.default_attrs = smart_nic_attrs
};

void mlx5_smartnic_sysfs_init(struct net_device *dev)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5_core_dev *mdev = priv->mdev;
	struct mlx5_smart_nic_vport *tmp;
	struct mlx5_eswitch *esw;
	int num_vports;
	int err;
	int i;

	if (!mlx5_core_is_ecpf(mdev))
		return;

	esw = mdev->priv.eswitch;
	esw->smart_nic_sysfs.kobj =
		kobject_create_and_add("smart_nic", &dev->dev.kobj);
	if (!esw->smart_nic_sysfs.kobj)
		return;

	num_vports = mlx5_core_max_vfs(mdev) + 1;
	esw->smart_nic_sysfs.vport =
		kcalloc(num_vports, sizeof(struct mlx5_smart_nic_vport),
			GFP_KERNEL);
	if (!esw->smart_nic_sysfs.vport)
		goto err_attr_mem;

	for (i = 0; i < num_vports; i++) {
		tmp = &esw->smart_nic_sysfs.vport[i];
		tmp->esw = esw;
		tmp->vport = i;
		if (i == 0)
			err = kobject_init_and_add(&tmp->kobj, &smart_nic_type,
						   esw->smart_nic_sysfs.kobj,
						   "pf");
		else
			err = kobject_init_and_add(&tmp->kobj, &smart_nic_type,
						   esw->smart_nic_sysfs.kobj,
						   "vf%d", i - 1);
		if (err)
			goto err_attr;
	}

	return;

err_attr:
	for (; i >= 0;	i--)
		kobject_put(&esw->smart_nic_sysfs.vport[i].kobj);
	kfree(esw->smart_nic_sysfs.vport);
	esw->smart_nic_sysfs.vport = NULL;

err_attr_mem:
	kobject_put(esw->smart_nic_sysfs.kobj);
	esw->smart_nic_sysfs.kobj = NULL;
}

void mlx5_smartnic_sysfs_cleanup(struct net_device *dev)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5_core_dev *mdev = priv->mdev;
	struct mlx5_smart_nic_vport *tmp;
	struct mlx5_eswitch *esw;
	int i;

	if (!mlx5_core_is_ecpf(mdev))
		return;

	esw = mdev->priv.eswitch;

	if (!esw->smart_nic_sysfs.kobj || !esw->smart_nic_sysfs.vport)
		return;

	for  (i = 0; i < mlx5_core_max_vfs(mdev); i++) {
		tmp = &esw->smart_nic_sysfs.vport[i];
		kobject_put(&tmp->kobj);
	}

	kfree(esw->smart_nic_sysfs.vport);
	esw->smart_nic_sysfs.vport = NULL;

	kobject_put(esw->smart_nic_sysfs.kobj);
	esw->smart_nic_sysfs.kobj = NULL;
}
