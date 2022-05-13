#ifndef LINUX_BONDING_H
#define LINUX_BONDING_H

#include "../../compat/config.h"

#ifdef HAVE_BONDING_H
#include_next <net/bonding.h>

#define MLX_USES_PRIMARY(mode)				\
		(((mode) == BOND_MODE_ACTIVEBACKUP) ||	\
		 ((mode) == BOND_MODE_TLB)          ||	\
		 ((mode) == BOND_MODE_ALB))

#define bond_option_active_slave_get_rcu LINUX_BACKPORT(bond_option_active_slave_get_rcu)
static inline struct net_device *bond_option_active_slave_get_rcu(struct bonding
								  *bond)
{
	struct slave *slave = rcu_dereference(bond->curr_active_slave);

	return MLX_USES_PRIMARY(bond->params.mode) && slave ? slave->dev : NULL;
}

#if defined(MLX_USE_LAG_COMPAT) && defined(MLX_IMPL_LAG_EVENTS)
static struct socket *mlx_lag_compat_rtnl_sock;
static void (*mlx_lag_compat_netdev_event_cb)(unsigned long, void *);

static void mlx_lag_compat_changelowerstate_event(struct slave *slave)
{
	struct netdev_notifier_changelowerstate_info info = {};
	struct netdev_lag_lower_state_info lag_lower_info = {};

	info.info.dev         = slave->dev;
	info.lower_state_info = &lag_lower_info;

	lag_lower_info.link_up    = bond_slave_is_up(slave);

	/* Refraining here from using bond_slave_can_tx(), due to its reliance
	 * on slave->link, which at the time this function is called may still
	 * be set to BOND_LINK_DOWN. */
	lag_lower_info.tx_enabled = bond_slave_is_up(slave) &&
				    bond_is_active_slave(slave);

	mlx_lag_compat_netdev_event_cb(NETDEV_CHANGELOWERSTATE, &info);
}

static void mlx_lag_compat_changeupper_event(struct bonding *bond,
					     struct slave *slave)
{
	struct netdev_notifier_changeupper_info info = {};
	struct netdev_lag_upper_info lag_upper_info = {};

	info.info.dev   = slave->dev;
	info.linking    = !!(slave->dev->flags & IFF_SLAVE);
	info.upper_dev  = bond->dev;
	info.master     = true;

	if (info.linking) {
		info.upper_info = &lag_upper_info;

		if (bond_mode_uses_xmit_hash(bond))
			lag_upper_info.tx_type = NETDEV_LAG_TX_TYPE_HASH;
		else if (BOND_MODE(bond) == BOND_MODE_ACTIVEBACKUP)
			lag_upper_info.tx_type = NETDEV_LAG_TX_TYPE_ACTIVEBACKUP;
	}

	if (info.linking)
		mlx_lag_compat_changelowerstate_event(slave);

	mlx_lag_compat_netdev_event_cb(NETDEV_CHANGEUPPER, &info);
}

#ifndef HAVE_SK_DATA_READY_2_PARAMS
static void mlx_lag_compat_rtnl_data_ready(struct sock *sk)
#else
static void mlx_lag_compat_rtnl_data_ready(struct sock *sk, int bytes)
#endif
{
	struct net_device *ndev;
	struct ifinfomsg *ifm;
	struct nlmsghdr *nlh;
	struct sk_buff *skb;
	struct bonding *bond;
	struct slave *slave;
	int rc = 0;

	skb = skb_recv_datagram(sk, 0, 0, &rc);
	if (rc)
		return;

	nlh = (struct nlmsghdr *)skb->data;
	if (!nlh || !NLMSG_OK(nlh, skb->len) || nlh->nlmsg_type != RTM_NEWLINK)
		goto free_skb;

	if (!rtnl_is_locked())
		goto free_skb;

	ifm  = nlmsg_data(nlh);
	ndev = dev_get_by_index(&init_net, ifm->ifi_index);
	if (!ndev)
		goto free_skb;

	if (ifm->ifi_change == IFF_SLAVE) {
		rcu_read_lock();
		slave = bond_slave_get_rcu(ndev);
		rcu_read_unlock();
		bond = bond_get_bond_by_slave(slave);
		mlx_lag_compat_changeupper_event(bond, slave);
	} else if (netif_is_bond_master(ndev)) {
		struct net_device *ndev_tmp;

		for_each_netdev(&init_net, ndev_tmp) {
			rcu_read_lock();
			if (netdev_master_upper_dev_get_rcu(ndev_tmp) != ndev) {
				rcu_read_unlock();
				continue;
			}

			slave = bond_slave_get_rcu(ndev_tmp);
			rcu_read_unlock();

			mlx_lag_compat_changelowerstate_event(slave);
		}
	} else if (netif_is_bond_slave(ndev)) {
		rcu_read_lock();
		slave = bond_slave_get_rcu(ndev);
		rcu_read_unlock();

		if (!slave)
			goto put_dev;

		mlx_lag_compat_changelowerstate_event(slave);
	}

put_dev:
	dev_put(ndev);

free_skb:
	kfree_skb(skb);
}

static int mlx_lag_compat_events_open(void (*cb)(unsigned long, void *))
{
	struct sockaddr_nl addr = {
		.nl_family = AF_NETLINK,
		.nl_groups = RTNLGRP_LINK,
	};
	int err;

#if defined(HAVE_SOCK_CREATE_KERN_5_PARAMS)
	err = sock_create_kern(&init_net, PF_NETLINK, SOCK_DGRAM, NETLINK_ROUTE,
			       &mlx_lag_compat_rtnl_sock);
#else
	err = sock_create_kern(PF_NETLINK, SOCK_DGRAM, NETLINK_ROUTE,
			       &mlx_lag_compat_rtnl_sock);
#endif
	if (err) {
		pr_err("mlx: ERROR: Couldn't create netlink socket. LAG events will not be dispatched.\n");
		mlx_lag_compat_rtnl_sock = NULL;
		return err;
	}

	mlx_lag_compat_netdev_event_cb = cb;
	mlx_lag_compat_rtnl_sock->sk->sk_data_ready = mlx_lag_compat_rtnl_data_ready;
	mlx_lag_compat_rtnl_sock->sk->sk_allocation = GFP_KERNEL;

	err = kernel_bind(mlx_lag_compat_rtnl_sock, (struct sockaddr *)&addr,
			  sizeof(addr));
	if (err) {
		pr_err("mlx: ERROR: Couldn't bind netlink socket. LAG events will not be dispatched.\n");
		sock_release(mlx_lag_compat_rtnl_sock);
		mlx_lag_compat_rtnl_sock = NULL;
		return err;
	}

	return 0;
}

static void mlx_lag_compat_events_close(void)
{
	if (mlx_lag_compat_rtnl_sock)
		sock_release(mlx_lag_compat_rtnl_sock);
}
#endif

#endif /* HAVE_BONDING_H */

#endif /* LINUX_BONDING_H */
