#ifndef COMPAT_CDEV_H
#define COMPAT_CDEV_H

#include "../../compat/config.h"

#include_next <linux/cdev.h>

#ifndef HAVE_CDEV_SET_PARENT
#include <linux/device.h>

#define cdev_set_parent LINUX_BACKPORT(cdev_set_parent)
static inline void cdev_set_parent(struct cdev *p, struct kobject *kobj)
{
	WARN_ON(!kobj->state_initialized);
	p->kobj.parent = kobj;
}

#define cdev_device_add LINUX_BACKPORT(cdev_device_add)
static inline int cdev_device_add(struct cdev *cdev, struct device *dev)
{
	int rc = 0;

	if (dev->devt) {
		cdev_set_parent(cdev, &dev->kobj);

		rc = cdev_add(cdev, dev->devt, 1);
		if (rc)
			return rc;
	}

	rc = device_add(dev);
	if (rc)
		cdev_del(cdev);

	return rc;
}

#define cdev_device_del LINUX_BACKPORT(cdev_device_del)
static inline void cdev_device_del(struct cdev *cdev, struct device *dev)
{
	device_del(dev);
	if (dev->devt)
		cdev_del(cdev);
}

#endif /* HAVE_CDEV_SET_PARENT */

#endif /* COMPAT_CDEV_H */
