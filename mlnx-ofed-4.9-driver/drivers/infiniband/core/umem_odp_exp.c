/*
 * Copyright (c) 2014 Mellanox Technologies. All rights reserved.
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
#ifndef UMEM_ODP_EXP_H
#define UMEM_ODP_EXP_H

#include "uverbs.h"
#include "umem_odp_exp.h"

static ssize_t show_num_page_fault_pages(struct device *device,
					 struct device_attribute *attr,
					 char *buf)
{
	struct ib_uverbs_device *dev =
			container_of(device, struct ib_uverbs_device, dev);

	return sprintf(buf, "%d\n",
		       atomic_read(&dev->ib_dev->odp_statistics.num_page_fault_pages));
}
static DEVICE_ATTR(num_page_fault_pages, S_IRUGO,
		   show_num_page_fault_pages, NULL);

static ssize_t show_num_invalidation_pages(struct device *device,
					   struct device_attribute *attr,
					   char *buf)
{
	struct ib_uverbs_device *dev =
			container_of(device, struct ib_uverbs_device, dev);

	return sprintf(buf, "%d\n",
		       atomic_read(&dev->ib_dev->odp_statistics.num_invalidation_pages));
}
static DEVICE_ATTR(num_invalidation_pages, S_IRUGO,
		   show_num_invalidation_pages, NULL);

static ssize_t show_num_invalidations(struct device *device,
				      struct device_attribute *attr,
				      char *buf)
{
	struct ib_uverbs_device *dev =
			container_of(device, struct ib_uverbs_device, dev);

	return sprintf(buf, "%d\n",
		       atomic_read(&dev->ib_dev->odp_statistics.num_invalidations));
}
static DEVICE_ATTR(num_invalidations, S_IRUGO, show_num_invalidations, NULL);

static ssize_t show_invalidations_faults_contentions(struct device *device,
						     struct device_attribute *attr,
						     char *buf)
{
	struct ib_uverbs_device *dev =
			container_of(device, struct ib_uverbs_device, dev);

	return sprintf(buf, "%d\n",
		       atomic_read(&dev->ib_dev->odp_statistics.invalidations_faults_contentions));
}
static DEVICE_ATTR(invalidations_faults_contentions, S_IRUGO,
		   show_invalidations_faults_contentions, NULL);

static ssize_t show_num_page_faults(struct device *device,
				    struct device_attribute *attr,
				    char *buf)
{
	struct ib_uverbs_device *dev =
			container_of(device, struct ib_uverbs_device, dev);

	return sprintf(buf, "%d\n",
		       atomic_read(&dev->ib_dev->odp_statistics.num_page_faults));
}
static DEVICE_ATTR(num_page_faults, S_IRUGO, show_num_page_faults, NULL);

static ssize_t show_num_prefetches_handled(struct device *device,
					   struct device_attribute *attr,
					   char *buf)
{
	struct ib_uverbs_device *dev =
			container_of(device, struct ib_uverbs_device, dev);

	return sprintf(buf, "%d\n",
		       atomic_read(&dev->ib_dev->odp_statistics.num_prefetches_handled));
}
static DEVICE_ATTR(num_prefetches_handled, S_IRUGO,
		   show_num_prefetches_handled, NULL);

static ssize_t show_num_prefetch_pages(struct device *device,
				       struct device_attribute *attr,
				       char *buf)
{
	struct ib_uverbs_device *dev =
			container_of(device, struct ib_uverbs_device, dev);

	return sprintf(buf, "%d\n",
		       atomic_read(&dev->ib_dev->odp_statistics.num_prefetch_pages));
}
static DEVICE_ATTR(num_prefetch_pages, S_IRUGO,
		   show_num_prefetch_pages, NULL);

int ib_umem_odp_add_statistic_nodes(struct device *dev)
{
	int ret;

	ret = device_create_file(dev, &dev_attr_num_page_fault_pages);
	if (ret)
		return ret;
	ret = device_create_file(dev, &dev_attr_num_invalidation_pages);
	if (ret)
		return ret;
	ret = device_create_file(dev, &dev_attr_num_invalidations);
	if (ret)
		return ret;
	ret = device_create_file(dev,
				 &dev_attr_invalidations_faults_contentions);
	if (ret)
		return ret;
	ret = device_create_file(dev, &dev_attr_num_page_faults);
	if (ret)
		return ret;
	ret = device_create_file(dev, &dev_attr_num_prefetches_handled);
	if (ret)
		return ret;
	ret = device_create_file(dev, &dev_attr_num_prefetch_pages);
	if (ret)
		return ret;

	return 0;
}
EXPORT_SYMBOL(ib_umem_odp_add_statistic_nodes);
#endif
