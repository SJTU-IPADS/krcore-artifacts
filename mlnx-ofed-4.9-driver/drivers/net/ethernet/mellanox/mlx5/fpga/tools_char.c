/*
 * Copyright (c) 2017 Mellanox Technologies. All rights reserved.
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
 *
 */

#include <linux/module.h>
#include <linux/stat.h>
#include <linux/fs.h>
#include <uapi/linux/mlx5/fpga_tools.h>
#include "tools_char.h"

#define CHUNK_SIZE (128 * 1024)

static struct class *char_class;
static int major_number;
struct ida minor_alloc;

struct file_context {
	struct mlx5_fpga_tools_dev *tdev;
	enum mlx5_fpga_access_type access_type;
};

static int tools_char_open(struct inode *inodep, struct file *filep)
{
	struct mlx5_fpga_tools_dev *tdev;
	struct file_context *context;

	tdev = container_of(inodep->i_cdev, struct mlx5_fpga_tools_dev, cdev);
	context = kzalloc(sizeof(*context), GFP_KERNEL);
	context->tdev = tdev;
	context->access_type = MLX5_FPGA_ACCESS_TYPE_DONTCARE;
	filep->private_data = context;
	atomic_inc(&tdev->open_count);
	dev_dbg(mlx5_fpga_dev(tdev->fdev),
		"tools char device opened %d times\n",
		atomic_read(&tdev->open_count));
	return 0;
}

static int tools_char_release(struct inode *inodep, struct file *filep)
{
	struct file_context *context = filep->private_data;

	WARN_ON(atomic_read(&context->tdev->open_count) < 1);
	atomic_dec(&context->tdev->open_count);
	dev_dbg(mlx5_fpga_dev(context->tdev->fdev),
		"tools char device closed. Still open %d times\n",
		atomic_read(&context->tdev->open_count));
	kfree(context);
	return 0;
}

static int mem_read(struct mlx5_fpga_tools_dev *tdev, void *buf,
		    size_t count, u64 address,
		    enum mlx5_fpga_access_type access_type)
{
	int ret;

	ret = mutex_lock_interruptible(&tdev->mutex);
	if (ret)
		goto out;

	ret = mlx5_fpga_mem_read(tdev->fdev, count, address, buf, access_type);
	if (ret < 0) {
		dev_dbg(mlx5_fpga_dev(tdev->fdev),
			"Failed to read %zu bytes at address 0x%llx: %d\n",
			count, address, ret);
		goto unlock;
	}

unlock:
	mutex_unlock(&tdev->mutex);

out:
	return ret;
}

static int mem_write(struct mlx5_fpga_tools_dev *tdev, void *buf, size_t count,
		     u64 address, enum mlx5_fpga_access_type access_type)
{
	int ret;

	ret = mutex_lock_interruptible(&tdev->mutex);
	if (ret)
		goto out;

	ret = mlx5_fpga_mem_write(tdev->fdev, count, address, buf, access_type);
	if (ret < 0) {
		dev_dbg(mlx5_fpga_dev(tdev->fdev),
			"Failed to write %zu bytes at address 0x%llx: %d\n",
			count, address, ret);
		goto unlock;
	}

unlock:
	mutex_unlock(&tdev->mutex);

out:
	return ret;
}

static ssize_t tools_char_read(struct file *filep, char __user *buffer,
			       size_t len, loff_t *offset)
{
	int ret = 0;
	void *kbuf = NULL;
	struct file_context *context = filep->private_data;

	dev_dbg(mlx5_fpga_dev(context->tdev->fdev),
		"tools char device reading %zu bytes at 0x%llx\n",
		len, *offset);

	if (len < 1)
		return len;
	if (len > CHUNK_SIZE)
		len = CHUNK_SIZE;

	kbuf = kmalloc(len, GFP_KERNEL);
	if (!kbuf) {
		ret = -ENOMEM;
		goto out;
	}
	ret = mem_read(context->tdev, kbuf, len, *offset, context->access_type);
	if (ret <= 0)
		goto out;
	*offset += ret;
	if (copy_to_user(buffer, kbuf, len)) {
		dev_err(mlx5_fpga_dev(context->tdev->fdev),
			"Failed to copy data to user buffer\n");
		ret = -EFAULT;
		goto out;
	}
out:
	kfree(kbuf);
	return ret;
}

