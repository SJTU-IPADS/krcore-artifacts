#ifndef __mtrack_h_
#define __mtrack_h_

#include "memtrack.h"

#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/io.h>           /* For ioremap_nocache, ioremap, iounmap */
#include <linux/random.h>
#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 27)
# include <linux/io-mapping.h>	/* For ioremap_nocache, ioremap, iounmap */
#endif
#include <linux/mm.h>           /* For all page handling */
#include <linux/workqueue.h>    /* For all work-queue handling */
#include <linux/scatterlist.h>  /* For using scatterlists */
#include <linux/skbuff.h>       /* For skbufs handling */
#include <asm/uaccess.h>	/* For copy from/to user */
#include <linux/export.h>
#include <linux/module.h>

#define MEMTRACK_ERROR_INJECTION_MESSAGE(module, file, line, call_func, func) ({ \
	printk(KERN_ERR "%s::%s::%s failure injected at %s:%d\n", module->name, call_func, func, file, line);\
	dump_stack();								\
})

#ifdef CONFIG_ARM64
#undef ioremap
static inline void *ioremap(phys_addr_t phys_addr, size_t size)
{
	return __ioremap(phys_addr, size, __pgprot(PROT_DEVICE_nGnRE));
}

#undef ioremap_nocache
static inline void *ioremap_nocache(phys_addr_t phys_addr, size_t size)
{
	return __ioremap(phys_addr, size, __pgprot(PROT_DEVICE_nGnRE));
}

#undef ioremap_wc
static inline void *ioremap_wc(phys_addr_t phys_addr, size_t size)
{
	return __ioremap(phys_addr, size, __pgprot(PROT_NORMAL_NC));
}

/* ARCH_HAS_IOREMAP_WC was defined for arm64 until 2014-07-24 */
#ifndef ARCH_HAS_IOREMAP_WC
#define ARCH_HAS_IOREMAP_WC 1
#endif

#ifdef iounmap
#undef iounmap
static inline void iounmap(void *addr)
{
	__iounmap(addr);
}
#endif /* iounmap  */
#endif /* CONFIG_ARM64 */

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 14)
#define RDMA_KZALLOC_H
#define kzalloc(size, flags) ({							\
	void *__memtrack_kz_addr = NULL;					\
										\
	if (memtrack_inject_error(THIS_MODULE, __FILE__, "kzalloc", __func__, __LINE__)) \
		MEMTRACK_ERROR_INJECTION_MESSAGE(THIS_MODULE, __FILE__, __LINE__, __func__, "kzalloc");\
	else									\
		__memtrack_kz_addr = kmalloc(size, flags);			\
	if (__memtrack_kz_addr && !is_non_trackable_alloc_func(__func__)) {	\
		memset(__memtrack_kz_addr, 0, size);				\
	}									\
	__memtrack_kz_addr;							\
})

#else
#ifdef ZERO_OR_NULL_PTR
#define kzalloc(size, flags) ({							\
	void *__memtrack_addr = NULL;						\
										\
	if (memtrack_inject_error(THIS_MODULE, __FILE__, "kzalloc", __func__, __LINE__)) \
		MEMTRACK_ERROR_INJECTION_MESSAGE(THIS_MODULE, __FILE__, __LINE__, __func__, "kzalloc");\
	else									\
		__memtrack_addr = kzalloc(size, flags);				\
	if (!ZERO_OR_NULL_PTR(__memtrack_addr) && !is_non_trackable_alloc_func(__func__)) {	\
		memtrack_alloc(MEMTRACK_KMALLOC, 0UL, (unsigned long)(__memtrack_addr), size, 0UL, 0, __FILE__, __LINE__, flags); \
	}									\
	__memtrack_addr;							\
})
#else
#define kzalloc(size, flags) ({							\
	void *__memtrack_addr = NULL;						\
										\
	if (memtrack_inject_error(THIS_MODULE, __FILE__, "kzalloc", __func__, __LINE__)) \
		MEMTRACK_ERROR_INJECTION_MESSAGE(THIS_MODULE, __FILE__, __LINE__, __func__, "kzalloc");\
	else									\
		__memtrack_addr = kzalloc(size, flags);				\
	if (__memtrack_addr && !is_non_trackable_alloc_func(__func__)) {	\
		memtrack_alloc(MEMTRACK_KMALLOC, 0UL, (unsigned long)(__memtrack_addr), size, 0UL, 0, __FILE__, __LINE__, flags); \
	}									\
	__memtrack_addr;							\
})

#endif
#endif

#define kzalloc_node(size, flags, node) ({					\
	void *__memtrack_addr = NULL;						\
										\
	if (memtrack_inject_error(THIS_MODULE, __FILE__, "kzalloc_node", __func__, __LINE__)) \
		MEMTRACK_ERROR_INJECTION_MESSAGE(THIS_MODULE, __FILE__, __LINE__, __func__, "kzalloc_node"); \
	else									\
		__memtrack_addr = kzalloc_node(size, flags, node);		\
	if (__memtrack_addr && (size) > 0 &&					\
	    !is_non_trackable_alloc_func(__func__)) {				\
		memtrack_alloc(MEMTRACK_KMALLOC, 0UL, (unsigned long)(__memtrack_addr), size, 0UL, 0, __FILE__, __LINE__, flags); \
	}									\
	__memtrack_addr;							\
})

#define kvzalloc(size, flags) ({						\
	void *__memtrack_addr = NULL;						\
										\
	if (memtrack_inject_error(THIS_MODULE, __FILE__, "kvzalloc", __func__, __LINE__)) \
		MEMTRACK_ERROR_INJECTION_MESSAGE(THIS_MODULE, __FILE__, __LINE__, __func__, "kvzalloc"); \
	else									\
		__memtrack_addr = kvzalloc(size, flags);			\
	if (__memtrack_addr && !is_non_trackable_alloc_func(__func__)) {	\
		memtrack_alloc(MEMTRACK_KVMALLOC, 0UL, (unsigned long)(__memtrack_addr), size, 0UL, 0, __FILE__, __LINE__, flags); \
	}									\
	__memtrack_addr;							\
})

#define kvmalloc_array(n, size, flags) ({					\
	void *__memtrack_addr = NULL;						\
										\
	if (memtrack_inject_error(THIS_MODULE, __FILE__, "kvmalloc_array", __func__, __LINE__)) \
		MEMTRACK_ERROR_INJECTION_MESSAGE(THIS_MODULE, __FILE__, __LINE__, __func__, "kvmalloc_array"); \
	else									\
		__memtrack_addr = kvmalloc_array(n, size, flags);		\
	if (__memtrack_addr && !is_non_trackable_alloc_func(__func__) && (n) * (size) > 0) {	\
		memtrack_alloc(MEMTRACK_KVMALLOC, 0UL, (unsigned long)(__memtrack_addr), (n)*size, 0UL, 0, __FILE__, __LINE__, flags); \
	}									\
	__memtrack_addr;							\
})
#define kvcalloc(n, size, flags) ({						\
	void *__memtrack_addr = NULL;						\
										\
	if (memtrack_inject_error(THIS_MODULE, __FILE__, "kvcalloc", __func__, __LINE__)) \
		MEMTRACK_ERROR_INJECTION_MESSAGE(THIS_MODULE, __FILE__, __LINE__, __func__, "kvcalloc"); \
	else									\
		__memtrack_addr = kvcalloc(n, size, flags);			\
	if (__memtrack_addr && !is_non_trackable_alloc_func(__func__)) {	\
		memtrack_alloc(MEMTRACK_KVMALLOC, 0UL, (unsigned long)(__memtrack_addr),(n)*(size), 0UL, 0, __FILE__, __LINE__, flags); \
	}									\
	__memtrack_addr;							\
})
#define kcalloc_node(n, size, flags, node) ({					\
	void *__memtrack_addr = NULL;						\
										\
	if (memtrack_inject_error(THIS_MODULE, __FILE__, "kcalloc_node", __func__, __LINE__)) \
		MEMTRACK_ERROR_INJECTION_MESSAGE(THIS_MODULE, __FILE__, __LINE__, __func__, "kcalloc_node"); \
	else									\
		__memtrack_addr = kcalloc_node(n, size, flags, node);		\
	if (__memtrack_addr && (size) > 0 &&					\
	    !is_non_trackable_alloc_func(__func__)) {				\
		memtrack_alloc(MEMTRACK_KMALLOC, 0UL, (unsigned long)(__memtrack_addr),(n) * (size), 0UL, 0, __FILE__, __LINE__, flags); \
	}									\
	__memtrack_addr;							\
})
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 19)
#define kcalloc(n, size, flags) kzalloc((n)*(size), flags)
#else
#ifdef ZERO_OR_NULL_PTR
#define kcalloc(n, size, flags) ({ \
	void *__memtrack_addr = NULL;						\
										\
	if (memtrack_inject_error(THIS_MODULE, __FILE__, "kcalloc", __func__, __LINE__)) \
		MEMTRACK_ERROR_INJECTION_MESSAGE(THIS_MODULE, __FILE__, __LINE__, __func__, "kcalloc");\
	else									\
		__memtrack_addr = kcalloc(n, size, flags);			\
	if ((!ZERO_OR_NULL_PTR(__memtrack_addr) && (n) * (size) > 0) &&		\
	    !is_non_trackable_alloc_func(__func__)) {				\
		memtrack_alloc(MEMTRACK_KMALLOC, 0UL, (unsigned long)(__memtrack_addr), (n)*(size), 0UL, 0, __FILE__, __LINE__, flags); \
	}									\
	__memtrack_addr;							\
})
#else
#define kcalloc(n, size, flags) ({ \
	void *__memtrack_addr = NULL;						\
										\
	if (memtrack_inject_error(THIS_MODULE, __FILE__, "kcalloc", __func__, __LINE__)) \
		MEMTRACK_ERROR_INJECTION_MESSAGE(THIS_MODULE, __FILE__, __LINE__, __func__, "kcalloc");\
	else									\
		__memtrack_addr = kcalloc(n, size, flags);			\
	if ((__memtrack_addr && (n) * (size) > 0) &&				\
	    !is_non_trackable_alloc_func(__func__)) {				\
		memtrack_alloc(MEMTRACK_KMALLOC, 0UL, (unsigned long)(__memtrack_addr), (n)*(size), 0UL, 0, __FILE__, __LINE__, flags); \
	}									\
	__memtrack_addr;							\
})
#endif
#endif



