#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/uaccess.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/timer.h>
#include <linux/sched.h>
#include <linux/sched/loadavg.h>

#define FIRST_MINOR 0
#define MINOR_CNT 1

#define DRIVER_NAME "echo-exp"
#define BUFFER_SIZE 10

static dev_t dev;
static struct cdev c_dev;
static struct class* cl;

static struct timer_list timer;

static DEFINE_MUTEX(lock);

struct message_buffer {
	char data[BUFFER_SIZE];

	unsigned long size;
	unsigned long next_in;
	unsigned long next_out; 
};

static struct message_buffer m_buf;

static void buffer_write(struct message_buffer* buf, char c) {
	buf->data[buf->next_in] = c;
	buf->next_in = (buf->next_in + 1) % BUFFER_SIZE;
	buf->size++;
}

static char buffer_read(struct message_buffer* buf) {
	char result = buf->data[buf->next_out];
	buf->next_out = (buf->next_out + 1)  % BUFFER_SIZE;
	buf->size--;
	return result;
}

static void echo_function(unsigned long data) {
	char local;
	unsigned long delay = 70;

	// printk("here\r\n");
	if(m_buf.size) {
		if(mutex_lock_interruptible(&lock)) {
			// fail
		}

		if(m_buf.size)
			local = buffer_read(&m_buf);

		mutex_unlock(&lock);

		printk("Recv: %c\r\n", local);
	}

	delay = msecs_to_jiffies(delay);
	mod_timer(&timer, jiffies + msecs_to_jiffies(delay));
}

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
#if 0
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
#else
	char local = '*';

	if(m_buf.size == BUFFER_SIZE) 
		return 0;

	if(copy_from_user(&local, buf, min(size, (size_t)1))) {
		return -EACCES;
	}

	printk("Echo Driver Write %ld bytes: %c\r\n", size, local);
	
	if(mutex_lock_interruptible(&lock)) 
		return -ERESTARTSYS;

	if(local != '*')
		buffer_write(&m_buf, local);

	mutex_unlock(&lock);

	return min(size, (size_t)1);
#endif
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

	// set up timer
	setup_timer(&timer, echo_function, (unsigned long)NULL);
	mod_timer(&timer, jiffies + msecs_to_jiffies(10));

	// set up buffer
	m_buf.size = 0;
	m_buf.next_in = 0;
	m_buf.next_out = 0;
	memset(m_buf.data, 0, sizeof(m_buf.data));

	printk("Echo Driver Has Been Loaded\r\n");

	return 0;
}

static void __exit echo_exit(void) {

	del_timer_sync(&timer);

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