static ssize_t tools_char_write(struct file *filep, const char __user *buffer,
				size_t len, loff_t *offset)
{
	int ret = 0;
	void *kbuf = NULL;
	struct file_context *context = filep->private_data;

	dev_dbg(mlx5_fpga_dev(context->tdev->fdev),
		"tools char device writing %zu bytes at 0x%llx\n",
		len, *offset);

	if (len < 1)
		return len;
	if (len > CHUNK_SIZE)
		len = CHUNK_SIZE;

	kbuf = kmalloc(len, GFP_KERNEL);
	if (!kbuf) {
		ret = -ENOMEM;
		goto out;
	}
	if (copy_from_user(kbuf, buffer, len)) {
		dev_err(mlx5_fpga_dev(context->tdev->fdev),
			"Failed to copy data from user buffer\n");
		ret = -EFAULT;
		goto out;
	}

	ret = mem_write(context->tdev, kbuf, len, *offset,
			context->access_type);
	if (ret <= 0)
		goto out;
	*offset += ret;
out:
	kfree(kbuf);
	return ret;
}

static loff_t tools_char_llseek(struct file *filep, loff_t offset, int whence)
{
	loff_t new_offset;
	struct file_context *context = filep->private_data;
	u64 max = mlx5_fpga_ddr_base_get(context->tdev->fdev) +
		  mlx5_fpga_ddr_size_get(context->tdev->fdev);
	new_offset = fixed_size_llseek(filep, offset, whence, max);
	if (new_offset >= 0)
		dev_dbg(mlx5_fpga_dev(context->tdev->fdev),
			"tools char device seeked to 0x%llx\n", new_offset);
	return new_offset;
}

long tools_char_ioctl(struct file *filep, unsigned int cmd, unsigned long arg)
{
	int err = 0;
	struct file_context *context = filep->private_data;
	struct mlx5_fpga_query query;
	struct mlx5_fpga_temperature temperature;
	enum mlx5_fpga_connect connect;
	struct mlx5_fpga_device *fdev = context->tdev->fdev;
	u32 fpga_cap[MLX5_ST_SZ_DW(fpga_cap)] = {0};

	if (!fdev)
		return -ENXIO;

	switch (cmd) {
	case IOCTL_ACCESS_TYPE:
		if (arg > MLX5_FPGA_ACCESS_TYPE_MAX) {
			dev_err(mlx5_fpga_dev(fdev),
				"unknown access type %lu\n", arg);
			err = -EINVAL;
			break;
		}
		context->access_type = arg;
		break;
	case IOCTL_FPGA_LOAD:
		if (arg > MLX5_FPGA_IMAGE_MAX) {
			dev_err(mlx5_fpga_dev(fdev),
				"unknown image type %lu\n", arg);
			err = -EINVAL;
			break;
		}
		err = mlx5_fpga_device_reload(fdev, arg);
		break;
	case IOCTL_FPGA_RESET:
		err = mlx5_fpga_device_reload(fdev, MLX5_FPGA_IMAGE_MAX + 1);
		break;
	case IOCTL_FPGA_IMAGE_SEL:
		if (arg > MLX5_FPGA_IMAGE_MAX) {
			dev_err(mlx5_fpga_dev(fdev),
				"unknown image type %lu\n", arg);
			err = -EINVAL;
			break;
		}
		err = mlx5_fpga_flash_select(fdev, arg);
		break;
	case IOCTL_FPGA_QUERY:
		mlx5_fpga_device_query(fdev, &query);

		if (copy_to_user((void __user *)arg, &query, sizeof(query))) {
			dev_err(mlx5_fpga_dev(fdev),
				"Failed to copy data to user buffer\n");
			err = -EFAULT;
		}
		break;
	case IOCTL_FPGA_CAP:
		mlx5_fpga_get_cap(fdev, fpga_cap);
		if (copy_to_user((void __user *)arg, fpga_cap, sizeof(fpga_cap))) {
			dev_err(mlx5_fpga_dev(fdev),
				"Failed to copy data to user buffer\n");
			err = -EFAULT;
		}
		break;
	case IOCTL_FPGA_TEMPERATURE:
		err = copy_from_user(&temperature, (void *)arg,
				     sizeof(temperature));
		if (err) {
			dev_err(mlx5_fpga_dev(fdev),
				"Failed to copy data from user buffer\n");
			err = -EFAULT;
		}
		mlx5_fpga_temperature(fdev, &temperature);
		err = copy_to_user((void __user *)arg, &temperature,
				   sizeof(temperature));
		if (err) {
			dev_err(mlx5_fpga_dev(fdev),
				"Failed to copy data to user buffer\n");
			err = -EFAULT;
		}
		break;
	case IOCTL_FPGA_CONNECT:
		err = copy_from_user(&connect, (void *)arg,
				     sizeof(connect));
		if (err) {
			dev_err(mlx5_fpga_dev(fdev),
				"Failed to copy data from user buffer\n");
			err = -EFAULT;
		}
		mlx5_fpga_connectdisconnect(fdev, &connect);
		err = copy_to_user((void __user *)arg, &connect,
				   sizeof(connect));
		if (err) {
			dev_err(mlx5_fpga_dev(fdev),
				"Failed to copy data to user buffer\n");
			err = -EFAULT;
		}
		break;
	default:
		dev_err(mlx5_fpga_dev(fdev),
			"unknown ioctl command 0x%08x\n", cmd);
		err = -ENOIOCTLCMD;
	}
	return err;
}

