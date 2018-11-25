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
#include <linux/cdev.h>

#include "../leds.h"

#define DRIVER_NAME "morse-input"
#define BUFFER_SIZE 32
#define FIRST_MINOR 0
#define MINOR_CNT 1

static const char* CHAR_TO_MORSE[128] = {
	"*", "*", "*", "*", "*", "*", "*", "*",
	"*", "*", "*", "*", "*", "*", "*", "*",
	"*", "*", "*", "*", "*", "*", "*", "*",
	"*", "*", "*", "*", "*", "*", "*", "*",
	"*", "-.-.--", ".-..-.", "*", "*", "*", "*", ".----.",
	"-.--.", "-.--.-", "*", "*", "--..--", "-....-", ".-.-.-", "-..-.",
	"-----", ".----", "..---", "...--", "....-", ".....", "-....", "--...",
	"---..", "----.", "---...", "*", "*", "-...-", "*", "..--..",
	".--.-.", ".-", "-...", "-.-.", "-..", ".", "..-.", "--.",
	"....", "..", ".---", "-.-", ".-..", "--", "-.", "---",
	".--.", "--.-", ".-.", "...", "-", "..-", "...-", ".--",
	"-..-", "-.--", "--..", "*", "*", "*", "*", "..--.-",
	"*", ".-", "-...", "-.-.", "-..", ".", "..-.", "--.",
	"....", "..", ".---", "-.-", ".-..", "--", "-.", "---",
	".--.", "--.-", ".-.", "...", "-", "..-", "...-", ".--",
	"-..-", "-.--", "--..", "*", "*", "*", "*", "*"
};


static int panic_morse;

struct morse_trig_data {

