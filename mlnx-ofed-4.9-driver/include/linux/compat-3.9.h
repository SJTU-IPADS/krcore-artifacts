#ifndef LINUX_3_9_COMPAT_H
#define LINUX_3_9_COMPAT_H

#include <linux/version.h>

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,9,0))

#include <linux/netdevice.h>
#include <linux/skbuff.h>

#ifndef CONFIG_COMPAT_NETIF_HAS_PICK_TX

#define __netdev_pick_tx LINUX_BACKPORT(__netdev_pick_tx)
u16 __netdev_pick_tx(struct net_device *dev, struct sk_buff *skb);

#endif /* CONFIG_COMPAT_NETIF_HAS_PICK_TX */

#if NR_CPUS < 64
#define MAX_XPS_CPUS		NR_CPUS
#else
#define MAX_XPS_CPUS		64
#endif /* NR_CPUS */
#define mlx4_en_hashrnd 0xd631614b
#define MAX_XPS_BUFFER_SIZE (DIV_ROUND_UP(MAX_XPS_CPUS, 32) * 9)

#ifndef HAVE_NETIF_SET_XPS_QUEUE

struct mlx4_en_netq_attribute {
	struct attribute attr;
	ssize_t (*show)(struct netdev_queue *queue,
			struct mlx4_en_netq_attribute *attr, char *buf);
	ssize_t (*store)(struct netdev_queue *queue,
			 struct mlx4_en_netq_attribute *attr, const char *buf,
			 size_t len);
};

#define to_netq_attr(_attr) container_of(_attr,				\
	struct mlx4_en_netq_attribute, attr)

#define netif_set_xps_queue LINUX_BACKPORT(netif_set_xps_queue)
int netif_set_xps_queue(struct net_device *dev, struct cpumask *msk, u16 idx);

#endif /* HAVE_NETIF_SET_XPS_QUEUE */
#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(3,9,0)) */

#endif /* LINUX_3_9_COMPAT_H */
