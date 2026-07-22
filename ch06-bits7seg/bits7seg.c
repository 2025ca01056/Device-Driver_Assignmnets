// SPDX-License-Identifier: GPL-2.0
/*
 * ch06-bits7seg/bits7seg.c - Emulated 7-segment display multi-minor char driver.
 * Author: Dhruv Patel <2025ca01056@wilp.bits-pilani.ac.in>
 */

#define pr_fmt(fmt) "bits7seg: " fmt

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>

#define NUM_DEVICES 4
#define CLASS_NAME "bits7seg"

/* Register Offsets */
#define REG_DIGIT  0
#define REG_CTRL   1
#define REG_STATUS 2

/* Bit Definitions */
#define CTRL_EN    BIT(0)
#define CTRL_BLANK BIT(1)
#define STATUS_RDY BIT(0)

struct seg_regs {
	u8 reg_digit;
	u8 reg_ctrl;
	u8 reg_status;
};

struct seg_dev {
	int minor;
	struct seg_regs *regs;
	struct mutex lock;
	struct cdev cdev;
	struct device *dev;
};

static dev_t dev_num;
static struct class *bits7seg_class;
static struct seg_dev g_devices[NUM_DEVICES];

/* Sysfs Show Function */
static ssize_t digit_show(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	struct seg_dev *sdev = dev_get_drvdata(dev);
	u8 val;

	mutex_lock(&sdev->lock);
	val = sdev->regs->reg_digit;
	mutex_unlock(&sdev->lock);

	return sysfs_emit(buf, "%u\n", val);
}

/* Sysfs Store Function */
static ssize_t digit_store(struct device *dev,
			   struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct seg_dev *sdev = dev_get_drvdata(dev);
	u8 val;
	int ret;

	ret = kstrtou8(buf, 10, &val);
	if (ret < 0 || val > 9)
		return -EINVAL;

	mutex_lock(&sdev->lock);
	sdev->regs->reg_digit = val;
	/* Read-Modify-Write to update control register EN bit */
	sdev->regs->reg_ctrl |= CTRL_EN;
	mutex_unlock(&sdev->lock);

	return count;
}

static DEVICE_ATTR_RW(digit);

static int bits7seg_open(struct inode *inode, struct file *file)
{
	struct seg_dev *sdev;

	sdev = container_of(inode->i_cdev, struct seg_dev, cdev);
	file->private_data = sdev;

	return 0;
}

static int bits7seg_release(struct inode *inode, struct file *file)
{
	return 0;
}

static ssize_t bits7seg_read(struct file *file, char __user *buf,
			      size_t count, loff_t *ppos)
{
	struct seg_dev *sdev = file->private_data;
	char kbuf[64];
	int len;
	u8 d, c, s;

	if (*ppos > 0)
		return 0;

	mutex_lock(&sdev->lock);
	d = sdev->regs->reg_digit;
	c = sdev->regs->reg_ctrl;
	s = sdev->regs->reg_status;
	mutex_unlock(&sdev->lock);

	len = snprintf(kbuf, sizeof(kbuf),
		       "digit=%u en=%u blank=%u ready=%u\n",
		       d, (c & CTRL_EN) ? 1 : 0,
		       (c & CTRL_BLANK) ? 1 : 0,
		       (s & STATUS_RDY) ? 1 : 0);

	if (copy_to_user(buf, kbuf, len))
		return -EFAULT;

	*ppos += len;
	return len;
}

static ssize_t bits7seg_write(struct file *file, const char __user *buf,
			       size_t count, loff_t *ppos)
{
	struct seg_dev *sdev = file->private_data;
	char kbuf[2];

	if (count == 0)
		return 0;

	if (copy_from_user(kbuf, buf, 1))
		return -EFAULT;

	if (kbuf[0] < '0' || kbuf[0] > '9')
		return -EINVAL;

	mutex_lock(&sdev->lock);
	sdev->regs->reg_digit = kbuf[0] - '0';
	/* Read-Modify-Write pattern */
	sdev->regs->reg_ctrl &= ~CTRL_BLANK;
	sdev->regs->reg_ctrl |= CTRL_EN;
	mutex_unlock(&sdev->lock);

	return count;
}

