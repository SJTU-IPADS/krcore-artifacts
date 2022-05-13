// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2019 Mellanox Technologies. */

#include <linux/workqueue.h>
#include <linux/spinlock.h>
#include <linux/rhashtable.h>
#include <net/netfilter/nf_conntrack_core.h>
#include <linux/netfilter/nf_conntrack_common.h>

#include "miniflow.h"

#ifdef HAVE_MINIFLOW

static unsigned int offloaded_ct_timeout = 30;
module_param(offloaded_ct_timeout, int, 0644);

static atomic_t offloaded_flow_cnt = ATOMIC_INIT(0);

struct flow_offload_table {
	struct rhashtable        rhashtable;
	spinlock_t               ht_lock;
	struct delayed_work      gc_work;
	struct workqueue_struct *flow_wq;
};

enum flow_offload_tuple_dir {
	FLOW_OFFLOAD_DIR_ORIGINAL = IP_CT_DIR_ORIGINAL,
	FLOW_OFFLOAD_DIR_REPLY    = IP_CT_DIR_REPLY,
	FLOW_OFFLOAD_DIR_MAX      = IP_CT_DIR_MAX
};

struct flow_offload_tuple_rhash {
	struct rhash_head           node;
	struct nf_conntrack_tuple   tuple;
	struct nf_conntrack_zone    zone;
};

#define FLOW_OFFLOAD_DYING      0x4
#define FLOW_OFFLOAD_TEARDOWN   0x8

struct flow_offload {
	struct flow_offload_tuple_rhash tuplehash[FLOW_OFFLOAD_DIR_MAX];
	u32 flags;
	u64 timeout;
};

struct flow_offload_entry {
	struct flow_offload   flow;
	struct nf_conn        *ct;
	struct rcu_head       rcu_head;
	struct list_head      deps;
	spinlock_t            dep_lock;
};

static struct flow_offload_table __rcu *_flowtable;

static void
flow_offload_fill_dir(struct flow_offload *flow,
		      struct nf_conn *ct,
		      int zone_id,
		      enum flow_offload_tuple_dir dir)
{
	flow->tuplehash[dir].tuple = ct->tuplehash[dir].tuple;
	flow->tuplehash[dir].tuple.dst.dir = dir;
	flow->tuplehash[dir].zone.id = zone_id;
}

static struct flow_offload *
flow_offload_alloc(struct nf_conn *ct, int zone_id)
{
	struct flow_offload_entry *entry;
	struct flow_offload *flow;

	if (unlikely(nf_ct_is_dying(ct) ||
		     !atomic_inc_not_zero(&ct->ct_general.use)))
		return ERR_PTR(-EINVAL);

	entry = kzalloc((sizeof(*entry)), GFP_ATOMIC);
	if (!entry)
		goto err_alloc;

	flow = &entry->flow;
	entry->ct = ct;
	INIT_LIST_HEAD(&entry->deps);
	spin_lock_init(&entry->dep_lock);

	flow_offload_fill_dir(flow, ct, zone_id, FLOW_OFFLOAD_DIR_ORIGINAL);
	flow_offload_fill_dir(flow, ct, zone_id, FLOW_OFFLOAD_DIR_REPLY);

	return flow;

err_alloc:
	nf_ct_put(ct);

	return ERR_PTR(-ENOMEM);
}

static void flow_offload_fixup_tcp(struct ip_ct_tcp *tcp)
{
	tcp->state = TCP_CONNTRACK_ESTABLISHED;
	tcp->seen[0].td_maxwin = 0;
	tcp->seen[1].td_maxwin = 0;
}

#define NF_FLOWTABLE_TCP_PICKUP_TIMEOUT        (120 * HZ)
#define NF_FLOWTABLE_UDP_PICKUP_TIMEOUT        (30 * HZ)

#define _nfct_time_stamp ((u32)(jiffies))

static void flow_offload_fixup_ct_state(struct nf_conn *ct)
{
	const struct nf_conntrack_l4proto *l4proto;
	unsigned int timeout;
	int l4num;

	l4num = nf_ct_protonum(ct);
	if (l4num == IPPROTO_TCP)
		flow_offload_fixup_tcp(&ct->proto.tcp);

	l4proto = __nf_ct_l4proto_find(l4num);
	if (!l4proto)
		return;

	if (l4num == IPPROTO_TCP)
		timeout = NF_FLOWTABLE_TCP_PICKUP_TIMEOUT;
	else if (l4num == IPPROTO_UDP)
		timeout = NF_FLOWTABLE_UDP_PICKUP_TIMEOUT;
	else
		return;

	ct->timeout = _nfct_time_stamp + timeout;
}

