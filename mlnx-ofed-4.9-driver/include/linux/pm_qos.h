#ifndef _COMPAT_LINUX_PM_QOS_H
#define _COMPAT_LINUX_PM_QOS_H 1

#include "../../compat/config.h"
#include <linux/slab.h>

#include_next <linux/pm_qos.h>

#if !defined(HAVE_PM_QOS_UPDATE_USER_LATENCY_TOLERANCE_EXPORTED)
#if defined(HAVE_DEV_PM_QOS_RESUME_LATENCY) && \
    defined(HAVE_DEV_PM_QOS_LATENCY_TOLERANCE) && \
    defined(HAVE_PM_QOS_LATENCY_TOLERANCE_NO_CONSTRAINT)
#define dev_pm_qos_drop_user_request LINUX_BACKPORT(dev_pm_qos_drop_user_request)
static inline void dev_pm_qos_drop_user_request(struct device *dev,
						enum dev_pm_qos_req_type type)
{
	struct dev_pm_qos_request *req = NULL;

	switch(type) {
	case DEV_PM_QOS_RESUME_LATENCY:
		req = dev->power.qos->resume_latency_req;
		dev->power.qos->resume_latency_req = NULL;
		break;
	case DEV_PM_QOS_LATENCY_TOLERANCE:
		req = dev->power.qos->latency_tolerance_req;
		dev->power.qos->latency_tolerance_req = NULL;
		break;
	case DEV_PM_QOS_FLAGS:
		req = dev->power.qos->flags_req;
		dev->power.qos->flags_req = NULL;
		break;
	}
	dev_pm_qos_remove_request(req);
	kfree(req);
}

#define dev_pm_qos_update_user_latency_tolerance LINUX_BACKPORT(dev_pm_qos_update_user_latency_tolerance)
static inline int dev_pm_qos_update_user_latency_tolerance(struct device *dev, s32 val)
{

	int ret;

	/*
	 * each original private function on kernel's function
	 * is replaced here by an exported version of the function
	 * (which calls the private one under a mutex lock).
	 */

	if (IS_ERR_OR_NULL(dev->power.qos)
		|| !dev->power.qos->latency_tolerance_req) {
		struct dev_pm_qos_request *req;

		if (val < 0) {
			if (val == PM_QOS_LATENCY_TOLERANCE_NO_CONSTRAINT)
				ret = 0;
			else
				ret = -EINVAL;
			goto out;
		}
		req = kzalloc(sizeof(*req), GFP_KERNEL);
		if (!req) {
			ret = -ENOMEM;
			goto out;
		}
		ret = dev_pm_qos_add_request(dev, req, DEV_PM_QOS_LATENCY_TOLERANCE, val);
		if (ret < 0) {
			kfree(req);
			goto out;
		}
		dev->power.qos->latency_tolerance_req = req;
	} else {
		if (val < 0) {
			dev_pm_qos_drop_user_request(dev, DEV_PM_QOS_LATENCY_TOLERANCE);
			ret = 0;
		} else {
			ret = dev_pm_qos_update_request(dev->power.qos->latency_tolerance_req, val);
		}
	}

out:
	return ret;
}
#endif
#endif
#endif /* _COMPAT_LINUX_PM_QOS_H */
