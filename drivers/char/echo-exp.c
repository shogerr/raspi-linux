#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/uaccess.h>
#include <linux/types.h>

#define FIRST_MINOR 0
#define MINOR_CNT 1

#define DRIVER_NAME "echo-exp"

static dev_t dev;
static struct cdev c_dev;
static struct class* cl;

static int my_open(struct inode* i, struct file* f) {
	printk("Echo Driver Open\r\n");
	return 0;
}

static int my_close(struct inode* i, struct file* f) {
	printk("Echo Driver Close\r\n");
	return 0;
}

static ssize_t echo_read(struct file* file, char __user *buf, size_t size, loff_t* ppos) {
	printk("Echo Driver Read\r\n")	;
	snprintf(buf, size, "I Am Echo\r\n");
	return strlen(buf);
}

static ssize_t echo_write(struct file* file, const char __user *buf, size_t size, loff_t* ppos) {
	char local[11];
	unsigned long buf_size;
	
	
	buf_size = sizeof(local);
	memset(local, 0, buf_size);

	printk("size: %ld, sizeof: %ld", size, buf_size - 1);

	if(copy_from_user(local, buf, min(size, buf_size - 1))) {
		return -EACCES;
	}
	
	printk("Echo Driver Write %ld bytes: %s\r\n", size, local);

	return min(size, buf_size - 1);
}

static struct file_operations fops = {
	.owner = THIS_MODULE,
	.open = my_open,
	.read = echo_read, 
	.write = echo_write,
	.release = my_close
};

static int __init echo_init(void) {
	int ret;
	struct device* dev_ret;

	// alloc device
	if((ret = alloc_chrdev_region(&dev, FIRST_MINOR, MINOR_CNT, DRIVER_NAME)) < 0) {
		return ret;
	}

	cdev_init(&c_dev, &fops);

	if((ret = cdev_add(&c_dev, dev, MINOR_CNT)) < 0) {
		return ret;
	}

	if(IS_ERR(cl = class_create(THIS_MODULE, "char"))) {
		cdev_del(&c_dev);
		unregister_chrdev_region(dev, MINOR_CNT);
		return PTR_ERR(cl);
	}

	if(IS_ERR(dev_ret = device_create(cl, NULL, dev, NULL, DRIVER_NAME))) {
		class_destroy(cl);
		cdev_del(&c_dev);
		unregister_chrdev_region(dev, MINOR_CNT);
		return PTR_ERR(dev_ret);
	}

	printk("Echo Driver Has Been Loaded\r\n");

	return 0;
}

static void __exit echo_exit(void) {
	device_destroy(cl, dev);
	class_destroy(cl);

	cdev_del(&c_dev);
	unregister_chrdev_region(dev, MINOR_CNT);
}

module_init(echo_init);
module_exit(echo_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Dakota Alton");
MODULE_DESCRIPTION("ECHO Demo Driver");
