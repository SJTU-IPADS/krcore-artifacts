/*
 * Copyright (c) 2018,  Mellanox Technologies. All rights reserved.
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

#include <linux/string.h>
#include <rdma/ib_sync_mem.h>
#include <rdma/ib_verbs.h>
#include <rdma/ib_umem.h>

/* global sync memory client */

static LIST_HEAD(rsync_head);
static DEFINE_SPINLOCK(rsync_lock);

/*
 *  Since this is going to a RCU mechanism, allow multiple registrations
 *  For the Nvidia GPU case, there is only a single registration that happens
 *  at module load time.
 */

void *ib_register_sync_memory_client(const struct sync_memory_client *sync_client,
				     int flag)
{
	struct ib_sync_memory_client *ib_sync_client;

	if (!sync_client->rsync) {
		pr_warn("Sync memory client: rsync callback is not defined\n");
		return NULL;
	}

	if (sizeof(struct sync_memory_client) != sync_client->size) {
		pr_warn("Sync memory client: wrong sync client size\n");
		return NULL;
	}

	ib_sync_client = kzalloc(sizeof(*ib_sync_client), GFP_KERNEL);
	if (!ib_sync_client) {
		pr_warn("Sync memory client: failed to allocate ib sync client\n");
		return NULL;
	}

	ib_sync_client->sync_mem = sync_client;

	spin_lock(&rsync_lock);
	list_add_rcu(&ib_sync_client->node, &rsync_head);
	spin_unlock(&rsync_lock);
	return (void *)ib_sync_client;
}
EXPORT_SYMBOL(ib_register_sync_memory_client);

int ib_unregister_sync_memory_client(void *reg_handle)
{
	struct ib_sync_memory_client *ib_sync_client;

	spin_lock(&rsync_lock);
	list_for_each_entry(ib_sync_client, &rsync_head, node) {
		if (reg_handle == ib_sync_client) {
			list_del_rcu(&ib_sync_client->node);
			spin_unlock(&rsync_lock);
			synchronize_rcu();
			kfree(ib_sync_client);
			return 0;
		}
	}
	spin_unlock(&rsync_lock);
	return -EINVAL;
}
EXPORT_SYMBOL(ib_unregister_sync_memory_client);

void *ib_invoke_sync_clients(struct mm_struct *mm, unsigned long addr,
			     size_t size)
{
	struct ib_sync_memory_client *ib_sync_client;

	rcu_read_lock();
	list_for_each_entry_rcu(ib_sync_client, &rsync_head, node) {
		ib_sync_client->sync_mem->rsync(ib_sync_client, mm, addr, size);
	}
	rcu_read_unlock();
	return NULL;
}
