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

#if !defined(PEER_MEM_H)
#define PEER_MEM_H

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/export.h>
#include <linux/scatterlist.h>

#define IB_PEER_MEMORY_NAME_MAX 64
#define IB_PEER_MEMORY_VER_MAX 16
#define PEER_MEM_U64_CORE_CONTEXT

/**
 *  struct peer_memory_client - registration information for peer client.
 *  @name:	peer client name
 *  @version:	peer client version
 *  @acquire:	callback function to be used by IB core to detect whether a
 *		virtual address in under the responsibility of a specific peer client.
 *  @get_pages: callback function to be used by IB core asking the peer client to pin
 *		the physical pages of the given address range and returns that information.
 *		It equivalents to the kernel API of get_user_pages(), but targets peer memory.
 *  @dma_map:	callback function to be used by IB core asking the peer client to fill
 *		the dma address mapping for a given address range.
 *  @dma_unmap:	callback function to be used by IB core asking the peer client to take
 *		relevant actions to unmap the memory.
 *  @put_pages:	callback function to be used by IB core asking the peer client to remove the
 *		pinning from the given memory.
 *		It's the peer-direct equivalent of the kernel API put_page.
 *  @get_page_size: callback function to be used by IB core to query the peer client for
 *		    the page size for the given allocation.
 *  @release:	callback function to be used by IB core asking peer client to release all
 *		resources associated with previous acquire call. The call will be performed
 *		only for contexts that have been successfully acquired (i.e. acquire returned a non-zero value).
 *              Additionally, IB core guarentees that there will be no pages pinned through this context when the callback is called.
 *
 *  The subsections in this description contain detailed description
 *  of the callback arguments and expected return values for the
 *  callbacks defined in this struct.
 *
 *	acquire:
 *
 *              Callback function to be used by IB core to detect
 *		whether a virtual address in under the responsibility
 *		of a specific peer client.
 *
 *		addr	[IN] - virtual address to be checked whether belongs to peer.
 *
 *		size	[IN] - size of memory area starting at addr.
 *
 *		peer_mem_private_data [IN] - The contents of ib_ucontext-> peer_mem_private_data.
 *					      This parameter allows usage of the peer-direct
 *                                            API in implementations where it is impossible
 *                                            to detect if the memory belongs to the device
 *                                            based upon the virtual address alone. In such
 *                                            cases, the peer device can create a special
 *                                            ib_ucontext, which will be associated with the
 *                                            relevant peer memory.
 *
 *		peer_mem_name         [IN] - The contents of ib_ucontext-> peer_mem_name.
 *					      Used to identify the peer memory client that
 *                                            initialized the ib_ucontext.
 *                                            This parameter is normally used along with
 *                                            peer_mem_private_data.
 *		client_context        [OUT] - peer opaque data which holds a peer context for
 *                                             the acquired address range, will be provided
 *                                             back to the peer memory in subsequent
 *                                             calls for that given memory.
 *
 *		If peer takes responsibility on the given address range further calls for memory management
 *		will be directed to the callbacks of this peer client.
 *
 *		Return - 1 in case peer client takes responsibility on that range otherwise 0.
 *			Any peer internal error should resulted in a zero answer, in case address range
 *			really belongs to the peer, no owner will be found and application will get an error
 *			from IB Core as expected.
 *
 *	get_pages:
 *
 *              Callback function to be used by IB core asking the
 *		peer client to pin the physical pages of the given
 *		address range and returns that information.  It
 *		equivalents to the kernel API of get_user_pages(), but
 *		targets peer memory.
 *
 *		addr           [IN] - start virtual address of that given allocation.
 *
 *		size           [IN] - size of memory area starting at addr.
 *
 *		write          [IN] - indicates whether the pages will be written to by the caller.
 *                                    Same meaning as of kernel API get_user_pages, can be
 *                                    ignored if not relevant.
 *
 *		force          [IN] - indicates whether to force write access even if user
 *                                    mapping is read only. Same meaning as of kernel API
 *                                    get_user_pages, can be ignored if not relevant.
 *
 *		sg_head        [IN/OUT] - pointer to head of struct sg_table.
 *                                        The peer client should allocate a table big
 *                                        enough to store all of the required entries. This
 *                                        function should fill the table with physical addresses
 *                                        and sizes of the memory segments composing this
 *                                        memory mapping.
 *                                        The table allocation can be done using sg_alloc_table.
 *                                        Filling in the physical memory addresses and size can
 *                                        be done using sg_set_page.
 *
 *		client_context [IN] - peer context for the given allocation, as received from
 *                                     the acquire call.
 *
 *		core_context   [IN] - IB core context. If the peer client wishes to
 *                                     invalidate any of the pages pinned through this API,
 *                                     it must provide this context as an argument to the
 *                                     invalidate callback.
 *
 *		Return - 0 success, otherwise errno error code.
 *
 *	dma_map:
 *
 *              Callback function to be used by IB core asking the peer client to fill
 *		the dma address mapping for a given address range.
 *
 *		sg_head        [IN/OUT] - pointer to head of struct sg_table. The peer memory
 *                                        should fill the dma_address & dma_length for
 *                                        each scatter gather entry in the table.
 *
 *		client_context [IN] - peer context for the allocation mapped.
 *
 *		dma_device     [IN] - the RDMA capable device which requires access to the
 *				      peer memory.
 *
 *		dmasync        [IN] - flush in-flight DMA when the memory region is written.
 *				      Same meaning as with host memory mapping, can be ignored if not relevant.
 *
 *		nmap           [OUT] - number of mapped/set entries.
 *
 *		Return - 0 success, otherwise errno error code.
 *
 *	dma_unmap:
 *
 *              Callback function to be used by IB core asking the peer client to take
 *		relevant actions to unmap the memory.
 *
 *		sg_head        [IN] - pointer to head of struct sg_table. The peer memory
 *				       should fill the dma_address & dma_length for
 *				       each scatter gather entry in the table.
 *
 *		client_context [IN] - peer context for the allocation mapped.
 *
 *		dma_device     [IN] - the RDMA capable device which requires access to the
 *				       peer memory.
 *
 *		Return -  0 success, otherwise errno error code.
 *
 *	put_pages:
 *
 *              Callback function to be used by IB core asking the peer client to remove the
 *		pinning from the given memory.
 *		It's the peer-direct equivalent of the kernel API put_page.
 *
 *		sg_head        [IN] - pointer to head of struct sg_table.
 *
 *		client_context [IN] - peer context for that given allocation.
 *
 *	get_page_size:
 *
 *              Callback function to be used by IB core to query the
 *		peer client for the page size for the given
 *		allocation.
 *
 *		sg_head        [IN] - pointer to head of struct sg_table.
 *
 *		client_context [IN] - peer context for that given allocation.
 *
 *		Return -  Page size in bytes
 *
 *	release:
 *
 *              Callback function to be used by IB core asking peer
 *		client to release all resources associated with
 *		previous acquire call. The call will be performed only
 *		for contexts that have been successfully acquired
 *		(i.e. acquire returned a non-zero value).
 *		Additionally, IB core guarentees that there will be no
 *		pages pinned through this context when the callback is
 *		called.
 *
 *		client_context [IN] - peer context for the given allocation.
 *
 **/
struct peer_memory_client {
	char	name[IB_PEER_MEMORY_NAME_MAX];
	char	version[IB_PEER_MEMORY_VER_MAX];
	int (*acquire)(unsigned long addr, size_t size, void *peer_mem_private_data,
		       char *peer_mem_name, void **client_context);
	int (*get_pages)(unsigned long addr,
			 size_t size, int write, int force,
			 struct sg_table *sg_head,
			 void *client_context, u64 core_context);
	int (*dma_map)(struct sg_table *sg_head, void *client_context,
		       struct device *dma_device, int dmasync, int *nmap);
	int (*dma_unmap)(struct sg_table *sg_head, void *client_context,
			 struct device  *dma_device);
	void (*put_pages)(struct sg_table *sg_head, void *client_context);
	unsigned long (*get_page_size)(void *client_context);
	void (*release)(void *client_context);
	void* (*get_context_private_data)(u64 peer_id);
	void (*put_context_private_data)(void *context);
};

typedef int (*invalidate_peer_memory)(void *reg_handle, u64 core_context);

void *ib_register_peer_memory_client(const struct peer_memory_client *peer_client,
				     invalidate_peer_memory *invalidate_callback);
void ib_unregister_peer_memory_client(void *reg_handle);

#endif
