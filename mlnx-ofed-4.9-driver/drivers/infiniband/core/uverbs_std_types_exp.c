
/*
 * Copyright (c) 2017, Mellanox Technologies inc.  All rights reserved.
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

#include <rdma/uverbs_std_types.h>
#include <rdma/ib_user_verbs.h>
#include <rdma/ib_verbs.h>
#include <linux/bug.h>
#include <linux/file.h>
#include "rdma_core.h"
#include "uverbs.h"
#include "uverbs_exp.h"


static int uverbs_exp_free_dct(struct ib_uobject *uobject,
			       enum rdma_remove_reason why, 
			       struct uverbs_attr_bundle *attrs)
{
	struct ib_dct  *dct = uobject->object;
	struct ib_udct_object *udct =
		container_of(uobject, struct ib_udct_object, uevent.uobject);
	int ret;

	ret = ib_exp_destroy_dct(dct, &attrs->driver_udata);
	if (ret && why == RDMA_REMOVE_DESTROY)
		return ret;

	ib_uverbs_release_uevent(&udct->uevent);

	return ret;
}
DECLARE_UVERBS_NAMED_OBJECT(UVERBS_OBJECT_DCT,
			    UVERBS_TYPE_ALLOC_IDR_SZ(sizeof(struct ib_udct_object),
                            uverbs_exp_free_dct));

const struct uapi_definition uverbs_def_obj_dct[] = {
	UAPI_DEF_CHAIN_OBJ_TREE_NAMED(UVERBS_OBJECT_DCT,
				      UAPI_DEF_OBJ_NEEDS_FN(exp_destroy_dct)),
	{}
};