static const struct file_operations tools_fops = {
		.owner = THIS_MODULE,
		.open = tools_char_open,
		.release = tools_char_release,
		.read = tools_char_read,
		.write = tools_char_write,
		.llseek = tools_char_llseek,
		.unlocked_ioctl = tools_char_ioctl,
};

int mlx5_fpga_tools_char_add_one(struct mlx5_fpga_tools_dev *tdev)
{
	int ret = 0;

	ret = ida_simple_get(&minor_alloc, 1, 0, GFP_KERNEL);
	if (ret < 0) {
		dev_err(mlx5_fpga_dev(tdev->fdev),
			"Failed to allocate minor number: %d", ret);
		goto out;
	}
	tdev->dev = MKDEV(major_number, ret);

	atomic_set(&tdev->open_count, 0);
	cdev_init(&tdev->cdev, &tools_fops);
	ret = cdev_add(&tdev->cdev, tdev->dev, 1);
	if (ret) {
		dev_err(mlx5_fpga_dev(tdev->fdev),
			"Failed to add cdev: %d\n", ret);
		goto err_minor;
	}

	tdev->char_device = device_create(char_class, NULL, tdev->dev, NULL,
					  "%s%s",
					  dev_name(mlx5_fpga_dev(tdev->fdev)),
					  MLX5_FPGA_TOOLS_NAME_SUFFIX);
	if (IS_ERR(tdev->char_device)) {
		ret = PTR_ERR(tdev->char_device);
		tdev->char_device = NULL;
		dev_err(mlx5_fpga_dev(tdev->fdev),
			"Failed to create a char device: %d\n", ret);
		goto err_cdev;
	}

	dev_dbg(mlx5_fpga_dev(tdev->fdev),
		"tools char device %u:%u created\n", MAJOR(tdev->dev),
		MINOR(tdev->dev));
	goto out;

err_cdev:
	cdev_del(&tdev->cdev);
err_minor:
	ida_simple_remove(&minor_alloc, MINOR(tdev->dev));
out:
	return ret;
}

void mlx5_fpga_tools_char_remove_one(struct mlx5_fpga_tools_dev *tdev)
{
	WARN_ON(atomic_read(&tdev->open_count) > 0);
	device_destroy(char_class, tdev->dev);
	cdev_del(&tdev->cdev);
	ida_simple_remove(&minor_alloc, MINOR(tdev->dev));
	dev_err(mlx5_fpga_dev(tdev->fdev),
		"tools char device %u:%u destroyed\n", MAJOR(tdev->dev),
		MINOR(tdev->dev));
}

int mlx5_fpga_tools_char_init(void)
{
	int ret = 0;

	major_number = register_chrdev(0, MLX5_FPGA_TOOLS_DRIVER_NAME,
				       &tools_fops);
	if (major_number < 0) {
		ret = major_number;
		pr_err("Failed to register major number for char device: %d\n",
		       ret);
		goto out;
	}
	pr_debug("mlx5_fpga_tools major number is %d\n", major_number);

	char_class = class_create(THIS_MODULE, MLX5_FPGA_TOOLS_DRIVER_NAME);
	if (IS_ERR(char_class)) {
		ret = PTR_ERR(char_class);
		pr_err("Failed to create char class: %d\n", ret);
		goto err_chrdev;
	}

	ida_init(&minor_alloc);

	goto out;

err_chrdev:
	unregister_chrdev(major_number, MLX5_FPGA_TOOLS_DRIVER_NAME);

out:
	return ret;
}

void mlx5_fpga_tools_char_deinit(void)
{
	ida_destroy(&minor_alloc);
	class_destroy(char_class);
	unregister_chrdev(major_number, MLX5_FPGA_TOOLS_DRIVER_NAME);
	pr_debug("mlx5_fpga_tools major number freed\n");
}