#ifdef ZERO_OR_NULL_PTR
#define kmalloc(sz, flgs) ({							\
	void *__memtrack_addr = NULL;						\
										\
	if (memtrack_inject_error(THIS_MODULE, __FILE__, "kmalloc", __func__, __LINE__)) \
		MEMTRACK_ERROR_INJECTION_MESSAGE(THIS_MODULE, __FILE__, __LINE__, __func__, "kmalloc");\
	else									\
		__memtrack_addr = kmalloc(sz, flgs);				\
	if (!ZERO_OR_NULL_PTR(__memtrack_addr)) {				\
		memtrack_alloc(MEMTRACK_KMALLOC, 0UL, (unsigned long)(__memtrack_addr), sz, 0UL, 0, __FILE__, __LINE__, flgs); \
		if (memtrack_randomize_mem())					\
			memset(__memtrack_addr, 0x5A, sz);			\
	}									\
	__memtrack_addr;							\
})
#else
#define kmalloc(sz, flgs) ({							\
	void *__memtrack_addr = NULL;						\
										\
	if (memtrack_inject_error(THIS_MODULE, __FILE__, "kmalloc", __func__, __LINE__)) \
		MEMTRACK_ERROR_INJECTION_MESSAGE(THIS_MODULE, __FILE__, __LINE__, __func__, "kmalloc");\
	else									\
		__memtrack_addr = kmalloc(sz, flgs);				\
	if (__memtrack_addr) {							\
		memtrack_alloc(MEMTRACK_KMALLOC, 0UL, (unsigned long)(__memtrack_addr), sz, 0UL, 0, __FILE__, __LINE__, flgs); \
		if (memtrack_randomize_mem())					\
			memset(__memtrack_addr, 0x5A, sz);			\
	}									\
	__memtrack_addr;							\
})

#endif

#define kmalloc_node(sz, flgs, node) ({						\
	void *__memtrack_addr = NULL;						\
										\
	if (memtrack_inject_error(THIS_MODULE, __FILE__, "kmalloc_node", __func__, __LINE__)) \
		MEMTRACK_ERROR_INJECTION_MESSAGE(THIS_MODULE, __FILE__, __LINE__, __func__, "kmalloc_node"); \
	else									\
		__memtrack_addr = kmalloc_node(sz, flgs, node);			\
	if (__memtrack_addr) {							\
		memtrack_alloc(MEMTRACK_KMALLOC, 0UL, (unsigned long)(__memtrack_addr), sz, 0UL, 0, __FILE__, __LINE__, flgs); \
		if (memtrack_randomize_mem() && ((flgs) == GFP_KERNEL))		\
			memset(__memtrack_addr, 0x5A, sz);			\
	}									\
	__memtrack_addr;							\
})

#define kvmalloc(sz, flgs) ({						\
	void *__memtrack_addr = NULL;						\
										\
	if (memtrack_inject_error(THIS_MODULE, __FILE__, "kvmalloc", __func__, __LINE__)) \
		MEMTRACK_ERROR_INJECTION_MESSAGE(THIS_MODULE, __FILE__, __LINE__, __func__, "kvmalloc"); \
	else									\
		__memtrack_addr = kvmalloc(sz, flgs);			\
	if (__memtrack_addr && !is_non_trackable_alloc_func(__func__)) {\
		memtrack_alloc(MEMTRACK_KVMALLOC, 0UL, (unsigned long)(__memtrack_addr), sz, 0UL, 0, __FILE__, __LINE__, flgs); \
		if (memtrack_randomize_mem() && ((flgs) == GFP_KERNEL))		\
			memset(__memtrack_addr, 0x5A, sz);			\
	}									\
	__memtrack_addr;							\
})
#define kvmalloc_node(sz, flgs, node) ({						\
	void *__memtrack_addr = NULL;						\
										\
	if (memtrack_inject_error(THIS_MODULE, __FILE__, "kvmalloc_node", __func__, __LINE__)) \
		MEMTRACK_ERROR_INJECTION_MESSAGE(THIS_MODULE, __FILE__, __LINE__, __func__, "kvmalloc_node"); \
	else									\
		__memtrack_addr = kvmalloc_node(sz, flgs, node);			\
	if (__memtrack_addr) {							\
		memtrack_alloc(MEMTRACK_KVMALLOC, 0UL, (unsigned long)(__memtrack_addr), sz, 0UL, 0, __FILE__, __LINE__, flgs); \
		if (memtrack_randomize_mem() && ((flgs) == GFP_KERNEL))		\
			memset(__memtrack_addr, 0x5A, sz);			\
	}									\
	__memtrack_addr;							\
})

#define kvzalloc_node(sz, flgs, node) ({						\
	void *__memtrack_addr = NULL;						\
										\
	if (memtrack_inject_error(THIS_MODULE, __FILE__, "kvzalloc_node", __func__, __LINE__)) \
		MEMTRACK_ERROR_INJECTION_MESSAGE(THIS_MODULE, __FILE__, __LINE__, __func__, "kvzalloc_node"); \
	else									\
		__memtrack_addr = kvzalloc_node(sz, flgs, node);			\
	if (__memtrack_addr) {							\
		memtrack_alloc(MEMTRACK_KVMALLOC, 0UL, (unsigned long)(__memtrack_addr), sz, 0UL, 0, __FILE__, __LINE__, flgs); \
	}									\
	__memtrack_addr;							\
})

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 4, 0)
#define kmalloc_array(n, size, flags) kzalloc((n)*(size), flags)
#else
#ifdef ZERO_OR_NULL_PTR
#define kmalloc_array(n, size, flags) ({ \
	void *__memtrack_addr = NULL;						\
										\
	if (memtrack_inject_error(THIS_MODULE, __FILE__, "kmalloc_array", __func__, __LINE__)) \
		MEMTRACK_ERROR_INJECTION_MESSAGE(THIS_MODULE, __FILE__, __LINE__, __func__, "kmalloc_array"); \
	else									\
		__memtrack_addr = kmalloc_array(n, size, flags);		\
	if (!ZERO_OR_NULL_PTR(__memtrack_addr) && (n) * (size) > 0) {		\
		memtrack_alloc(MEMTRACK_KMALLOC, 0UL, (unsigned long)(__memtrack_addr), (n)*(size), 0UL, 0, __FILE__, __LINE__, flags); \
	}									\
	__memtrack_addr;							\
})
#else
#define kmalloc_array(n, size, flags) ({ \
	void *__memtrack_addr = NULL;						\
										\
	if (memtrack_inject_error(THIS_MODULE, __FILE__, "kmalloc_array", __func__, __LINE__)) \
		MEMTRACK_ERROR_INJECTION_MESSAGE(THIS_MODULE, __FILE__, __LINE__, __func__, "kmalloc_array"); \
	else									\
		__memtrack_addr = kmalloc_array(n, size, flags);			\
	if (__memtrack_addr && (n) * (size) > 0) {				\
		memtrack_alloc(MEMTRACK_KMALLOC, 0UL, (unsigned long)(__memtrack_addr), (n)*(size), 0UL, 0, __FILE__, __LINE__, flags); \
	}									\
	__memtrack_addr;							\
})
#endif
#endif

