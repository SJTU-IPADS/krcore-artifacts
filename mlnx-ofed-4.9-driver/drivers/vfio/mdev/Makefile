# SPDX-License-Identifier: GPL-2.0-only

mdev-y := mdev_core.o mdev_sysfs.o mdev_driver.o

obj-$(CONFIG_VFIO_MDEV) += mdev.o
obj-$(CONFIG_VFIO_MDEV_DEVICE) += vfio_mdev.o

# kvmgt depends on mdev, replace it with dummy driver
# i915 depends on kvmgt, so replace it with a dummy driver as well
ifeq ($(CONFIG_VFIO_MDEV),m)
obj-$(CONFIG_DRM_I915_GVT_KVMGT) += kvmgt.o
kvmgt-y := kvmgt_dummy.o

obj-$(CONFIG_DRM_I915) += i915.o
i915-y := i915_dummy.o
endif
