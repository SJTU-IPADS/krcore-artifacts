/*
 * Copyright (c) 2014,  Mellanox Technologies. All rights reserved.
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

#include <rdma/ib_peer_mem.h>
#include <rdma/ib_verbs.h>
#include <rdma/ib_umem.h>

static DEFINE_MUTEX(peer_memory_mutex);
static LIST_HEAD(peer_memory_list);
static struct kobject *peers_kobj;

static void complete_peer(struct kref *kref);
static struct ib_peer_memory_client *get_peer_by_kobj(void *kobj);
static ssize_t version_show(struct kobject *kobj,
			    struct kobj_attribute *attr, char *buf)
{
	struct ib_peer_memory_client *ib_peer_client = get_peer_by_kobj(kobj);

	if (ib_peer_client) {
		sprintf(buf, "%s\n", ib_peer_client->peer_mem->version);
		kref_put(&ib_peer_client->ref, complete_peer);
		return strlen(buf);
	}
	/* not found - nothing is return */
	return 0;
}

static ssize_t num_alloc_mrs_show(struct kobject *kobj,
				  struct kobj_attribute *attr, char *buf)
{
	struct ib_peer_memory_client *ib_peer_client = get_peer_by_kobj(kobj);

	if (ib_peer_client) {
		sprintf(buf, "%llu\n", (u64)atomic64_read(&ib_peer_client->stats.num_alloc_mrs));
		kref_put(&ib_peer_client->ref, complete_peer);
		return strlen(buf);
	}
	/* not found - nothing is return */
	return 0;
}

static ssize_t num_dealloc_mrs_show(struct kobject *kobj,
				    struct kobj_attribute *attr, char *buf)
{
	struct ib_peer_memory_client *ib_peer_client = get_peer_by_kobj(kobj);

	if (ib_peer_client) {
		sprintf(buf, "%llu\n", (u64)atomic64_read(&ib_peer_client->stats.num_dealloc_mrs));
		kref_put(&ib_peer_client->ref, complete_peer);
		return strlen(buf);
	}
	/* not found - nothing is return */
	return 0;
}

static ssize_t num_reg_pages_show(struct kobject *kobj,
				  struct kobj_attribute *attr, char *buf)
{
	struct ib_peer_memory_client *ib_peer_client = get_peer_by_kobj(kobj);

	if (ib_peer_client) {
		sprintf(buf, "%llu\n", (u64)atomic64_read(&ib_peer_client->stats.num_reg_pages));
		kref_put(&ib_peer_client->ref, complete_peer);
		return strlen(buf);
	}
	/* not found - nothing is return */
	return 0;
}

static ssize_t num_dereg_pages_show(struct kobject *kobj,
				    struct kobj_attribute *attr, char *buf)
{
	struct ib_peer_memory_client *ib_peer_client = get_peer_by_kobj(kobj);

	if (ib_peer_client) {
		sprintf(buf, "%llu\n", (u64)atomic64_read(&ib_peer_client->stats.num_dereg_pages));
		kref_put(&ib_peer_client->ref, complete_peer);
		return strlen(buf);
	}
	/* not found - nothing is return */
	return 0;
}

static ssize_t num_reg_bytes_show(struct kobject *kobj,
				  struct kobj_attribute *attr, char *buf)
{
	struct ib_peer_memory_client *ib_peer_client = get_peer_by_kobj(kobj);

	if (ib_peer_client) {
		sprintf(buf, "%llu\n", (u64)atomic64_read(&ib_peer_client->stats.num_reg_bytes));
		kref_put(&ib_peer_client->ref, complete_peer);
		return strlen(buf);
	}
	/* not found - nothing is return */
	return 0;
}

static ssize_t num_dereg_bytes_show(struct kobject *kobj,
				    struct kobj_attribute *attr, char *buf)
{
	struct ib_peer_memory_client *ib_peer_client = get_peer_by_kobj(kobj);

	if (ib_peer_client) {
		sprintf(buf, "%llu\n", (u64)atomic64_read(&ib_peer_client->stats.num_dereg_bytes));
		kref_put(&ib_peer_client->ref, complete_peer);
		return strlen(buf);
	}
	/* not found - nothing is return */
	return 0;
}