#ifdef ZERO_OR_NULL_PTR
#define kmemdup(src, sz, flgs) ({						\
	void *__memtrack_addr = NULL;						\
										\
	if (memtrack_inject_error(THIS_MODULE, __FILE__, "kmemdup", __func__, __LINE__)) \
		MEMTRACK_ERROR_INJECTION_MESSAGE(THIS_MODULE, __FILE__, __LINE__, __func__, "kmemdup");\
	else									\
		__memtrack_addr = kmemdup(src, sz, flgs);			\
	if (!ZERO_OR_NULL_PTR(__memtrack_addr)) {				\
		memtrack_alloc(MEMTRACK_KMALLOC, 0UL, (unsigned long)(__memtrack_addr), sz, 0UL, 0, __FILE__, __LINE__, flgs); \
	}									\
	__memtrack_addr;							\
})
#else
#define kmemdup(src, sz, flgs) ({						\
	void *__memtrack_addr = NULL;						\
										\
	if (memtrack_inject_error(THIS_MODULE, __FILE__, "kmemdup", __func__, __LINE__)) \
		MEMTRACK_ERROR_INJECTION_MESSAGE(THIS_MODULE, __FILE__, __LINE__, __func__, "kmemdup");\
	else									\
		__memtrack_addr = kmemdup(src, sz, flgs);			\
	if (__memtrack_addr) {							\
		memtrack_alloc(MEMTRACK_KMALLOC, 0UL, (unsigned long)(__memtrack_addr), sz, 0UL, 0, __FILE__, __LINE__, flgs); \
	}									\
	__memtrack_addr;							\
})
#endif

#ifndef kstrdup
#define kstrdup(src, flgs) ({						\
	void *__memtrack_addr = NULL;						\
	size_t sz = strlen(src) + 1;						\
										\
	if (memtrack_inject_error(THIS_MODULE, __FILE__, "kstrdup", __func__, __LINE__)) \
		MEMTRACK_ERROR_INJECTION_MESSAGE(THIS_MODULE, __FILE__, __LINE__, __func__, "kstrdup");\
	else									\
		__memtrack_addr = kstrdup(src, flgs);			\
	if (__memtrack_addr) {							\
		memtrack_alloc(MEMTRACK_KMALLOC, 0UL, (unsigned long)(__memtrack_addr), sz, 0UL, 0, __FILE__, __LINE__, flgs); \
	}									\
	__memtrack_addr;							\
})
#endif

#ifdef ZERO_OR_NULL_PTR
#define kfree(addr) ({								\
	void *__memtrack_addr = (void *)addr;					\
										\
	if (!ZERO_OR_NULL_PTR(__memtrack_addr) &&				\
	    !is_non_trackable_free_func(__func__)) {				\
		memtrack_free(MEMTRACK_KMALLOC, 0UL, (unsigned long)(__memtrack_addr), 0UL, 0, __FILE__, __LINE__); \
	}									\
	kfree(__memtrack_addr);							\
})
#else
#define kfree(addr) ({								\
	void *__memtrack_addr = (void *)addr;					\
										\
	if (__memtrack_addr && !is_non_trackable_free_func(__func__)) {		\
		memtrack_free(MEMTRACK_KMALLOC, 0UL, (unsigned long)(__memtrack_addr), 0UL, 0, __FILE__, __LINE__); \
	}									\
	kfree(__memtrack_addr);							\
})
#endif

#ifdef CONFIG_COMPAT_RCU
#ifdef kfree_rcu
	#undef kfree_rcu
#endif

#ifdef ZERO_OR_NULL_PTR
#define kfree_rcu(addr, rcu_head) ({								\
	void *__memtrack_addr = (void *)addr;					\
										\
	if (!ZERO_OR_NULL_PTR(__memtrack_addr) &&				\
	    !is_non_trackable_free_func(__func__)) {				\
		memtrack_free(MEMTRACK_KMALLOC, 0UL, (unsigned long)(__memtrack_addr), 0UL, 0, __FILE__, __LINE__); \
	}									\
	__kfree_rcu(&((addr)->rcu_head), offsetof(typeof(*(addr)), rcu_head));					\
})
#else
#define kfree_rcu(addr, rcu_head) ({								\
	void *__memtrack_addr = (void *)addr;					\
										\
	if (__memtrack_addr && !is_non_trackable_free_func(__func__)) {		\
		memtrack_free(MEMTRACK_KMALLOC, 0UL, (unsigned long)(__memtrack_addr), 0UL, 0, __FILE__, __LINE__); \
	}									\
	__kfree_rcu(&((addr)->rcu_head), offsetof(typeof(*(addr)), rcu_head));					\
})
#endif
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(3, 0, 0) */

#define vmalloc(size) ({							\
	void *__memtrack_addr = NULL;						\
										\
	if (memtrack_inject_error(THIS_MODULE, __FILE__, "vmalloc", __func__, __LINE__)) \
		MEMTRACK_ERROR_INJECTION_MESSAGE(THIS_MODULE, __FILE__, __LINE__, __func__, "vmalloc");\
	else									\
		__memtrack_addr = vmalloc(size);				\
	if (__memtrack_addr) {							\
		memtrack_alloc(MEMTRACK_VMALLOC, 0UL, (unsigned long)(__memtrack_addr), size, 0UL, 0, __FILE__, __LINE__, GFP_ATOMIC); \
		if (memtrack_randomize_mem())					\
			memset(__memtrack_addr, 0x5A, size);			\
	}									\
	__memtrack_addr;							\
})

#ifndef vzalloc
#define vzalloc(size) ({							\
	void *__memtrack_addr = NULL;						\
										\
	if (memtrack_inject_error(THIS_MODULE, __FILE__, "vzalloc", __func__, __LINE__)) \
		MEMTRACK_ERROR_INJECTION_MESSAGE(THIS_MODULE, __FILE__, __LINE__, __func__, "vzalloc");\
	else									\
		__memtrack_addr = vzalloc(size);				\
	if (__memtrack_addr) {							\
		memtrack_alloc(MEMTRACK_VMALLOC, 0UL, (unsigned long)(__memtrack_addr), size, 0UL, 0, __FILE__, __LINE__, GFP_ATOMIC); \
	}									\
	__memtrack_addr;							\
})
#endif

#ifndef vzalloc_node
#define vzalloc_node(size, node) ({						\
	void *__memtrack_addr = NULL;						\
										\
	if (memtrack_inject_error(THIS_MODULE, __FILE__, "vzalloc_node", __func__, __LINE__)) \
		MEMTRACK_ERROR_INJECTION_MESSAGE(THIS_MODULE, __FILE__, __LINE__, __func__, "vzalloc_node"); \
	else									\
		__memtrack_addr = vzalloc_node(size, node);			\
	if (__memtrack_addr) {							\
		memtrack_alloc(MEMTRACK_VMALLOC, 0UL, (unsigned long)(__memtrack_addr), size, 0UL, 0, __FILE__, __LINE__, GFP_ATOMIC); \
	}									\
	__memtrack_addr;							\
})
#endif

#ifndef __vmalloc
#define __vmalloc(size, mask, prot) ({						\
	void *__memtrack_addr = NULL;						\
										\
	if (memtrack_inject_error(THIS_MODULE, __FILE__, "__vmalloc", __func__, __LINE__)) \
		MEMTRACK_ERROR_INJECTION_MESSAGE(THIS_MODULE, __FILE__, __LINE__, __func__, "__vmalloc"); \
	else									\
		__memtrack_addr = __vmalloc(size, mask, prot);			\
	if (__memtrack_addr) {							\
		memtrack_alloc(MEMTRACK_VMALLOC, 0UL, (unsigned long)(__memtrack_addr), size, 0UL, 0, __FILE__, __LINE__, GFP_ATOMIC); \
		if (memtrack_randomize_mem())					\
			memset(__memtrack_addr, 0x5A, size);			\
	}									\
	__memtrack_addr;							\
})
#endif