static void flow_offload_free(struct flow_offload *flow)
{
	struct flow_offload_entry *e;

	e = container_of(flow, struct flow_offload_entry, flow);
	if (flow->flags & FLOW_OFFLOAD_DYING)
		nf_ct_delete(e->ct, 0, 0);
	nf_ct_put(e->ct);
	kfree_rcu(e, rcu_head);
}

static u32 _flow_offload_hash(const void *data, u32 len, u32 seed)
{
	const struct nf_conntrack_tuple *tuple = data;
	unsigned int n;

	n = (sizeof(tuple->src) + sizeof(tuple->dst.u3)) / sizeof(u32);

	/* reuse nf_conntrack hash method */
	return jhash2((u32 *)tuple, n, seed ^
			(((__force __u16)tuple->dst.u.all << 16) |
			 tuple->dst.protonum));
}

static u32 _flow_offload_hash_obj(const void *data, u32 len, u32 seed)
{
	const struct flow_offload_tuple_rhash *tuplehash = data;
	unsigned int n;

	n = (sizeof(tuplehash->tuple.src) +
	     sizeof(tuplehash->tuple.dst.u3)) / sizeof(u32);

	return jhash2((u32 *)&tuplehash->tuple, n, seed ^
			(((__force __u16)tuplehash->tuple.dst.u.all << 16) |
			 tuplehash->tuple.dst.protonum));
}

static int _flow_offload_hash_cmp(struct rhashtable_compare_arg *arg,
				  const void *ptr)
{
	const struct flow_offload_tuple_rhash *x = ptr;
	struct flow_offload_tuple_rhash *thash;

	thash = container_of(arg->key, struct flow_offload_tuple_rhash, tuple);

	if (memcmp(&x->tuple, &thash->tuple,
		   offsetof(struct nf_conntrack_tuple, dst.dir)) ||
	    x->zone.id != thash->zone.id)
		return 1;

	return 0;
}

static const struct rhashtable_params rhash_params = {
	.head_offset            = offsetof(struct flow_offload_tuple_rhash,
					   node),
	.hashfn                 = _flow_offload_hash,
	.obj_hashfn             = _flow_offload_hash_obj,
	.obj_cmpfn              = _flow_offload_hash_cmp,
	.automatic_shrinking    = true,
};

static int
flow_offload_add(struct flow_offload_table *flow_table,
		 struct flow_offload *flow)
{
	int ret;

	assert_spin_locked(&flow_table->ht_lock);

	ret = rhashtable_insert_fast(&flow_table->rhashtable,
				     &flow->tuplehash[FLOW_OFFLOAD_DIR_ORIGINAL].node,
				     rhash_params);
	if (ret)
		return ret;

	ret = rhashtable_insert_fast(&flow_table->rhashtable,
				     &flow->tuplehash[FLOW_OFFLOAD_DIR_REPLY].node,
				     rhash_params);
	if (ret)
		rhashtable_remove_fast(&flow_table->rhashtable,
				       &flow->tuplehash[FLOW_OFFLOAD_DIR_ORIGINAL].node,
				       rhash_params);

	return ret;
}

static void
flow_offload_del(struct flow_offload_table *flow_table,
		 struct flow_offload *flow)
{
	struct flow_offload_entry *e;

	flow->flags |= FLOW_OFFLOAD_TEARDOWN;

	spin_lock(&flow_table->ht_lock);
	rhashtable_remove_fast(&flow_table->rhashtable,
			       &flow->tuplehash[FLOW_OFFLOAD_DIR_ORIGINAL].node,
			       rhash_params);
	rhashtable_remove_fast(&flow_table->rhashtable,
			       &flow->tuplehash[FLOW_OFFLOAD_DIR_REPLY].node,
			       rhash_params);
	spin_unlock(&flow_table->ht_lock);

	e = container_of(flow, struct flow_offload_entry, flow);
	clear_bit(IPS_OFFLOAD_BIT, &e->ct->status);
	atomic_dec(&offloaded_flow_cnt);
	flow_offload_fixup_ct_state(e->ct);

	spin_lock(&e->dep_lock);
	ct_flow_offload_destroy(&e->deps);
	spin_unlock(&e->dep_lock);

	flow_offload_free(flow);
}

static struct flow_offload_tuple_rhash *
flow_offload_lookup(struct flow_offload_table *flow_table,
		    const struct nf_conntrack_zone *zone,
		    const struct nf_conntrack_tuple *tuple)
{
	struct flow_offload_tuple_rhash key, *res;
	struct flow_offload *flow;
	int dir;

	assert_spin_locked(&flow_table->ht_lock);

	key.tuple = *tuple;
	key.zone  = *zone;

	res = rhashtable_lookup_fast(&flow_table->rhashtable, &key.tuple,
				     rhash_params);
	if (!res)
		return NULL;

	dir = res->tuple.dst.dir;
	flow = container_of(res, struct flow_offload, tuplehash[dir]);
	if (flow->flags & (FLOW_OFFLOAD_DYING | FLOW_OFFLOAD_TEARDOWN))
		return NULL;

	return res;
}

