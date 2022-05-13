// SPDX-License-Identifier: GPL-2.0-only
/*
 * Mediated device Core Driver
 *
 * Copyright (c) 2016, NVIDIA CORPORATION. All rights reserved.
 *     Author: Neo Jia <cjia@nvidia.com>
 *             Kirti Wankhede <kwankhede@nvidia.com>
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/uuid.h>
#include <linux/sysfs.h>
#include <linux/mdev.h>

#include "mdev_private.h"

#define DRIVER_VERSION		"0.1"
#define DRIVER_AUTHOR		"NVIDIA Corporation"
#define DRIVER_DESC		"Mediated device Core Driver"

static LIST_HEAD(parent_list);
static DEFINE_MUTEX(parent_list_lock);
static struct class_compat *mdev_bus_compat_class;

static LIST_HEAD(mdev_list);
static DEFINE_MUTEX(mdev_list_lock);

struct device *mdev_parent_dev(struct mdev_device *mdev)
{
	return mdev->parent->dev;
}
EXPORT_SYMBOL(mdev_parent_dev);

void *mdev_get_drvdata(struct mdev_device *mdev)
{
	return mdev->driver_data;
}
EXPORT_SYMBOL(mdev_get_drvdata);

void mdev_set_drvdata(struct mdev_device *mdev, void *data)
{
	mdev->driver_data = data;
}
EXPORT_SYMBOL(mdev_set_drvdata);

struct device *mdev_dev(struct mdev_device *mdev)
{
	return &mdev->dev;
}
EXPORT_SYMBOL(mdev_dev);

struct mdev_device *mdev_from_dev(struct device *dev)
{
	return dev_is_mdev(dev) ? to_mdev_device(dev) : NULL;
}
EXPORT_SYMBOL(mdev_from_dev);

const guid_t *mdev_uuid(struct mdev_device *mdev)
{
	return &mdev->uuid;
}
EXPORT_SYMBOL(mdev_uuid);

/* Should be called holding parent_list_lock */
static struct mdev_parent *__find_parent_device(struct device *dev)
{
	struct mdev_parent *parent;

	list_for_each_entry(parent, &parent_list, next) {
		if (parent->dev == dev)
			return parent;
	}
	return NULL;
}

static bool mdev_try_get_parent(struct mdev_parent *parent)
{
	if (parent)
		return refcount_inc_not_zero(&parent->refcount);
	return false;
}

static void mdev_put_parent(struct mdev_parent *parent)
{
	if (parent && refcount_dec_and_test(&parent->refcount))
		complete(&parent->unreg_completion);
}

static void mdev_device_remove_common(struct mdev_device *mdev)
{
	struct mdev_parent *parent;
	struct mdev_type *type;
	int ret;

	type = to_mdev_type(mdev->type_kobj);
	mdev_remove_sysfs_files(&mdev->dev, type);
	device_del(&mdev->dev);
	parent = mdev->parent;
	ret = parent->ops->remove(mdev);
	if (ret)
		dev_err(&mdev->dev, "Remove failed: err=%d\n", ret);

	/* Balances with device_initialize() */
	put_device(&mdev->dev);
}

static int mdev_device_remove_cb(struct device *dev, void *data)
{
	if (dev_is_mdev(dev))
		mdev_device_remove_common(to_mdev_device(dev));

	return 0;
}

/*
 * mdev_register_device : Register a device
 * @dev: device structure representing parent device.
 * @ops: Parent device operation structure to be registered.
 *
 * Add device to list of registered parent devices.
 * Returns a negative value on error, otherwise 0.
 */
int mdev_register_device(struct device *dev, const struct mdev_parent_ops *ops)
{
	int ret;
	struct mdev_parent *parent;

	/* check for mandatory ops */
	if (!ops || !ops->create || !ops->remove || !ops->supported_type_groups)
		return -EINVAL;

	dev = get_device(dev);
	if (!dev)
		return -EINVAL;

	mutex_lock(&parent_list_lock);

	/* Check for duplicate */
	parent = __find_parent_device(dev);
	if (parent) {
		parent = NULL;
		ret = -EEXIST;
		goto add_dev_err;
	}

	parent = kzalloc(sizeof(*parent), GFP_KERNEL);
	if (!parent) {
		ret = -ENOMEM;
		goto add_dev_err;
	}

	refcount_set(&parent->refcount, 1);
	init_completion(&parent->unreg_completion);

	parent->dev = dev;
	parent->ops = ops;

	if (!mdev_bus_compat_class) {
		mdev_bus_compat_class = class_compat_register("mdev_bus");
		if (!mdev_bus_compat_class) {
			ret = -ENOMEM;
			goto add_dev_err;
		}
	}

	ret = parent_create_sysfs_files(parent);
	if (ret)
		goto add_dev_err;

	ret = class_compat_create_link(mdev_bus_compat_class, dev, NULL);
	if (ret)
		dev_warn(dev, "Failed to create compatibility class link\n");

	list_add(&parent->next, &parent_list);
	mutex_unlock(&parent_list_lock);

	dev_info(dev, "MDEV: Registered\n");
	return 0;

add_dev_err:
	mutex_unlock(&parent_list_lock);
	if (parent)
		mdev_put_parent(parent);
	else
		put_device(dev);
	return ret;
}
EXPORT_SYMBOL(mdev_register_device);

/*
 * mdev_unregister_device : Unregister a parent device
 * @dev: device structure representing parent device.
 *
 * Remove device from list of registered parent devices. Give a chance to free
 * existing mediated devices for given device.
 */

