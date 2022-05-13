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

#ifndef SYNC_MEM_H
#define SYNC_MEM_H

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/export.h>

#define IB_SYNC_MEMORY_NAME_MAX 64
#define IB_SYNC_MEMORY_VER_MAX 16

/**
 *  struct sync_memory_client - registration information for sync client.
 *  @name:	sync client name
 *  @version:	sync client version
 *  @rsync:	callback function to be used by IB core to detect whether a
 *		virtual address in under the responsibility of a specific
 *		sync client.
 *  @flags:	Currently only relaxed ordering flag
 *
 *  The subsections in this description contain detailed description
 *  of the callback arguments and expected return values for the
 *  callbacks defined in this struct.
 *
 *	rsync:
 *
 *		Callback function to be used by IB core asking the
 *		sync client to ensure all physical pages written
 *		by the HCA to device memory have been flushed to memory.
 *
 *		addr	[IN] - start virtual address of that given allocation.
 *
 *		size	[IN] - size of memory area starting at addr.
 *
 **/
struct sync_memory_client {
	char	name[IB_SYNC_MEMORY_NAME_MAX];
	char	version[IB_SYNC_MEMORY_VER_MAX];
	int     (*rsync)(void *handle, struct mm_struct *mm, unsigned long addr,
			 size_t size);
	size_t  size;
};

void *ib_register_sync_memory_client(const struct sync_memory_client *sync_client,
				     int flags);
int ib_unregister_sync_memory_client(void *reg_handle);

#endif