static int
flow_offload_table_iterate(struct flow_offload_table *flow_table,
			   void (*iter)(struct flow_offload *flow, void *data),
			   void *data)
{
	struct flow_offload_tuple_rhash *tuplehash;
	struct rhashtable_iter hti;
	struct flow_offload *flow;
	int err = 0;

	rhashtable_walk_enter(&flow_table->rhashtable, &hti);
	rhashtable_walk_start(&hti);

	while ((tuplehash = rhashtable_walk_next(&hti))) {
		if (IS_ERR(tuplehash)) {
			if (PTR_ERR(tuplehash) != -EAGAIN) {
				err = PTR_ERR(tuplehash);
				break;
			}
			continue;
		}
		if (tuplehash->tuple.dst.dir)
			continue;

		flow = container_of(tuplehash, struct flow_offload,
				    tuplehash[0]);
		iter(flow, data);
	}

	rhashtable_walk_stop(&hti);
	rhashtable_walk_exit(&hti);

	return err;
}

static inline bool
nf_flow_has_expired(const struct flow_offload *flow)
{
	return (__s32)(flow->timeout - (u32)jiffies) <= 0;
}

static void
flow_offload_teardown(struct flow_offload_table *flowtable,
		      struct flow_offload *flow)
{
	if (flow->flags & FLOW_OFFLOAD_TEARDOWN)
		return;

	flow->flags |= FLOW_OFFLOAD_TEARDOWN;
}

static void nf_flow_offload_gc_step(struct flow_offload *flow, void *data)
{
	struct flow_offload_table *flow_table = data;
	struct flow_offload_entry *e;
	unsigned int timeout = offloaded_ct_timeout * HZ;
	u64 lastuse;

	e = container_of(flow, struct flow_offload_entry, flow);

	spin_lock(&e->dep_lock);
	ct_flow_offload_get_stats(&e->deps, &lastuse);
	spin_unlock(&e->dep_lock);

	if (flow->timeout < (lastuse + timeout))
		flow->timeout = lastuse + timeout;

	if (nf_flow_has_expired(flow) ||
	    (flow->flags & (FLOW_OFFLOAD_DYING | FLOW_OFFLOAD_TEARDOWN)))
		flow_offload_del(flow_table, flow);
}

static void flow_offload_work_gc(struct work_struct *gc_work)
{
	struct flow_offload_table *flow_table;

	flow_table = container_of(gc_work, struct flow_offload_table,
				  gc_work.work);
	flow_offload_table_iterate(flow_table, nf_flow_offload_gc_step,
				   flow_table);
	queue_delayed_work(flow_table->flow_wq, &flow_table->gc_work, HZ);
}

static int flow_offload_table_init(struct flow_offload_table *flowtable)
{
	int err;

	spin_lock_init(&flowtable->ht_lock);
	INIT_DELAYED_WORK(&flowtable->gc_work, flow_offload_work_gc);

	flowtable->flow_wq = alloc_workqueue("mlx5_ct_flow_offload",
					     WQ_MEM_RECLAIM | WQ_UNBOUND |
					     WQ_SYSFS,
					     1);
	if (!flowtable->flow_wq)
		return -ENOMEM;

	err = rhashtable_init(&flowtable->rhashtable, &rhash_params);
	if (err) {
		destroy_workqueue(flowtable->flow_wq);
		return err;
	}

	queue_delayed_work(flowtable->flow_wq, &flowtable->gc_work, HZ);

	return 0;
}

static void nf_flow_table_do_cleanup(struct flow_offload *flow, void *data)
{
	flow_offload_teardown((struct flow_offload_table *)data, flow);
}

static void flow_offload_table_free(struct flow_offload_table *flowtable)
{
	cancel_delayed_work_sync(&flowtable->gc_work);
	flow_offload_table_iterate(flowtable, nf_flow_table_do_cleanup,
				   flowtable);
	flow_offload_table_iterate(flowtable, nf_flow_offload_gc_step,
				   flowtable);
	destroy_workqueue(flowtable->flow_wq);
	rhashtable_destroy(&flowtable->rhashtable);
}

static int _flowtable_add_entry(const struct net *net, int zone_id,
				struct nf_conn *ct,
				struct flow_offload **ret_flow)
{
	struct flow_offload_table *flowtable;
	struct flow_offload *flow;
	int ret = -ENOENT;

	flow = flow_offload_alloc(ct, zone_id);
	if (IS_ERR(flow)) {
		ret = PTR_ERR(flow);
		goto err_flow_alloc;
	}

