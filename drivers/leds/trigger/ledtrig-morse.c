// Led Morse Trigger

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/sched.h>
#include <linux/sched/loadavg.h>
#include <linux/leds.h>
#include <linux/reboot.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/version.h>
#include <linux/device.h>

#include "../leds.h"

#define DRIVER_NAME "morse-input"
#define BUFFER_SIZE 32


static const char* CHAR_TO_MORSE[1] = {
	"test"
};

static int panic_morse;

struct morse_trig_data {
	
	unsigned int speed;

	const char* message;
	const char* message_location;
	const char* morse_location;

	unsigned int phase;

	struct timer_list timer;
};

static dev_t dev;
static struct cdev c_dev;
static struct class* cl;

static DEFINE_MUTEX(lock);

static message_buffer {
	char* data[BUFFER_SIZE];

	unsigned long size;
	unsigned long next_in;
	unsigned long next_out;
};

static struct message_buffer m_buf;

static void buffer_write(struct message_buffer* buf, char* c) {
	buf->data[buf->next_in] = c;
	buf->next_in = (buf->next_in + 1) % BUFFER_SIZE;
	buf->size++;
}

static char* buffer_read(struct message_buffer* buf) {
	char* result = buf->data[buf->next_out];
	buf->next_out = (buf->next_out + 1) % BUFFER_SIZE;
	buf->size--;
	return result;
}

static int my_open(struct inode* i, struct file* f) {
	printk("morse driver open\r\n");
	return 0;
}

static int my_close(struct inode* i, struct file* f) {
	printk("morse driver closer\r\n");
	return 0;
}

static ssize_t my_read(struct file* file, char __user *buf, size_t size, loff_t* ppos) {
	printk("morse driver read\r\n");
	snprintf(buf, size, "morse driver\r\n");
	return strlen(buf);
}

static ssize_t my_write(struct file& file, const char __user *buf, size_t size, loff_t* ppos) {
	char* local = kzalloc(size + 1, GFP_KERNEL);
	if(!local) {
		return
	}

}

static void led_morse_function(unsigned long data) {
	// todo implement	
}

static ssize_t led_speed_show(struct device* dev, struct device_attribute* attr, char* buf) {
	struct led_classdev* led_cdev = dev_get_drvdata(dev);
	struct morse_trig_data* morse_data = led_cdev->trigger_data;
	return sprintf(buf, "%u\n", morse_data->speed);
}

static ssize_t led_speed_store(struct device* dev, struct device_attribute* attr, const char* buf, size_t size) {
	struct led_classdev* led_cdev = dev_get_drvdata(dev);
	struct morse_trig_data* morse_data = led_cdev->trigger_data;
	unsigned long state;
	int ret;
	ret = kstrtoul(buf, 0, &state);
	if(ret)
		return ret;
	morse_data->speed = state;
	return size;
}

static DEVICE_ATTR(speed, 0644, led_speed_show, led_speed_store);

static void morse_trig_activate(struct led_classdev* led_cdev) {
	struct morse_trig_data* morse_data;
	int rc;
	
	morse_data = kzalloc(sizeof(*morse_data), GFP_KERNEL);
	if(!morse_data)
		return;

	led_cdev->trigger_data = morse_data;
	rc = device_create_file(led_cdev->dev, &dev_attr_speed);
	if(rc) {
		kfree(led_cdev->trigger_data);
		return;
	}

	setup_timer(&morse_data->timer, led_morse_function, (unsigned long)led_cdev);

	morse_data->phase = 0;
	morse_data->speed = 1;
	morse_data->message = 0;
	morse_data->message_location = 0;
	morse_data->morse_location = 0;

	led_morse_function(morse_data->timer.data);
	set_bit(LED_BLINK_SW, &led_cdev->work_flags);
	led_cdev->activated = true;

	// set up buffer 
}

static void morse_trig_deactivate(struct led_classdev* led_cdev) {
	struct morse_trig_data* morse_data = led_cdev->trigger_data;
	if(led_cdev->activated) {
		del_timer_sync(&morse_data->timer);
		device_remove_file(led_cdev->dev, &dev_attr_speed);
		kfree(morse_data);
		clear_bit(LED_BLINK_SW, &led_cdev->work_flags);
		led_cdev->activated = false;
	}
}

static struct led_trigger morse_led_trigger = {
	.name = "morse",
	.activate = morse_trig_activate,
	.deactivate = morse_trig_deactivate
};

static int morse_reboot_notifier(struct notifier_block* nb, unsigned long code, void* unused) {
	led_trigger_unregister(&morse_led_trigger);
	return NOTIFY_DONE;
}

static int morse_panic_notifier(struct notifier_block* nb, unsigned long code, void* unused) {
	panic_morse = 1;
	return NOTIFY_DONE;
}

static struct notifier_block morse_reboot_nb = {
	.notifier_call = morse_reboot_notifier
};

static struct notifier_block morse_panic_nb = {
	.notifier_call = morse_panic_notifier
};

static int __init morse_trig_init(void) {
	int rc = led_trigger_register(&morse_led_trigger);

	if(!rc) {
		atomic_notifier_chain_register(&panic_notifier_list, &morse_panic_nb);
		register_reboot_notifier(&morse_reboot_nb);
	}

	return rc;
}

static void __exit morse_trig_exit(void) {
	unregister_reboot_notifier(&morse_reboot_nb);
	atomic_notifier_chain_unregister(&panic_notifier_list, &morse_panic_nb);
	led_trigger_unregister(&morse_led_trigger);
}

module_init(morse_trig_init);
module_exit(morse_trig_exit);

MODULE_AUTHOR("Dakota Alton, Ross Shoger");
MODULE_DESCRIPTION("Morse LED Trigger");
MODULE_LICENSE("GPL");