static ssize_t num_free_callbacks_show(struct kobject *kobj,
				       struct kobj_attribute *attr, char *buf)
{
	struct ib_peer_memory_client *ib_peer_client = get_peer_by_kobj(kobj);

	if (ib_peer_client) {
		sprintf(buf, "%lu\n", ib_peer_client->stats.num_free_callbacks);
		kref_put(&ib_peer_client->ref, complete_peer);
		return strlen(buf);
	}
	/* not found - nothing is return */
	return 0;
}

static struct kobj_attribute version_attr = __ATTR_RO(version);
static struct kobj_attribute num_alloc_mrs = __ATTR_RO(num_alloc_mrs);
static struct kobj_attribute num_dealloc_mrs = __ATTR_RO(num_dealloc_mrs);
static struct kobj_attribute num_reg_pages = __ATTR_RO(num_reg_pages);
static struct kobj_attribute num_dereg_pages = __ATTR_RO(num_dereg_pages);
static struct kobj_attribute num_reg_bytes = __ATTR_RO(num_reg_bytes);
static struct kobj_attribute num_dereg_bytes = __ATTR_RO(num_dereg_bytes);
static struct kobj_attribute num_free_callbacks = __ATTR_RO(num_free_callbacks);

static struct attribute *peer_mem_attrs[] = {
			&version_attr.attr,
			&num_alloc_mrs.attr,
			&num_dealloc_mrs.attr,
			&num_reg_pages.attr,
			&num_dereg_pages.attr,
			&num_reg_bytes.attr,
			&num_dereg_bytes.attr,
			&num_free_callbacks.attr,
			NULL,
};

static void destroy_peer_sysfs(struct ib_peer_memory_client *ib_peer_client)
{
	kobject_put(ib_peer_client->kobj);
	if (list_empty(&peer_memory_list))
		kobject_put(peers_kobj);
}

static int create_peer_sysfs(struct ib_peer_memory_client *ib_peer_client)
{
	int ret;

	if (list_empty(&peer_memory_list)) {
		/* creating under /sys/kernel/mm */
		peers_kobj = kobject_create_and_add("memory_peers", mm_kobj);
		if (!peers_kobj)
			return -ENOMEM;
	}

	ib_peer_client->peer_mem_attr_group.attrs = peer_mem_attrs;
	/* Dir alreday was created explicitly to get its kernel object for further usage */
	ib_peer_client->peer_mem_attr_group.name =  NULL;
	ib_peer_client->kobj = kobject_create_and_add(ib_peer_client->peer_mem->name,
		peers_kobj);

	if (!ib_peer_client->kobj) {
		ret = -EINVAL;
		goto free;
	}

	/* Create the files associated with this kobject */
	ret = sysfs_create_group(ib_peer_client->kobj,
				 &ib_peer_client->peer_mem_attr_group);
	if (ret)
		goto peer_free;

	return 0;

peer_free:
	kobject_put(ib_peer_client->kobj);

free:
	if (list_empty(&peer_memory_list))
		kobject_put(peers_kobj);

	return ret;
}

static struct ib_peer_memory_client *get_peer_by_kobj(void *kobj)
{
	struct ib_peer_memory_client *ib_peer_client;

	mutex_lock(&peer_memory_mutex);
	list_for_each_entry(ib_peer_client, &peer_memory_list, core_peer_list) {
		if (ib_peer_client->kobj == kobj) {
			kref_get(&ib_peer_client->ref);
			goto found;
		}
	}

	ib_peer_client = NULL;
found:
	mutex_unlock(&peer_memory_mutex);
	return ib_peer_client;
}

/* Caller should be holding the peer client lock, ib_peer_client->lock */
static struct core_ticket *ib_peer_search_context(struct ib_peer_memory_client *ib_peer_client,
						  u64 key)
{
	struct core_ticket *core_ticket;

	list_for_each_entry(core_ticket, &ib_peer_client->core_ticket_list,
			    ticket_list) {
		if (core_ticket->key == key)
			return core_ticket;
	}

	return NULL;
}