	rcu_read_lock();
	flowtable = rcu_dereference(_flowtable);
	if (!flowtable)
		goto err_flow_add;

	set_bit(IPS_OFFLOAD_BIT, &ct->status);

	ret = flow_offload_add(flowtable, flow);
	if (ret)
		goto err_flow_add;

	if (ret_flow)
		*ret_flow = flow;

	rcu_read_unlock();

	atomic_inc(&offloaded_flow_cnt);

	return ret;

err_flow_add:
	rcu_read_unlock();
	flow_offload_free(flow);
err_flow_alloc:
	clear_bit(IPS_OFFLOAD_BIT, &ct->status);

	return ret;
}

static inline struct flow_offload_tuple_rhash *
_flowtable_lookup(const struct nf_conntrack_zone *zone,
		  const struct nf_conntrack_tuple *tuple)
{
	struct flow_offload_table *flowtable;
	struct flow_offload_tuple_rhash *tuplehash = NULL;

	rcu_read_lock();

	flowtable = rcu_dereference(_flowtable);
	if (flowtable)
		tuplehash = flow_offload_lookup(flowtable, zone, tuple);

	rcu_read_unlock();

	return tuplehash;
}

int mlx5_ct_flow_offload_add(const struct net *net,
			     const struct nf_conntrack_zone *zone,
			     const struct nf_conntrack_tuple *tuple,
			     struct mlx5e_tc_flow *tc_flow)
{
	struct flow_offload_tuple_rhash *fhash;
	struct flow_offload_table *flowtable;
	struct flow_offload_entry *entry;
	enum flow_offload_tuple_dir dir;
	unsigned int timeout = offloaded_ct_timeout * HZ;
	int err = 0;

	if (!rcu_access_pointer(_flowtable))
		return -ENOENT;

	rcu_read_lock();
	flowtable = rcu_dereference(_flowtable);

	/* _flowtable_lookup and _flowtable_add_entry should be locked together */
	spin_lock(&flowtable->ht_lock);
	fhash = _flowtable_lookup(zone, tuple);
	if (fhash) {
		dir = fhash->tuple.dst.dir;
		entry = container_of(fhash, struct flow_offload_entry,
				     flow.tuplehash[dir]);
		// TODO take refcount on ct?
	} else {
		struct nf_conntrack_tuple_hash *thash;
		struct flow_offload *flow;
		struct nf_conn *ct;

		thash = nf_conntrack_find_get((struct net *)net, zone, tuple);
		if (!thash) {
			err = -EINVAL;
			goto out;
		}

		ct = nf_ct_tuplehash_to_ctrack(thash);
		err = _flowtable_add_entry(net, zone->id, ct, &flow);
		/* reduce refcount from nf_conntrack_find_get
		 * we also got refcount from adding the entry
		 */
		nf_ct_put(ct);
		if (err)
			goto out;

		entry = container_of(flow, struct flow_offload_entry, flow);
	}
	spin_unlock(&flowtable->ht_lock);
	rcu_read_unlock();

	spin_lock(&entry->dep_lock);
	if (entry->flow.flags & (FLOW_OFFLOAD_DYING | FLOW_OFFLOAD_TEARDOWN)) {
		spin_unlock(&entry->dep_lock);
		err = -EAGAIN;
		goto err_flow;
	}
	tc_flow->dep_lock = &entry->dep_lock;
	ct_flow_offload_add(tc_flow, &entry->deps);
	spin_unlock(&entry->dep_lock);

	entry->flow.timeout = jiffies + timeout;

	return 0;

out:
	spin_unlock(&flowtable->ht_lock);
	rcu_read_unlock();
err_flow:
	return err;
}

int mlx5_ct_flow_offload_table_init(void)
{
	struct flow_offload_table *flowtable;
	int err;

	rcu_assign_pointer(_flowtable, NULL);
	flowtable = kzalloc(sizeof(*flowtable), GFP_KERNEL);
	if (!flowtable)
		return -ENOMEM;

	err = flow_offload_table_init(flowtable);
	if (err) {
		kfree(flowtable);
		return err;
	}

	rcu_assign_pointer(_flowtable, flowtable);

	return 0;
}

void mlx5_ct_flow_offload_table_destroy(void)
{
	struct flow_offload_table *flowtable;

	flowtable = rcu_access_pointer(_flowtable);
	if (flowtable) {
		rcu_assign_pointer(_flowtable, NULL);
		synchronize_rcu();
		flow_offload_table_free(flowtable);
		kfree(flowtable);
	}
}

int mlx5_ct_flow_offloaded_count(void)
{
	return atomic_read(&offloaded_flow_cnt);
}

#endif /* HAVE_MINIFLOW */