#define vmalloc_node(size, node) ({						\
	void *__memtrack_addr = NULL;						\
										\
	if (memtrack_inject_error(THIS_MODULE, __FILE__, "vmalloc_node", __func__, __LINE__)) \
		MEMTRACK_ERROR_INJECTION_MESSAGE(THIS_MODULE, __FILE__, __LINE__, __func__, "vmalloc_node"); \
	else									\
		__memtrack_addr = vmalloc_node(size, node);			\
	if (__memtrack_addr) {							\
		memtrack_alloc(MEMTRACK_VMALLOC, 0UL, (unsigned long)(__memtrack_addr), size, 0UL, 0, __FILE__, __LINE__, GFP_ATOMIC); \
		if (memtrack_randomize_mem())					\
			memset(__memtrack_addr, 0x5A, size);			\
	}									\
	__memtrack_addr;							\
})

#define vfree(addr) ({ \
	void *__memtrack_addr = (void *)addr;					\
	if (__memtrack_addr && !is_non_trackable_free_func(__func__)) {		\
		memtrack_free(MEMTRACK_VMALLOC, 0UL, (unsigned long)(__memtrack_addr), 0UL, 0, __FILE__, __LINE__); \
	}									\
	vfree(__memtrack_addr);							\
})

#ifndef kvfree
#ifdef ZERO_OR_NULL_PTR
#define kvfree(addr) ({								\
	void *__memtrack_addr = (void *)addr;					\
	if (__memtrack_addr && !is_non_trackable_free_func(__func__)) {		\
		if (is_vmalloc_addr(__memtrack_addr)) {				\
			memtrack_free(MEMTRACK_VMALLOC, 0UL, (unsigned long)(__memtrack_addr), 0UL, 0, __FILE__, __LINE__); \
		} else {							\
			if (!ZERO_OR_NULL_PTR(__memtrack_addr)) {		\
				memtrack_free(MEMTRACK_KMALLOC, 0UL, (unsigned long)(__memtrack_addr), 0UL, 0, __FILE__, __LINE__); \
			}							\
		}								\
	}									\
	kvfree(__memtrack_addr);						\
})
#else
#define kvfree(addr) ({								\
	void *__memtrack_addr = (void *)addr;					\
	if (__memtrack_addr && !is_non_trackable_free_func(__func__)) {		\
		if (is_vmalloc_addr(__memtrack_addr)) {				\
			memtrack_free(MEMTRACK_VMALLOC, 0UL, (unsigned long)(__memtrack_addr), 0UL, 0, __FILE__, __LINE__); \
		} else {							\
			memtrack_free(MEMTRACK_KMALLOC, 0UL, (unsigned long)(__memtrack_addr), 0UL, 0, __FILE__, __LINE__); \
		}								\
	}									\
	kvfree(__memtrack_addr);						\
})
#endif
#endif

#ifndef memdup_user
#ifdef ZERO_OR_NULL_PTR
#define memdup_user(user_addr, size) ({						\
	void *__memtrack_addr = NULL;						\
										\
	if (memtrack_inject_error(THIS_MODULE, __FILE__, "memdup_user", __func__, __LINE__)) \
		MEMTRACK_ERROR_INJECTION_MESSAGE(THIS_MODULE, __FILE__, __LINE__, __func__, "memdup_user"); \
	else									\
		__memtrack_addr = memdup_user(user_addr, size);			\
										\
	if (!ZERO_OR_NULL_PTR(__memtrack_addr)) {							\
		memtrack_alloc(MEMTRACK_KMALLOC, 0UL, (unsigned long)(__memtrack_addr), size, 0UL, 0, __FILE__, __LINE__, GFP_KERNEL); \
	}									\
	__memtrack_addr;							\
})
#else
#define memdup_user(user_addr, size) ({						\
	void *__memtrack_addr = NULL;						\
										\
	if (memtrack_inject_error(THIS_MODULE, __FILE__, "memdup_user", __func__, __LINE__)) \
		MEMTRACK_ERROR_INJECTION_MESSAGE(THIS_MODULE, __FILE__, __LINE__, __func__, "memdup_user"); \
	else									\
		__memtrack_addr = memdup_user(user_addr, size);			\
										\
	if (__memtrack_addr) {							\
		memtrack_alloc(MEMTRACK_KMALLOC, 0UL, (unsigned long)(__memtrack_addr), size, 0UL, 0, __FILE__, __LINE__, GFP_KERNEL); \
	}									\
	__memtrack_addr;							\
})
#endif
#endif

#ifndef memdup_user_nul
#ifdef ZERO_OR_NULL_PTR
#define memdup_user_nul(user_addr, size) ({						\
	void *__memtrack_addr = NULL;						\
										\
	if (memtrack_inject_error(THIS_MODULE, __FILE__, "memdup_user_nul", __func__, __LINE__)) \
		MEMTRACK_ERROR_INJECTION_MESSAGE(THIS_MODULE, __FILE__, __LINE__, __func__, "memdup_user_nul"); \
	else									\
		__memtrack_addr = memdup_user_nul(user_addr, size);			\
										\
	if (!ZERO_OR_NULL_PTR(__memtrack_addr)) {							\
		memtrack_alloc(MEMTRACK_KMALLOC, 0UL, (unsigned long)(__memtrack_addr), size, 0UL, 0, __FILE__, __LINE__, GFP_KERNEL); \
	}									\
	__memtrack_addr;							\
})
#else
#define memdup_user_nul(user_addr, size) ({						\
	void *__memtrack_addr = NULL;						\
										\
	if (memtrack_inject_error(THIS_MODULE, __FILE__, "memdup_user_nul", __func__, __LINE__)) \
		MEMTRACK_ERROR_INJECTION_MESSAGE(THIS_MODULE, __FILE__, __LINE__, __func__, "memdup_user_nul"); \
	else									\
		__memtrack_addr = memdup_user_nul(user_addr, size);			\
										\
	if (__memtrack_addr) {							\
		memtrack_alloc(MEMTRACK_KMALLOC, 0UL, (unsigned long)(__memtrack_addr), size, 0UL, 0, __FILE__, __LINE__, GFP_KERNEL); \
	}									\
	__memtrack_addr;							\
})
#endif
#endif


#define kmem_cache_alloc(cache, flags) ({					\
	void *__memtrack_addr = NULL;						\
										\
	if (memtrack_inject_error(THIS_MODULE, __FILE__, "kmem_cache_alloc", __func__, __LINE__)) \
		MEMTRACK_ERROR_INJECTION_MESSAGE(THIS_MODULE, __FILE__, __LINE__, __func__, "kmem_cache_alloc"); \
	else									\
		__memtrack_addr = kmem_cache_alloc(cache, flags);		\
	if (__memtrack_addr) {							\
		memtrack_alloc(MEMTRACK_KMEM_OBJ, 0UL, (unsigned long)(__memtrack_addr), 1, 0UL, 0, __FILE__, __LINE__, flags); \
	}									\
	__memtrack_addr;							\
})

#define kmem_cache_zalloc(cache, flags) ({					\
	void *__memtrack_addr = NULL;						\
										\
	__memtrack_addr = kmem_cache_alloc(cache, flags | __GFP_ZERO);		\
	__memtrack_addr;							\
})

#define kmem_cache_free(cache, addr) ({						\
	void *__memtrack_addr = (void *)addr;					\
										\
	if (__memtrack_addr) {						\
		memtrack_free(MEMTRACK_KMEM_OBJ, 0UL, (unsigned long)(__memtrack_addr), 0UL, 0, __FILE__, __LINE__); \
	}									\
	kmem_cache_free(cache, __memtrack_addr);				\
})

#ifndef kasprintf
#define kasprintf(gfp, fmt, ...) ({						\
	void *__memtrack_addr = NULL;						\
										\
	if (memtrack_inject_error(THIS_MODULE, __FILE__, "kasprintf", __func__, __LINE__)) \
		MEMTRACK_ERROR_INJECTION_MESSAGE(THIS_MODULE, __FILE__, __LINE__, __func__, "kasprintf"); \
	else									\
		__memtrack_addr = kasprintf(gfp, fmt, __VA_ARGS__);		\
	if (__memtrack_addr && strncmp((char *)__memtrack_addr, "infiniband", 10)) {	\
		memtrack_alloc(MEMTRACK_KMALLOC, 0UL, (unsigned long)(__memtrack_addr), strlen((char *)__memtrack_addr), 0UL, 0, __FILE__, __LINE__, gfp); \
	}									\
	__memtrack_addr;							\
})
#endif

/* All IO-MAP handling */
#ifdef ioremap
	#undef ioremap