static int ib_invalidate_peer_memory(void *reg_handle, u64 core_context)
{
	struct ib_peer_memory_client *ib_peer_client = reg_handle;
	struct invalidation_ctx *invalidation_ctx;
	struct core_ticket *core_ticket;
	int need_unlock = 1;

	mutex_lock(&ib_peer_client->lock);
	ib_peer_client->stats.num_free_callbacks += 1;
	core_ticket = ib_peer_search_context(ib_peer_client, core_context);
	if (!core_ticket)
		goto out;

	invalidation_ctx = (struct invalidation_ctx *)core_ticket->context;
	/* If context is not ready yet, mark it to be invalidated */
	if (!invalidation_ctx->func) {
		invalidation_ctx->peer_invalidated = 1;
		goto out;
	}
	invalidation_ctx->func(invalidation_ctx->cookie,
					invalidation_ctx->umem, 0, 0);
	if (invalidation_ctx->inflight_invalidation) {
		/* init the completion to wait on before letting other thread to run */
		init_completion(&invalidation_ctx->comp);
		mutex_unlock(&ib_peer_client->lock);
		need_unlock = 0;
		wait_for_completion(&invalidation_ctx->comp);
	}

	kfree(invalidation_ctx);
out:
	if (need_unlock)
		mutex_unlock(&ib_peer_client->lock);

	return 0;
}

static int ib_peer_insert_context(struct ib_peer_memory_client *ib_peer_client,
				  void *context,
				  u64 *context_ticket)
{
	struct core_ticket *core_ticket = kzalloc(sizeof(*core_ticket), GFP_KERNEL);

	if (!core_ticket)
		return -ENOMEM;

	mutex_lock(&ib_peer_client->lock);
	core_ticket->key = ib_peer_client->last_ticket++;
	core_ticket->context = context;
	list_add_tail(&core_ticket->ticket_list,
		      &ib_peer_client->core_ticket_list);
	*context_ticket = core_ticket->key;
	mutex_unlock(&ib_peer_client->lock);

	return 0;
}

/* Caller should be holding the peer client lock, specifically, the caller should hold ib_peer_client->lock */
static int ib_peer_remove_context(struct ib_peer_memory_client *ib_peer_client,
				  u64 key)
{
	struct core_ticket *core_ticket;

	list_for_each_entry(core_ticket, &ib_peer_client->core_ticket_list,
			    ticket_list) {
		if (core_ticket->key == key) {
			list_del(&core_ticket->ticket_list);
			kfree(core_ticket);
			return 0;
		}
	}

	return 1;
}

/**
** ib_peer_create_invalidation_ctx - creates invalidation context for a given umem
** @ib_peer_mem: peer client to be used
** @umem: umem struct belongs to that context
** @invalidation_ctx: output context
**/
int ib_peer_create_invalidation_ctx(struct ib_peer_memory_client *ib_peer_mem, struct ib_umem *umem,
				    struct invalidation_ctx **invalidation_ctx)
{
	int ret;
	struct invalidation_ctx *ctx;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ret = ib_peer_insert_context(ib_peer_mem, ctx,
				     &ctx->context_ticket);
	if (ret) {
		kfree(ctx);
		return ret;
	}

	ctx->umem = umem;
	umem->invalidation_ctx = ctx;
	*invalidation_ctx = ctx;
	return 0;
}

/**
 * ** ib_peer_destroy_invalidation_ctx - destroy a given invalidation context
 * ** @ib_peer_mem: peer client to be used
 * ** @invalidation_ctx: context to be invalidated
 * **/
void ib_peer_destroy_invalidation_ctx(struct ib_peer_memory_client *ib_peer_mem,
				      struct invalidation_ctx *invalidation_ctx)
{
	int peer_callback;
	int inflight_invalidation;