void mdev_unregister_device(struct device *dev)
{
	struct mdev_parent *parent;

	mutex_lock(&parent_list_lock);
	parent = __find_parent_device(dev);

	if (!parent) {
		mutex_unlock(&parent_list_lock);
		return;
	}
	dev_info(dev, "MDEV: Unregistering\n");

	list_del(&parent->next);
	mutex_unlock(&parent_list_lock);

	/* Release the initial reference so that new create cannot start */
	mdev_put_parent(parent);

	/*
	 * Wait for all the create and remove references to drop.
	 */
	wait_for_completion(&parent->unreg_completion);

	/*
	 * New references cannot be taken and all users are done
	 * using the parent. So it is safe to unregister parent.
	 */
	class_compat_remove_link(mdev_bus_compat_class, dev, NULL);

	device_for_each_child(dev, NULL, mdev_device_remove_cb);

	parent_remove_sysfs_files(parent);
	kfree(parent);
	put_device(dev);
}
EXPORT_SYMBOL(mdev_unregister_device);

static void mdev_device_release(struct device *dev)
{
	struct mdev_device *mdev = to_mdev_device(dev);

	mutex_lock(&mdev_list_lock);
	list_del(&mdev->next);
	mutex_unlock(&mdev_list_lock);

	dev_dbg(&mdev->dev, "MDEV: destroying\n");
	kfree(mdev);
}

int mdev_device_create(struct kobject *kobj,
		       struct device *dev, const guid_t *uuid)
{
	int ret;
	struct mdev_device *mdev, *tmp;
	struct mdev_parent *parent;
	struct mdev_type *type = to_mdev_type(kobj);

	if (!mdev_try_get_parent(type->parent))
		return -EINVAL;

	parent = type->parent;

	mutex_lock(&mdev_list_lock);

	/* Check for duplicate */
	list_for_each_entry(tmp, &mdev_list, next) {
		if (guid_equal(&tmp->uuid, uuid)) {
			mutex_unlock(&mdev_list_lock);
			ret = -EEXIST;
			goto mdev_fail;
		}
	}

	mdev = kzalloc(sizeof(*mdev), GFP_KERNEL);
	if (!mdev) {
		mutex_unlock(&mdev_list_lock);
		ret = -ENOMEM;
		goto mdev_fail;
	}

	guid_copy(&mdev->uuid, uuid);
	list_add(&mdev->next, &mdev_list);
	mutex_unlock(&mdev_list_lock);

	mdev->parent = parent;

	device_initialize(&mdev->dev);
	mdev->dev.parent  = dev;
	mdev->dev.bus     = &mdev_bus_type;
	mdev->dev.release = mdev_device_release;
	dev_set_name(&mdev->dev, "%pUl", uuid);
	mdev->dev.groups = parent->ops->mdev_attr_groups;
	mdev->type_kobj = kobj;

	ret = parent->ops->create(kobj, mdev);
	if (ret)
		goto ops_create_fail;

	ret = device_add(&mdev->dev);
	if (ret)
		goto add_fail;

	ret = mdev_create_sysfs_files(&mdev->dev, type);
	if (ret)
		goto sysfs_fail;

	mdev->active = true;
	dev_dbg(&mdev->dev, "MDEV: created\n");
	mdev_put_parent(parent);

	return 0;

sysfs_fail:
	device_del(&mdev->dev);
add_fail:
	parent->ops->remove(mdev);
ops_create_fail:
	put_device(&mdev->dev);
mdev_fail:
	mdev_put_parent(parent);
	return ret;
}

int mdev_device_remove(struct device *dev)
{
	struct mdev_device *mdev, *tmp;
	struct mdev_parent *parent;
	struct mdev_type *type;

	mdev = to_mdev_device(dev);

	mutex_lock(&mdev_list_lock);
	list_for_each_entry(tmp, &mdev_list, next) {
		if (tmp == mdev)
			break;
	}

	if (tmp != mdev) {
		mutex_unlock(&mdev_list_lock);
		return -ENODEV;
	}

	if (!mdev->active) {
		mutex_unlock(&mdev_list_lock);
		return -EAGAIN;
	}

	mdev->active = false;
	mutex_unlock(&mdev_list_lock);

	type = to_mdev_type(mdev->type_kobj);
	if (!mdev_try_get_parent(type->parent)) {
		/*
		 * Parent unregistration have started.
		 * No need to remove here.
		 */
		return -ENODEV;
	}

	parent = mdev->parent;
	mdev_device_remove_common(mdev);
	mdev_put_parent(parent);

	return 0;
}

int mdev_set_iommu_device(struct device *dev, struct device *iommu_device)
{
	struct mdev_device *mdev = to_mdev_device(dev);

	mdev->iommu_device = iommu_device;

	return 0;
}
EXPORT_SYMBOL(mdev_set_iommu_device);

struct device *mdev_get_iommu_device(struct device *dev)
{
	struct mdev_device *mdev = to_mdev_device(dev);

	return mdev->iommu_device;
}
EXPORT_SYMBOL(mdev_get_iommu_device);

static int __init mdev_init(void)
{
	return mdev_bus_register();
}

static void __exit mdev_exit(void)
{
	if (mdev_bus_compat_class)
		class_compat_unregister(mdev_bus_compat_class);

	mdev_bus_unregister();
}

module_init(mdev_init)
module_exit(mdev_exit)

MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_SOFTDEP("post: vfio_mdev");