#endif
#define ioremap(phys_addr, size) ({						\
	void __iomem *__memtrack_addr = NULL;					\
										\
	if (memtrack_inject_error(THIS_MODULE, __FILE__, "ioremap", __func__, __LINE__)) \
		MEMTRACK_ERROR_INJECTION_MESSAGE(THIS_MODULE, __FILE__, __LINE__, __func__, "ioremap");\
	else									\
		__memtrack_addr = ioremap(phys_addr, size);			\
	if (__memtrack_addr) {						\
		memtrack_alloc(MEMTRACK_IOREMAP, 0UL, (unsigned long)(__memtrack_addr), size, 0UL, 0, __FILE__, __LINE__, GFP_ATOMIC); \
	}									\
	__memtrack_addr;							\
})

#ifdef ioremap_wc
	#undef ioremap_wc
#endif
#ifdef ARCH_HAS_IOREMAP_WC
#define ioremap_wc(phys_addr, size) ({						\
	void __iomem *__memtrack_addr = NULL;					\
										\
	if (memtrack_inject_error(THIS_MODULE, __FILE__, "ioremap_wc", __func__, __LINE__)) \
		MEMTRACK_ERROR_INJECTION_MESSAGE(THIS_MODULE, __FILE__, __LINE__, __func__, "ioremap_wc");\
	else									\
		__memtrack_addr = ioremap_wc(phys_addr, size);			\
	if (__memtrack_addr) {						\
		memtrack_alloc(MEMTRACK_IOREMAP, 0UL, (unsigned long)(__memtrack_addr), size, 0UL, 0, __FILE__, __LINE__, GFP_ATOMIC); \
	}									\
	__memtrack_addr;							\
})
#else
#define ioremap_wc(phys_addr, size) ({						\
	void __iomem *__memtrack_addr = NULL;					\
										\
	if (memtrack_inject_error(THIS_MODULE, __FILE__, "ioremap_wc", __func__, __LINE__)) \
		MEMTRACK_ERROR_INJECTION_MESSAGE(THIS_MODULE, __FILE__, __LINE__, __func__, "ioremap_wc");\
	else									\
		__memtrack_addr = ioremap_nocache(phys_addr, size);			\
	__memtrack_addr;							\
})
#endif

#define io_mapping_create_wc(base, size) ({					\
	void __iomem *__memtrack_addr = NULL;					\
										\
	if (memtrack_inject_error(THIS_MODULE, __FILE__, "io_mapping_create_wc", __func__, __LINE__)) \
		MEMTRACK_ERROR_INJECTION_MESSAGE(THIS_MODULE, __FILE__, __LINE__, __func__, "io_mapping_create_wc"); \
	else									\
		__memtrack_addr = io_mapping_create_wc(base, size);		\
	if (__memtrack_addr) {							\
		memtrack_alloc(MEMTRACK_IOREMAP, 0UL, (unsigned long)(__memtrack_addr), size, 0UL, 0, __FILE__, __LINE__, GFP_ATOMIC); \
	}									\
	__memtrack_addr;							\
})

#define io_mapping_free(addr) ({						\
	void *__memtrack_addr = (void *)addr;					\
										\
	if (__memtrack_addr) {							\
		memtrack_free(MEMTRACK_IOREMAP, 0UL, (unsigned long)(__memtrack_addr), 0UL, 0, __FILE__, __LINE__); \
	}									\
	io_mapping_free(__memtrack_addr);					\
})

#ifdef ioremap_nocache
	#undef ioremap_nocache
#endif
#ifdef CONFIG_PPC
#define ioremap_nocache(phys_addr, size) ({					\
	void __iomem *__memtrack_addr = NULL;					\
										\
	if (memtrack_inject_error(THIS_MODULE, __FILE__, "ioremap_nocache", __func__, __LINE__)) \
		MEMTRACK_ERROR_INJECTION_MESSAGE(THIS_MODULE, __FILE__, __LINE__, __func__, "ioremap_nocache"); \
	else									\
		__memtrack_addr = ioremap(phys_addr, size);			\
	__memtrack_addr;							\
})
#else
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 18) /* 2.6.16 - 2.6.17 */
#define ioremap_nocache(phys_addr, size) ({					\
	void __iomem *__memtrack_addr = NULL;					\
										\
	if (memtrack_inject_error(THIS_MODULE, __FILE__, "ioremap_nocache", __func__, __LINE__)) \
		MEMTRACK_ERROR_INJECTION_MESSAGE(THIS_MODULE, __FILE__, __LINE__, __func__, "ioremap_nocache"); \
	else									\
		__memtrack_addr = ioremap(phys_addr, size);			\
	__memtrack_addr;							\
})
#else
#define ioremap_nocache(phys_addr, size) ({ \
	void __iomem *__memtrack_addr = NULL;					\
										\
	if (memtrack_inject_error(THIS_MODULE, __FILE__, "ioremap_nocache", __func__, __LINE__)) \
		MEMTRACK_ERROR_INJECTION_MESSAGE(THIS_MODULE, __FILE__, __LINE__, __func__, "ioremap_nocache"); \
	else									\
		__memtrack_addr = ioremap_nocache(phys_addr, size);		\
	if (__memtrack_addr) {							\
		memtrack_alloc(MEMTRACK_IOREMAP, 0UL, (unsigned long)(__memtrack_addr), size, 0UL, 0, __FILE__, __LINE__, GFP_ATOMIC); \
	}									\
	__memtrack_addr;							\
})
#endif /* Kernel version is under 2.6.18 */
#endif	/* PPC */

#ifdef iounmap
	#undef iounmap
#endif
#define iounmap(addr) ({							\
	void *__memtrack_addr = (void *)addr;					\
										\
	if (__memtrack_addr) {							\
		memtrack_free(MEMTRACK_IOREMAP, 0UL, (unsigned long)(__memtrack_addr), 0UL, 0, __FILE__, __LINE__); \
	}									\
	iounmap(__memtrack_addr);						\
})


/* All Page handlers */
/* TODO: Catch netif_rx for page dereference */
#define alloc_pages_node(nid, gfp_mask, order) ({				\
	struct page *page_addr = NULL;						\
										\
	if (memtrack_inject_error(THIS_MODULE, __FILE__, "alloc_pages_node", __func__, __LINE__)) \
		MEMTRACK_ERROR_INJECTION_MESSAGE(THIS_MODULE, __FILE__, __LINE__, __func__, "alloc_pages_node"); \
	else									\
	page_addr = (struct page *)alloc_pages_node(nid, gfp_mask, order);	\
	if (page_addr && !is_non_trackable_alloc_func(__func__)) {		\
		memtrack_alloc(MEMTRACK_PAGE_ALLOC, 0UL, (unsigned long)(page_addr), (unsigned long)(order), 0UL, 0, __FILE__, __LINE__, GFP_ATOMIC); \
	}									\
	page_addr;								\
})

#define dev_alloc_pages(order) ({                              \
	struct page *page_addr = NULL;                                          \
                                                                        \
	if (memtrack_inject_error(THIS_MODULE, __FILE__, "dev_alloc_pages", __func__, __LINE__)) \
		MEMTRACK_ERROR_INJECTION_MESSAGE(THIS_MODULE, __FILE__, __LINE__, __func__, "dev_alloc_pages"); \
	else                                                                    \
	page_addr = (struct page *)dev_alloc_pages(order);      \
	if (page_addr && !is_non_trackable_alloc_func(__func__)) {              \
		memtrack_alloc(MEMTRACK_PAGE_ALLOC, 0UL, (unsigned long)(page_addr), (unsigned long)(order), 0UL, 0, __FILE__, __LINE__, GFP_ATOMIC); \
	}                                                                       \
	page_addr;                                                              \
})

#ifdef HAVE_SPLIT_PAGE_EXPORTED
#define split_page(pg, order) ({					\
	if (memtrack_inject_error(THIS_MODULE, __FILE__, "split_page", __func__, __LINE__)) \
		MEMTRACK_ERROR_INJECTION_MESSAGE(THIS_MODULE, __FILE__, __LINE__, __func__, "split_page"); \
	else {								\
		int i;							\
		split_page(pg, order);					\
		for (i = 1; i < (1 << order); i++) {			\
			struct page *page_addr = &pg[i];		\
			if (page_addr && !is_non_trackable_alloc_func(__func__)) {	\
				memtrack_alloc(MEMTRACK_PAGE_ALLOC, 0UL, (unsigned long)(page_addr), (unsigned long)(order), 0UL, 0, __FILE__, __LINE__, GFP_ATOMIC); \
			}						\
		}							\
	}								\
})
#endif