	/* If we are under peer callback lock was already taken.*/
	if (!invalidation_ctx->peer_callback)
		mutex_lock(&ib_peer_mem->lock);
	ib_peer_remove_context(ib_peer_mem, invalidation_ctx->context_ticket);
	/* make sure to check inflight flag after took the lock and remove from tree.
	 * in addition, from that point using local variables for peer_callback and
	 * inflight_invalidation as after the complete invalidation_ctx can't be accessed
	 * any more as it may be freed by the callback.
	 */
	peer_callback = invalidation_ctx->peer_callback;
	inflight_invalidation = invalidation_ctx->inflight_invalidation;
	if (inflight_invalidation)
		complete(&invalidation_ctx->comp);

	/* On peer callback lock is handled externally */
	if (!peer_callback)
		mutex_unlock(&ib_peer_mem->lock);

	/* in case under callback context or callback is pending let it free the invalidation context */
	if (!peer_callback && !inflight_invalidation)
		kfree(invalidation_ctx);
}
static int ib_memory_peer_check_mandatory(const struct peer_memory_client
						     *peer_client)
{
#define PEER_MEM_MANDATORY_FUNC(x) { offsetof(struct peer_memory_client, x), #x }
		static const struct {
			size_t offset;
			char  *name;
		} mandatory_table[] = {
			PEER_MEM_MANDATORY_FUNC(acquire),
			PEER_MEM_MANDATORY_FUNC(get_pages),
			PEER_MEM_MANDATORY_FUNC(put_pages),
			PEER_MEM_MANDATORY_FUNC(get_page_size),
			PEER_MEM_MANDATORY_FUNC(dma_map),
			PEER_MEM_MANDATORY_FUNC(dma_unmap)
		};
		int i;

		for (i = 0; i < ARRAY_SIZE(mandatory_table); ++i) {
			if (!*(void **)((void *)peer_client + mandatory_table[i].offset)) {
				pr_err("Peer memory %s is missing mandatory function %s\n",
				       peer_client->name, mandatory_table[i].name);
				return -EINVAL;
			}
		}

		return 0;
}

static void complete_peer(struct kref *kref)
{
	struct ib_peer_memory_client *ib_peer_client =
		container_of(kref, struct ib_peer_memory_client, ref);

	complete(&ib_peer_client->unload_comp);
}

void *ib_register_peer_memory_client(const struct peer_memory_client *peer_client,
				     invalidate_peer_memory *invalidate_callback)
{
	struct ib_peer_memory_client *ib_peer_client;

	if (ib_memory_peer_check_mandatory(peer_client))
		return NULL;

	ib_peer_client = kzalloc(sizeof(*ib_peer_client), GFP_KERNEL);
	if (!ib_peer_client)
		return NULL;

	INIT_LIST_HEAD(&ib_peer_client->core_ticket_list);
	mutex_init(&ib_peer_client->lock);
	init_completion(&ib_peer_client->unload_comp);
	kref_init(&ib_peer_client->ref);
	ib_peer_client->peer_mem = peer_client;

	/* Once peer supplied a non NULL callback it's an indication that
	 * invalidation support is required for any memory owning.
	 */
	if (invalidate_callback) {
		*invalidate_callback = ib_invalidate_peer_memory;
		ib_peer_client->invalidation_required = 1;
	}
	ib_peer_client->last_ticket = 1;

	mutex_lock(&peer_memory_mutex);
	if (create_peer_sysfs(ib_peer_client)) {
		kfree(ib_peer_client);
		ib_peer_client = NULL;
		goto end;
	}
	list_add_tail(&ib_peer_client->core_peer_list, &peer_memory_list);
end:

	mutex_unlock(&peer_memory_mutex);
	return ib_peer_client;
}
EXPORT_SYMBOL(ib_register_peer_memory_client);

void ib_unregister_peer_memory_client(void *reg_handle)
{
	struct ib_peer_memory_client *ib_peer_client = reg_handle;

	mutex_lock(&peer_memory_mutex);
	list_del(&ib_peer_client->core_peer_list);
	destroy_peer_sysfs(ib_peer_client);
	mutex_unlock(&peer_memory_mutex);

	kref_put(&ib_peer_client->ref, complete_peer);
	wait_for_completion(&ib_peer_client->unload_comp);
	kfree(ib_peer_client);
}
EXPORT_SYMBOL(ib_unregister_peer_memory_client);

