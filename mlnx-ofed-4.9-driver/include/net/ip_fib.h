#ifndef _COMPAT_NET_IP_FIB_H
#define _COMPAT_NET_IP_FIB_H 1

#include_next <net/ip_fib.h>

#include "../../compat/config.h"

#if !defined(HAVE_FIB_LOOKUP_EXPORTED) && defined(CONFIG_COMPAT_IS_FIB_LOOKUP_STATIC_AND_EXTERN)
#define fib_lookup LINUX_BACKPORT(fib_lookup)
#ifdef HAVE_FLOWI_AF_SPECIFIC_INSTANCES
int fib_lookup(struct net *net, struct flowi4 *flp, struct fib_result *res)
{
	struct fib_lookup_arg arg = {
		.result = res,
		.flags = FIB_LOOKUP_NOREF,
	};
	int err;

	err = fib_rules_lookup(net->ipv4.rules_ops, flowi4_to_flowi(flp), 0, &arg);
	res->r = arg.rule;

	return err;
}
#else

#define TABLE_LOCAL_INDEX       0
#define TABLE_MAIN_INDEX        1
struct fib_table *fib_get_table(struct net *net, u32 id)
{
        struct hlist_head *ptr;

        ptr = id == RT_TABLE_LOCAL ?
                &net->ipv4.fib_table_hash[TABLE_LOCAL_INDEX] :
                &net->ipv4.fib_table_hash[TABLE_MAIN_INDEX];
        return hlist_entry(ptr->first, struct fib_table, tb_hlist);
}

inline struct fib_table *fib_new_table(struct net *net, u32 id)
{
        return fib_get_table(net, id);
}

int fib_lookup(struct net *net, const struct flowi *flp,
                             struct fib_result *res)
{
        struct fib_table *table;
        int err;

        table = fib_get_table(net, RT_TABLE_LOCAL);
        if (!table->tb_lookup(table, flp, res))
                return 0;

        table = fib_get_table(net, RT_TABLE_MAIN);
        err = table->tb_lookup(table, flp, res);
        if (err <= 0 && err != -EAGAIN)
                return err;

        return -ENETUNREACH;
}

#endif
#endif
#endif	/* _COMPAT_NET_IP_FIB_H */