#ifdef CONFIG_NUMA
#define alloc_pages(gfp_mask, order) ({						\
	struct page *page_addr = NULL;						\
										\
	if (memtrack_inject_error(THIS_MODULE, __FILE__, "alloc_pages", __func__, __LINE__)) \
		MEMTRACK_ERROR_INJECTION_MESSAGE(THIS_MODULE, __FILE__, __LINE__, __func__, "alloc_pages"); \
	else									\
		page_addr = (struct page *)alloc_pages(gfp_mask, order);	\
	if (page_addr && !is_non_trackable_alloc_func(__func__)) {		\
		memtrack_alloc(MEMTRACK_PAGE_ALLOC, 0UL, (unsigned long)(page_addr), (unsigned long)(order), 0UL, 0, __FILE__, __LINE__, GFP_ATOMIC); \
	}									\
	page_addr;								\
})
#else
#ifdef alloc_pages
	#undef alloc_pages
#endif
#define alloc_pages(gfp_mask, order) ({						\
	struct page *page_addr;							\
										\
	page_addr = (struct page *)alloc_pages_node(numa_node_id(), gfp_mask, order); \
	page_addr;								\
})
#endif

#define __get_free_pages(gfp_mask, order) ({					\
	struct page *page_addr = NULL;						\
										\
	if (memtrack_inject_error(THIS_MODULE, __FILE__, "__get_free_pages", __func__, __LINE__)) \
		MEMTRACK_ERROR_INJECTION_MESSAGE(THIS_MODULE, __FILE__, __LINE__, __func__, "__get_free_pages"); \
	else									\
		page_addr = (struct page *)__get_free_pages(gfp_mask, order);	\
	if (page_addr && !is_non_trackable_alloc_func(__func__)) {		\
		memtrack_alloc(MEMTRACK_PAGE_ALLOC, 0UL, (unsigned long)(page_addr), (unsigned long)(order), 0UL, 0, __FILE__, __LINE__, GFP_ATOMIC); \
	}									\
	(unsigned long)page_addr;						\
})

#define get_zeroed_page(gfp_mask) ({						\
	struct page *page_addr = NULL;						\
										\
	if (memtrack_inject_error(THIS_MODULE, __FILE__, "get_zeroed_page", __func__, __LINE__)) \
		MEMTRACK_ERROR_INJECTION_MESSAGE(THIS_MODULE, __FILE__, __LINE__, __func__, "get_zeroed_page"); \
	else									\
		page_addr = (struct page *)get_zeroed_page(gfp_mask);		\
	if (page_addr && !is_non_trackable_alloc_func(__func__)) {		\
		memtrack_alloc(MEMTRACK_PAGE_ALLOC, 0UL, (unsigned long)(page_addr), 0, 0UL, 0, __FILE__, __LINE__, GFP_ATOMIC); \
	}									\
	(unsigned long)page_addr;						\
})

#define __free_pages(addr, order) ({						\
	void *__memtrack_addr = (void *)addr;					\
										\
	if (__memtrack_addr && !is_non_trackable_alloc_func(__func__)) {	\
		if (!memtrack_check_size(MEMTRACK_PAGE_ALLOC, (unsigned long)(__memtrack_addr), (unsigned long)(order), __FILE__, __LINE__)) \
			memtrack_free(MEMTRACK_PAGE_ALLOC, 0UL, (unsigned long)(__memtrack_addr), 0UL, 0, __FILE__, __LINE__); \
	}									\
	__free_pages(addr, order);						\
})


#define free_pages(addr, order) ({						\
	void *__memtrack_addr = (void *)addr;					\
										\
	if (__memtrack_addr && !is_non_trackable_alloc_func(__func__)) {	\
		if (!memtrack_check_size(MEMTRACK_PAGE_ALLOC, (unsigned long)(__memtrack_addr), (unsigned long)(order), __FILE__, __LINE__)) \
			memtrack_free(MEMTRACK_PAGE_ALLOC, 0UL, (unsigned long)(__memtrack_addr), 0UL, 0, __FILE__, __LINE__); \
	}									\
	free_pages(addr, order);						\
})


#define get_page(addr) ({							\
	void *__memtrack_addr = (void *)addr;					\
										\
	if (__memtrack_addr && !is_non_trackable_alloc_func(__func__)) {	\
		if (memtrack_is_new_addr(MEMTRACK_PAGE_ALLOC, (unsigned long)(__memtrack_addr), 0, __FILE__, __LINE__)) { \
			memtrack_alloc(MEMTRACK_PAGE_ALLOC, 0UL, (unsigned long)(__memtrack_addr), 0, 0UL, 0, __FILE__, __LINE__, GFP_ATOMIC); \
		}								\
	}									\
	get_page(addr);								\
})

#define get_user_pages_fast(start, nr_pages, write, pages) ({			\
	int __memtrack_rc = -1;							\
										\
	if (memtrack_inject_error(THIS_MODULE, __FILE__, "get_user_pages_fast", __func__, __LINE__)) \
		MEMTRACK_ERROR_INJECTION_MESSAGE(THIS_MODULE, __FILE__, __LINE__, __func__, "get_user_pages_fast"); \
	else									\
		__memtrack_rc = get_user_pages_fast(start, nr_pages, write, pages); \
	if (__memtrack_rc > 0 && !is_non_trackable_alloc_func(__func__)) {	\
		int __memtrack_i;						\
										\
		for (__memtrack_i = 0; __memtrack_i < __memtrack_rc; __memtrack_i++) \
			memtrack_alloc(MEMTRACK_PAGE_ALLOC, 0UL, (unsigned long)(pages[__memtrack_i]), 0, 0UL, 0, __FILE__, __LINE__, GFP_ATOMIC); \
	}									\
	__memtrack_rc;								\
})

#define put_page(addr) ({							\
	void *__memtrack_addr = (void *)addr;					\
										\
	if (__memtrack_addr && !is_non_trackable_alloc_func(__func__)) {	\
		/* Check whether this is not part of umem put page & not */\
		/* a new addr and the ref-count is 1 then we'll free this addr */\
		/* Don't change the order these conditions */			\
		if (!is_umem_put_page(__func__) && \
		    !memtrack_is_new_addr(MEMTRACK_PAGE_ALLOC, (unsigned long)(__memtrack_addr), 1, __FILE__, __LINE__) && \
		    (memtrack_get_page_ref_count((unsigned long)(__memtrack_addr)) == 1)) { \
			memtrack_free(MEMTRACK_PAGE_ALLOC, 0UL, (unsigned long)(__memtrack_addr), 0UL, 0, __FILE__, __LINE__); \
		}								\
	}									\
	put_page(addr);								\
})


/* Work-Queue handlers */
#ifdef create_workqueue
	#undef create_workqueue
#endif
#ifdef create_rt_workqueue
	#undef create_rt_workqueue
#endif
#ifdef create_freezeable_workqueue
	#undef create_freezeable_workqueue
#endif
#ifdef create_singlethread_workqueue
	#undef create_singlethread_workqueue
#endif
/* if kernel version < 2.6.37, it's defined in compat as
 * singlethread_workqueue
*/
#if defined(alloc_ordered_workqueue) && LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37)
	#undef alloc_ordered_workqueue
#endif

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 16) /* 2.6.16 */
#ifdef alloc_workqueue
	#undef alloc_workqueue
#endif

#define alloc_workqueue(name, flags, max_active) ({				\
	struct workqueue_struct *wq_addr = NULL;				\
										\
	if (memtrack_inject_error(THIS_MODULE, __FILE__, "alloc_workqueue", __func__, __LINE__)) \
		MEMTRACK_ERROR_INJECTION_MESSAGE(THIS_MODULE, __FILE__, __LINE__, __func__, "alloc_workqueue"); \
	else									\
	wq_addr = __create_workqueue((name), (max_active));				\
	if (wq_addr) {								\
		memtrack_alloc(MEMTRACK_WORK_QUEUE, 0UL, (unsigned long)(wq_addr), 0, 0UL, 0, __FILE__, __LINE__, GFP_ATOMIC); \
	}									\
	wq_addr;								\
})
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 20) /* 2.6.18 - 2.6.19 */
#define create_workqueue(name) ({						\
	struct workqueue_struct *wq_addr = NULL;				\
										\
	if (memtrack_inject_error(THIS_MODULE, __FILE__, "create_workqueue", __func__, __LINE__)) \
		MEMTRACK_ERROR_INJECTION_MESSAGE(THIS_MODULE, __FILE__, __LINE__, __func__, "create_workqueue"); \
	else									\
		wq_addr = __create_workqueue((name), 0);			\
	if (wq_addr) {								\
		memtrack_alloc(MEMTRACK_WORK_QUEUE, 0UL, (unsigned long)(wq_addr), 0, 0UL, 0, __FILE__, __LINE__, GFP_ATOMIC); \
	}									\
	wq_addr;								\
})