static const struct file_operations bits7seg_fops = {
	.owner   = THIS_MODULE,
	.open    = bits7seg_open,
	.release = bits7seg_release,
	.read    = bits7seg_read,
	.write   = bits7seg_write,
};

static int __init bits7seg_init(void)
{
	int ret, i;

	ret = alloc_chrdev_region(&dev_num, 0, NUM_DEVICES, "bits7seg");
	if (ret < 0) {
		pr_err("Failed to allocate chrdev region\n");
		return ret;
	}

	bits7seg_class = class_create(CLASS_NAME);
	if (IS_ERR(bits7seg_class)) {
		ret = PTR_ERR(bits7seg_class);
		goto unregister_chrdev;
	}

	for (i = 0; i < NUM_DEVICES; i++) {
		g_devices[i].minor = i;
		mutex_init(&g_devices[i].lock);

		g_devices[i].regs = kzalloc(sizeof(struct seg_regs), GFP_KERNEL);
		if (!g_devices[i].regs) {
			ret = -ENOMEM;
			goto cleanup_devices;
		}

		/* Initialize status register READY bit */
		g_devices[i].regs->reg_status |= STATUS_RDY;

		cdev_init(&g_devices[i].cdev, &bits7seg_fops);
		g_devices[i].cdev.owner = THIS_MODULE;
		ret = cdev_add(&g_devices[i].cdev, MKDEV(MAJOR(dev_num), i), 1);
		if (ret < 0)
			goto cleanup_devices;

		g_devices[i].dev = device_create(bits7seg_class, NULL,
						  MKDEV(MAJOR(dev_num), i),
						  &g_devices[i],
						  "bits7seg%d", i);
		if (IS_ERR(g_devices[i].dev)) {
			ret = PTR_ERR(g_devices[i].dev);
			cdev_del(&g_devices[i].cdev);
			goto cleanup_devices;
		}

		ret = device_create_file(g_devices[i].dev, &dev_attr_digit);
		if (ret < 0) {
			device_destroy(bits7seg_class, MKDEV(MAJOR(dev_num), i));
			cdev_del(&g_devices[i].cdev);
			goto cleanup_devices;
		}
	}

	pr_info("Driver loaded with major %d\n", MAJOR(dev_num));
	return 0;

cleanup_devices:
	while (--i >= 0) {
		device_remove_file(g_devices[i].dev, &dev_attr_digit);
		device_destroy(bits7seg_class, MKDEV(MAJOR(dev_num), i));
		cdev_del(&g_devices[i].cdev);
		kfree(g_devices[i].regs);
		mutex_destroy(&g_devices[i].lock);
	}
	class_destroy(bits7seg_class);
unregister_chrdev:
	unregister_chrdev_region(dev_num, NUM_DEVICES);
	return ret;
}

static void __exit bits7seg_exit(void)
{
	int i;

	for (i = 0; i < NUM_DEVICES; i++) {
		device_remove_file(g_devices[i].dev, &dev_attr_digit);
		device_destroy(bits7seg_class, MKDEV(MAJOR(dev_num), i));
		cdev_del(&g_devices[i].cdev);
		kfree(g_devices[i].regs);
		mutex_destroy(&g_devices[i].lock);
	}
	class_destroy(bits7seg_class);
	unregister_chrdev_region(dev_num, NUM_DEVICES);

	pr_info("Driver unloaded cleanly\n");
}

module_init(bits7seg_init);
module_exit(bits7seg_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Dhruv Patel <2025ca01056@wilp.bits-pilani.ac.in>");
MODULE_DESCRIPTION("Multi-minor 7-Segment emulated character driver");
