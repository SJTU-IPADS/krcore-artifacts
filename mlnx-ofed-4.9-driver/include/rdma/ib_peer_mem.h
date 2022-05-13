#if !defined(IB_PEER_MEM_H)
#define IB_PEER_MEM_H

#include <rdma/peer_mem.h>

struct ib_peer_memory_statistics {
	atomic64_t num_alloc_mrs;
	atomic64_t num_dealloc_mrs;
	atomic64_t num_reg_pages;
	atomic64_t num_dereg_pages;
	atomic64_t num_reg_bytes;
	atomic64_t num_dereg_bytes;
	unsigned long num_free_callbacks;
};

struct ib_ucontext;
struct ib_umem;
struct invalidation_ctx;

struct ib_peer_memory_client {
	const struct peer_memory_client *peer_mem;
	struct list_head	core_peer_list;
	int invalidation_required;
	struct kref ref;
	struct completion unload_comp;
	/* lock is used via the invalidation flow */
	struct mutex lock;
	struct list_head   core_ticket_list;
	u64	last_ticket;
	struct kobject *kobj;
	struct attribute_group peer_mem_attr_group;
	struct ib_peer_memory_statistics stats;
};

enum ib_peer_mem_flags {
	IB_PEER_MEM_ALLOW	= 1,
	IB_PEER_MEM_INVAL_SUPP = (1<<1),
};

struct core_ticket {
	unsigned long key;
	void *context;
	struct list_head   ticket_list;
};

struct ib_peer_memory_client *ib_get_peer_client(struct ib_ucontext *context, unsigned long addr,
						 size_t size, unsigned long peer_mem_flags,
						 void **peer_client_context);

void ib_put_peer_client(struct ib_peer_memory_client *ib_peer_client,
			void *peer_client_context);

int ib_peer_create_invalidation_ctx(struct ib_peer_memory_client *ib_peer_mem, struct ib_umem *umem,
				    struct invalidation_ctx **invalidation_ctx);

void ib_peer_destroy_invalidation_ctx(struct ib_peer_memory_client *ib_peer_mem,
				      struct invalidation_ctx *invalidation_ctx);

int ib_get_peer_private_data(struct ib_ucontext *context, __u64 peer_id,
			     char *peer_name);
void ib_put_peer_private_data(struct ib_ucontext *context);

#endif