#define create_singlethread_workqueue(name) ({					\
	struct workqueue_struct *wq_addr = NULL;				\
										\
	if (memtrack_inject_error(THIS_MODULE, __FILE__, "create_singlethread_workqueue", __func__, __LINE__)) \
		MEMTRACK_ERROR_INJECTION_MESSAGE(THIS_MODULE, __FILE__, __LINE__, __func__, "create_singlethread_workqueue"); \
	else									\
		wq_addr = __create_workqueue((name), 1);			\
	if (wq_addr) {								\
		memtrack_alloc(MEMTRACK_WORK_QUEUE, 0UL, (unsigned long)(wq_addr), 0, 0UL, 0, __FILE__, __LINE__, GFP_ATOMIC); \
	}									\
	wq_addr;								\
})

#elif LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 28) /* 2.6.20 - 2.6.27 */
#define create_workqueue(name) ({						\
	struct workqueue_struct *wq_addr = NULL;				\
										\
	if (memtrack_inject_error(THIS_MODULE, __FILE__, "create_workqueue", __func__, __LINE__)) \
		MEMTRACK_ERROR_INJECTION_MESSAGE(THIS_MODULE, __FILE__, __LINE__, __func__, "create_workqueue"); \
	else									\
		wq_addr = __create_workqueue((name), 0, 0);			\
	if (wq_addr) {								\
		memtrack_alloc(MEMTRACK_WORK_QUEUE, 0UL, (unsigned long)(wq_addr), 0, 0UL, 0, __FILE__, __LINE__, GFP_ATOMIC); \
	}									\
	wq_addr;								\
})

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 22) /* 2.6.20 - 2.6.21 */
#define create_freezeable_workqueue(name) ({					\
	struct workqueue_struct *wq_addr = NULL;				\
										\
	if (memtrack_inject_error(THIS_MODULE, __FILE__, "create_freezeable_workqueue", __func__, __LINE__)) \
		MEMTRACK_ERROR_INJECTION_MESSAGE(THIS_MODULE, __FILE__, __LINE__, __func__, "create_freezeable_workqueue"); \
	else									\
	wq_addr = __create_workqueue((name), 0, 1);				\
	if (wq_addr) {								\
		memtrack_alloc(MEMTRACK_WORK_QUEUE, 0UL, (unsigned long)(wq_addr), 0, 0UL, 0, __FILE__, __LINE__, GFP_ATOMIC); \
	}									\
	wq_addr;								\
})
#else /* 2.6.22 - 2.6.27 */
#define create_freezeable_workqueue(name) ({					\
	struct workqueue_struct *wq_addr = NULL;				\
										\
	if (memtrack_inject_error(THIS_MODULE, __FILE__, "create_freezeable_workqueue", __func__, __LINE__)) \
		MEMTRACK_ERROR_INJECTION_MESSAGE(THIS_MODULE, __FILE__, __LINE__, __func__, "create_freezeable_workqueue"); \
	else									\
		wq_addr = __create_workqueue((name), 1, 1);			\
	if (wq_addr) {								\
		memtrack_alloc(MEMTRACK_WORK_QUEUE, 0UL, (unsigned long)(wq_addr), 0, 0UL, 0, __FILE__, __LINE__, GFP_ATOMIC); \
	}									\
	wq_addr;								\
})
#endif /* 2.6.20 - 2.6.27 */

#define create_singlethread_workqueue(name) ({					\
	struct workqueue_struct *wq_addr = NULL;				\
										\
	if (memtrack_inject_error(THIS_MODULE, __FILE__, "create_singlethread_workqueue", __func__, __LINE__)) \
		MEMTRACK_ERROR_INJECTION_MESSAGE(THIS_MODULE, __FILE__, __LINE__, __func__, "create_singlethread_workqueue"); \
	else									\
		wq_addr = __create_workqueue((name), 1, 0);			\
	if (wq_addr) {								\
		memtrack_alloc(MEMTRACK_WORK_QUEUE, 0UL, (unsigned long)(wq_addr), 0, 0UL, 0, __FILE__, __LINE__, GFP_ATOMIC); \
	}									\
	wq_addr;								\
})

#elif LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 36) /* 2.6.28 - 2.6.35 */

#ifdef alloc_workqueue
	#undef alloc_workqueue
#endif

#define alloc_workqueue(name, flags, max_active) ({				\
	struct workqueue_struct *wq_addr = NULL;				\
										\
	if (memtrack_inject_error(THIS_MODULE, __FILE__, "alloc_workqueue", __func__, __LINE__)) \
		MEMTRACK_ERROR_INJECTION_MESSAGE(THIS_MODULE, __FILE__, __LINE__, __func__, "alloc_workqueue"); \
	else									\
	wq_addr = __create_workqueue((name), (flags), (max_active), 0);				\
	if (wq_addr) {								\
		memtrack_alloc(MEMTRACK_WORK_QUEUE, 0UL, (unsigned long)(wq_addr), 0, 0UL, 0, __FILE__, __LINE__, GFP_ATOMIC); \
	}									\
	wq_addr;								\
})

#define create_workqueue(name) ({						\
	struct workqueue_struct *wq_addr = NULL;				\
										\
	if (memtrack_inject_error(THIS_MODULE, __FILE__, "create_workqueue", __func__, __LINE__)) \
		MEMTRACK_ERROR_INJECTION_MESSAGE(THIS_MODULE, __FILE__, __LINE__, __func__, "create_workqueue"); \
	else									\
	wq_addr = __create_workqueue((name), 0, 0, 0);				\
	if (wq_addr) {								\
		memtrack_alloc(MEMTRACK_WORK_QUEUE, 0UL, (unsigned long)(wq_addr), 0, 0UL, 0, __FILE__, __LINE__, GFP_ATOMIC); \
	}									\
	wq_addr;								\
})

#define create_rt_workqueue(name) ({						\
	struct workqueue_struct *wq_addr = NULL;				\
										\
	if (memtrack_inject_error(THIS_MODULE, __FILE__, "create_rt_workqueue", __func__, __LINE__)) \
		MEMTRACK_ERROR_INJECTION_MESSAGE(THIS_MODULE, __FILE__, __LINE__, __func__, "create_rt_workqueue"); \
	else									\
		wq_addr = __create_workqueue((name), 0, 0, 1);			\
	if (wq_addr) {								\
		memtrack_alloc(MEMTRACK_WORK_QUEUE, 0UL, (unsigned long)(wq_addr), 0, 0UL, 0, __FILE__, __LINE__, GFP_ATOMIC); \
	}									\
	wq_addr;								\
})

#define create_freezeable_workqueue(name) ({					\
	struct workqueue_struct *wq_addr = NULL;				\
										\
	if (memtrack_inject_error(THIS_MODULE, __FILE__, "create_freezeable_workqueue", __func__, __LINE__)) \
		MEMTRACK_ERROR_INJECTION_MESSAGE(THIS_MODULE, __FILE__, __LINE__, __func__, "create_freezeable_workqueue"); \
	else									\
		wq_addr = __create_workqueue((name), 1, 1, 0);			\
	if (wq_addr) {								\
		memtrack_alloc(MEMTRACK_WORK_QUEUE, 0UL, (unsigned long)(wq_addr), 0, 0UL, 0, __FILE__, __LINE__, GFP_ATOMIC); \
	}									\
	wq_addr;								\
})

#define create_singlethread_workqueue(name) ({					\
	struct workqueue_struct *wq_addr = NULL;				\
										\
	if (memtrack_inject_error(THIS_MODULE, __FILE__, "create_singlethread_workqueue", __func__, __LINE__)) \
		MEMTRACK_ERROR_INJECTION_MESSAGE(THIS_MODULE, __FILE__, __LINE__, __func__, "create_singlethread_workqueue"); \
	else									\
		wq_addr = __create_workqueue((name), 1, 0, 0);			\
	if (wq_addr) {								\
		memtrack_alloc(MEMTRACK_WORK_QUEUE, 0UL, (unsigned long)(wq_addr), 0, 0UL, 0, __FILE__, __LINE__, GFP_ATOMIC); \
	}									\
	wq_addr;								\
})
#else /* 2.6.36 */