struct ib_peer_memory_client *ib_get_peer_client(struct ib_ucontext *context, unsigned long addr,
						 size_t size, unsigned long peer_mem_flags,
						 void **peer_client_context)
{
	struct ib_peer_memory_client *ib_peer_client;
	int ret = 0;

	mutex_lock(&peer_memory_mutex);
	list_for_each_entry(ib_peer_client, &peer_memory_list, core_peer_list) {
		/* In case peer requires invalidation it can't own
		 * memory which doesn't support it
		 */
		if ((ib_peer_client->invalidation_required &&
		     (!(peer_mem_flags & IB_PEER_MEM_INVAL_SUPP))) ||
		    (!context->peer_mem_private_data &&
		     ib_peer_client->peer_mem->get_context_private_data))
			continue;
		ret = ib_peer_client->peer_mem->acquire(addr, size,
						   context->peer_mem_private_data,
						   context->peer_mem_name,
						   peer_client_context);
		if (ret > 0)
			goto found;
	}

	ib_peer_client = NULL;

found:
	if (ib_peer_client)
		kref_get(&ib_peer_client->ref);

	mutex_unlock(&peer_memory_mutex);
	return ib_peer_client;
}
EXPORT_SYMBOL(ib_get_peer_client);

void ib_put_peer_client(struct ib_peer_memory_client *ib_peer_client,
			void *peer_client_context)
{
	if (ib_peer_client->peer_mem->release)
		ib_peer_client->peer_mem->release(peer_client_context);

	kref_put(&ib_peer_client->ref, complete_peer);
}
EXPORT_SYMBOL(ib_put_peer_client);

int ib_get_peer_private_data(struct ib_ucontext *context, u64 peer_id,
			     char *peer_name)
{
	struct ib_peer_memory_client *ib_peer_client;
	void *peer_mem_private_data = NULL;
	int ret = 0;

	mutex_lock(&peer_memory_mutex);
	list_for_each_entry(ib_peer_client, &peer_memory_list, core_peer_list) {
		if ((!ib_peer_client->peer_mem->get_context_private_data) ||
		    (!ib_peer_client->peer_mem->put_context_private_data) ||
		    (strcmp(peer_name, ib_peer_client->peer_mem->name) != 0))
			continue;

		peer_mem_private_data = ib_peer_client->peer_mem->get_context_private_data(peer_id);
		if (peer_mem_private_data)
			break;
	}
	mutex_unlock(&peer_memory_mutex);

	if (peer_mem_private_data) {
		int peer_name_size = sizeof(ib_peer_client->peer_mem->name);

		context->peer_mem_name = kzalloc(peer_name_size, GFP_KERNEL);
		if (!context->peer_mem_name)
			return -ENOMEM;
		memcpy(context->peer_mem_name, peer_name, peer_name_size - 1);
		context->peer_mem_private_data = peer_mem_private_data;
		context->flags |= IB_UCONTEXT_LOCAL_PEER_ALLOC;
	} else {
		ret = -ENODEV;
	}

	return ret;
}
EXPORT_SYMBOL(ib_get_peer_private_data);

void ib_put_peer_private_data(struct ib_ucontext *context)
{
	struct ib_peer_memory_client *ib_peer_client;

	if (!(context->flags & IB_UCONTEXT_LOCAL_PEER_ALLOC))
		return;

	mutex_lock(&peer_memory_mutex);
	list_for_each_entry(ib_peer_client, &peer_memory_list, core_peer_list) {
		if (strcmp(ib_peer_client->peer_mem->name,
			   context->peer_mem_name) == 0) {
			ib_peer_client->peer_mem->put_context_private_data(context->peer_mem_private_data);
			break;
		}
	}

	mutex_unlock(&peer_memory_mutex);
	context->peer_mem_private_data = NULL;
	kfree(context->peer_mem_name);
}
EXPORT_SYMBOL(ib_put_peer_private_data);