	unsigned int speed;
	unsigned int repeat;

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

struct message_buffer {
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

static ssize_t my_write(struct file* file, const char __user *buf, size_t size, loff_t* ppos) {
	int i;
	char* local = kzalloc(size + 5, GFP_KERNEL);
	if(!local) 
		return 0;

	if(m_buf.size == BUFFER_SIZE) {
		kfree(local);
		return 0;
	}


	if(mutex_lock_interruptible(&lock)) {
		kfree(local);
		return -ERESTARTSYS;
	}

	if(copy_from_user(local, buf, size)) {
		return -EACCES;
	}
	
	printk("Echo Driver Write %ld bytes: %s\r\n", size, local);
	local[size] = '\0';

	for(i = 0; i < strlen(local); i++) {
		if(local[i] >= 128 || local[i] < 0) 
			local[i] = 0;
	}

	buffer_write(&m_buf, local);

	mutex_unlock(&lock);

	return size;
}

static struct file_operations fops = {
	.owner = THIS_MODULE,
	.open = my_open,
	.read = my_read,
	.write = my_write,
	.release = my_close
};

static void led_morse_function(unsigned long data) {
	struct led_classdev* led_cdev = (struct led_classdev*)data;
	struct morse_trig_data* morse_data = led_cdev->trigger_data;

	unsigned long brightness = LED_ON;
	unsigned long delay = 0;
	unsigned long dit = morse_data->speed * 100;

	// check if to see if the current message needs to be updated
	if(morse_data->message == 0) {
		printk("message == 0\r\n");
		if(m_buf.size) {
			if(mutex_lock_interruptible(&lock))	{
				delay = dit;
				brightness = LED_OFF;
				morse_data->phase = 0;
				goto update;
			}	

			morse_data->message = buffer_read(&m_buf);

			printk("%lu items in buffer\r\n", m_buf.size);

			mutex_unlock(&lock);

			morse_data->message_location = morse_data->message;
			morse_data->morse_location = CHAR_TO_MORSE[(int)(*morse_data->message_location)];
		} else {
			delay = dit;
			brightness = LED_OFF;
			morse_data->phase = 0;
			goto update;
		}	
	}

	if(morse_data->phase == 1) {
		delay = dit;
		brightness = LED_OFF;
		morse_data->phase = 0;
		goto update;
	}

	if(morse_data->morse_location == NULL) {
		printk("early escape\r\n");
		delay = dit;
		brightness = LED_OFF;
		morse_data->phase = 0;
		goto update;
	}

	printk("before switch\r\n");
	switch(*morse_data->morse_location) {
		case '*':
			printk("Invalid Characher\r\n");
			delay = dit;
			brightness = LED_OFF;
			morse_data->phase = 0;
			break;
		case '.':
			delay = dit;
			morse_data->phase = 1;
			morse_data->morse_location++;
			break;
		case '-':
			delay = 3 * dit;
			morse_data->phase = 1;
			morse_data->morse_location++;
			break;
		default:
			printk("In Deafault Of Outer Switch. next char: %d\r\n", (int)(*(morse_data->message_location+1)));
			brightness = LED_OFF;
			switch(*(morse_data->message_location+1)) {
				case ' ':
					printk("In Space Test For Inner Switch\r\n");
					delay = 7 * dit;
					morse_data->message_location += 2;
					break;
				case '\n':
				case '\0':
					printk("try repeat\r\n");
					delay = 20 * dit;
					if(morse_data->repeat) { 
						// check if there is another message pending
						if(m_buf.size) {
							// `signal` to update the message
							kfree(morse_data->message);
							morse_data->message = 0;
						} else {
							morse_data->message_location = morse_data->message;
						}
					} else {
						kfree(morse_data->message);
						morse_data->message = 0;
					}
					printk("end repeat\r\n");
					break;
				default:
					printk("In Deafault of Inner Switch\r\n");
					delay = 3 * dit;
					morse_data->message_location++;
			};
			printk("set morse_location\r\n");
			morse_data->morse_location = CHAR_TO_MORSE[(int)(*morse_data->message_location)];
	};

	printk("after switch\r\n");
update:
	led_set_brightness_nosleep(led_cdev, brightness);
	mod_timer(&morse_data->timer, jiffies + msecs_to_jiffies(delay));
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

static ssize_t led_repeat_show(struct device* dev, struct device_attribute* attr, char* buf) {
	struct led_classdev* led_cdev = dev_get_drvdata(dev);
	struct morse_trig_data* morse_data = led_cdev->trigger_data;
	return sprintf(buf, "%u\n", morse_data->repeat);
}

static ssize_t led_repeat_store(struct device* dev, struct device_attribute* attr, const char* buf, size_t size) {
	struct led_classdev* led_cdev = dev_get_drvdata(dev);
	struct morse_trig_data* morse_data = led_cdev->trigger_data;
	unsigned long state;
	int ret;
	ret = kstrtoul(buf, 0, &state);
	if(ret)
		return ret;
	morse_data->repeat = state;
	return size;
}

static DEVICE_ATTR(repeat, 0644, led_repeat_show, led_repeat_store);

static void morse_trig_activate(struct led_classdev* led_cdev) {
	struct morse_trig_data* morse_data;
	int rc;
	int ret;
	struct device* dev_ret;

	morse_data = kzalloc(sizeof(*morse_data), GFP_KERNEL);
	if(!morse_data)
		return;

	led_cdev->trigger_data = morse_data;
	rc = device_create_file(led_cdev->dev, &dev_attr_speed);
	if(rc) {
		kfree(led_cdev->trigger_data);
		return;
	}

	rc = device_create_file(led_cdev->dev, &dev_attr_repeat);
	if(rc) {
		kfree(led_cdev->trigger_data);
		return;
	}

	setup_timer(&morse_data->timer, led_morse_function, (unsigned long)led_cdev);

	morse_data->phase = 0;
	morse_data->speed = 1;
	morse_data->message = 0;
	morse_data->repeat = 1;
	morse_data->message_location = 0;
	morse_data->morse_location = 0;

	led_morse_function(morse_data->timer.data);
	set_bit(LED_BLINK_SW, &led_cdev->work_flags);
	led_cdev->activated = true;

	// set up buffer
	m_buf.size = 0;
	m_buf.next_in = 0;
	m_buf.next_out = 0;
	memset(m_buf.data, 0, sizeof(m_buf.data));

	// set up char driver

	if((ret = alloc_chrdev_region(&dev, FIRST_MINOR, MINOR_CNT, DRIVER_NAME)) < 0) {
		kfree(led_cdev->trigger_data);
		return;
	}

	cdev_init(&c_dev, &fops);

	if((ret = cdev_add(&c_dev, dev, MINOR_CNT)) < 0) {
		kfree(led_cdev->trigger_data);
		return;
	}
	
	if(IS_ERR(cl = class_create(THIS_MODULE, "char")) < 0) {
		kfree(led_cdev->trigger_data);
		cdev_del(&c_dev);
		unregister_chrdev_region(dev, MINOR_CNT);
		return;
	}

	if(IS_ERR(dev_ret = device_create(cl, led_cdev->dev, dev, NULL, DRIVER_NAME))) {
		class_destroy(cl);
		cdev_del(&c_dev);
		unregister_chrdev_region(dev, MINOR_CNT);
		kfree(led_cdev->trigger_data);
		return;
	}


}

static void morse_trig_deactivate(struct led_classdev* led_cdev) {
	struct morse_trig_data* morse_data = led_cdev->trigger_data;
	if(led_cdev->activated) {
		del_timer_sync(&morse_data->timer);
		device_remove_file(led_cdev->dev, &dev_attr_speed);
		device_remove_file(led_cdev->dev, &dev_attr_repeat);
		kfree(morse_data);
		clear_bit(LED_BLINK_SW, &led_cdev->work_flags);
		led_cdev->activated = false;

		device_destroy(cl, dev);
		class_destroy(cl);

		cdev_del(&c_dev);
		unregister_chrdev_region(dev, MINOR_CNT);
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