#ifdef alloc_workqueue
/* In kernels < 5.1, alloc_workqueue was a macro */
#undef alloc_workqueue
#ifdef CONFIG_LOCKDEP
#define alloc_workqueue(name, flags, max_active, args...)			\
({										\
	static struct lock_class_key __key;					\
	const char *__lock_name;						\
	struct workqueue_struct *wq_addr = NULL;				\
										\
	if (__builtin_constant_p(name))						\
		__lock_name = (name);						\
	else									\
		__lock_name = #name;						\
										\
	if (memtrack_inject_error(THIS_MODULE, __FILE__, "alloc_workqueue", __func__, __LINE__)) \
		MEMTRACK_ERROR_INJECTION_MESSAGE(THIS_MODULE, __FILE__, __LINE__, __func__, "alloc_workqueue"); \
	else									\
		wq_addr = __alloc_workqueue_key((name), (flags), (max_active),	\
						&__key, __lock_name, ##args);	\
	if (wq_addr) {								\
		memtrack_alloc(MEMTRACK_WORK_QUEUE, 0UL, (unsigned long)(wq_addr), 0, 0UL, 0, __FILE__, __LINE__, GFP_ATOMIC); \
	}									\
	wq_addr;								\
})
#else
#define alloc_workqueue(name, flags, max_active, args...) ({			\
	struct workqueue_struct *wq_addr = NULL;				\
										\
	if (memtrack_inject_error(THIS_MODULE, __FILE__, "alloc_workqueue", __func__, __LINE__)) \
		MEMTRACK_ERROR_INJECTION_MESSAGE(THIS_MODULE, __FILE__, __LINE__, __func__, "alloc_workqueue"); \
	else									\
		wq_addr = __alloc_workqueue_key((name), (flags), (max_active),	\
						NULL, NULL, ##args);		\
	if (wq_addr) {								\
		memtrack_alloc(MEMTRACK_WORK_QUEUE, 0UL, (unsigned long)(wq_addr), 0, 0UL, 0, __FILE__, __LINE__, GFP_ATOMIC); \
	}									\
	wq_addr;								\
})
#endif
#else
/* In kernels >= 5.1, alloc_workqueue is a function */
#define alloc_workqueue(name, flags, max_active, args...) ({			\
	struct workqueue_struct *wq_addr = NULL;				\
										\
	if (memtrack_inject_error(THIS_MODULE, __FILE__, "alloc_workqueue", __func__, __LINE__)) \
		MEMTRACK_ERROR_INJECTION_MESSAGE(THIS_MODULE, __FILE__, __LINE__, __func__, "alloc_workqueue"); \
	else									\
		wq_addr = alloc_workqueue(name, flags, max_active, ##args);	\
	if (wq_addr) {								\
		memtrack_alloc(MEMTRACK_WORK_QUEUE, 0UL, (unsigned long)(wq_addr), 0, 0UL, 0, __FILE__, __LINE__, GFP_ATOMIC); \
	}									\
	wq_addr;								\
})
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0))
#define WQ_RESCUER 1 << 7 /* internal: workqueue has rescuer */
#endif

#define create_workqueue(name)							\
	alloc_workqueue((name), WQ_RESCUER, 1);

#define create_freezeable_workqueue(name)					\
	alloc_workqueue((name), WQ_FREEZEABLE | WQ_UNBOUND | WQ_RESCUER, 1);

#define create_singlethread_workqueue(name)					\
	alloc_workqueue((name), WQ_UNBOUND | WQ_RESCUER, 1);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 0)
#define alloc_ordered_workqueue(name, flags, args...)				\
	alloc_workqueue((name), WQ_UNBOUND | __WQ_ORDERED | (flags), 1, ##args)
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3, 3, 0)
#define alloc_ordered_workqueue(name, flags, args...)				\
	alloc_workqueue((name), WQ_UNBOUND | (flags), 1, ##args)
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
#define alloc_ordered_workqueue(name, flags)					\
	alloc_workqueue((name), WQ_UNBOUND | (flags), 1)
#endif /* alloc_ordered_workqueue */

#endif /* Work-Queue Kernel Versions */

#define destroy_workqueue(wq_addr) ({						\
	void *__memtrack_addr = (void *)wq_addr;				\
										\
	if (__memtrack_addr) {							\
		memtrack_free(MEMTRACK_WORK_QUEUE, 0UL, (unsigned long)(__memtrack_addr), 0UL, 0, __FILE__, __LINE__); \
	}									\
	destroy_workqueue(wq_addr);						\
})

/* ONLY error injection to functions that we don't monitor */
#define alloc_skb(size, prio) ({ \
	struct sk_buff *__memtrack_skb = NULL;					\
										\
	if (memtrack_inject_error(THIS_MODULE, __FILE__, "alloc_skb", __func__, __LINE__)) \
		MEMTRACK_ERROR_INJECTION_MESSAGE(THIS_MODULE, __FILE__, __LINE__, __func__, "alloc_skb"); \
	else									\
		 __memtrack_skb = alloc_skb(size, prio);			\
	__memtrack_skb;								\
})

#define dev_alloc_skb(size) ({							\
	struct sk_buff *__memtrack_skb = NULL;					\
										\
	if (memtrack_inject_error(THIS_MODULE, __FILE__, "dev_alloc_skb", __func__, __LINE__)) \
		MEMTRACK_ERROR_INJECTION_MESSAGE(THIS_MODULE, __FILE__, __LINE__, __func__, "dev_alloc_skb"); \
	else									\
		__memtrack_skb = dev_alloc_skb(size);				\
	__memtrack_skb;								\
})

#define alloc_skb_fclone(size, prio) ({						\
	struct sk_buff *__memtrack_skb = NULL;					\
										\
	if (memtrack_inject_error(THIS_MODULE, __FILE__, "alloc_skb_fclone", __func__, __LINE__)) \
		MEMTRACK_ERROR_INJECTION_MESSAGE(THIS_MODULE, __FILE__, __LINE__, __func__, "alloc_skb_fclone"); \
	else									\
		__memtrack_skb = alloc_skb_fclone(size, prio);			\
	__memtrack_skb;								\
})

#define copy_from_user(to, from, n) ({						\
	int ret = n;								\
										\
	if (memtrack_inject_error(THIS_MODULE, __FILE__, "copy_from_user", __func__, __LINE__)) \
		MEMTRACK_ERROR_INJECTION_MESSAGE(THIS_MODULE, __FILE__, __LINE__, __func__, "copy_from_user"); \
	else									\
		ret = copy_from_user(to, from, n);				\
	ret;									\
})

#define copy_to_user(to, from, n) ({						\
	int ret = n;								\
										\
	if (memtrack_inject_error(THIS_MODULE, __FILE__, "copy_to_user", __func__, __LINE__)) \
		MEMTRACK_ERROR_INJECTION_MESSAGE(THIS_MODULE, __FILE__, __LINE__, __func__, "copy_to_user"); \
	else									\
		ret = copy_to_user(to, from, n);				\
	ret;									\
})

#define sysfs_create_file(kobj, attr) ({						\
	int ret = -ENOSYS;							\
										\
	if (memtrack_inject_error(THIS_MODULE, __FILE__, "sysfs_create_file", __func__, __LINE__)) \
		MEMTRACK_ERROR_INJECTION_MESSAGE(THIS_MODULE, __FILE__, __LINE__, __func__, "sysfs_create_file"); \
	else									\
		ret = sysfs_create_file(kobj, attr);				\
	ret;									\
})

#define sysfs_create_link(kobj, target, name) ({				\
	int ret = -ENOSYS;							\
										\
	if (memtrack_inject_error(THIS_MODULE, __FILE__, "sysfs_create_link", __func__, __LINE__)) \
		MEMTRACK_ERROR_INJECTION_MESSAGE(THIS_MODULE, __FILE__, __LINE__, __func__, "sysfs_create_link"); \
	else									\
		ret = sysfs_create_link(kobj, target, name);			\
	ret;									\
})

#define sysfs_create_group(kobj, grp) ({					\
	int ret = -ENOSYS;							\
										\
	if (memtrack_inject_error(THIS_MODULE, __FILE__, "sysfs_create_group", __func__, __LINE__)) \
		MEMTRACK_ERROR_INJECTION_MESSAGE(THIS_MODULE, __FILE__, __LINE__, __func__, "sysfs_create_group"); \
	else									\
		ret = sysfs_create_group(kobj, grp);				\
	ret;									\
})

#endif /* __mtrack_h_ */

