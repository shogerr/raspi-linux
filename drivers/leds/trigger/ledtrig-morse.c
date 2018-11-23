/**
 *
 * LED Morse Trigger
 *
 *
 *
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/sched.h>
#include <linux/sched/loadavg.h>
#include <linux/leds.h>
#include <linux/reboot.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/kdev_t.h>
#include <linux/version.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/uaccess.h>
#include <linux/types.h>
#include <linux/ioctl.h>
#include <linux/mutex.h>
#include <linux/kfifo.h>

#include "../leds.h"

#define FIRST_MINOR 0
#define MINOR_CNT 1

static void led_morse_function(unsigned long data);

static ssize_t led_speed_show(struct device* dev, struct device_attribute* attr, char* buf); 
static ssize_t led_speed_store(struct device* dev, struct device_attribute* attr, const char* buf, size_t size); 

static ssize_t led_do_repeat_show(struct device* dev, struct device_attribute* attr, char* buf);
static ssize_t led_do_repeat_store(struct device* dev, struct device_attribute* attr, const char* buf, size_t size);

static void morse_trig_activate(struct led_classdev* led_cdev);
static void morse_trig_deactivate(struct led_classdev* led_cdev); 

static int morse_reboot_notifier(struct notifier_block* nb, unsigned long code, void* unused);
static int morse_panic_notifier(struct notifier_block* nb, unsigned long code, void* unused);

static int __init morse_trig_init(void);
static void __exit morse_trig_exit(void);

static int on_open(struct inode* i, struct file* f);
static int on_close(struct inode* i, struct file* f);
static ssize_t on_read(struct file* f, char __user *buf, size_t size, loff_t* ppos);
static ssize_t on_write(struct file* f, const char __user *buf, size_t size, loff_t* ppos);


static const char* CHAR_TO_MORSE[128] = {
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL, "-.-.--", ".-..-.", NULL, NULL, NULL, NULL, ".----.",
	"-.--.", "-.--.-", NULL, NULL, "--..--", "-....-", ".-.-.-", "-..-.",
	"-----", ".----", "..---", "...--", "....-", ".....", "-....", "--...",
	"---..", "----.", "---...", NULL, NULL, "-...-", NULL, "..--..",
	".--.-.", ".-", "-...", "-.-.", "-..", ".", "..-.", "--.",
	"....", "..", ".---", "-.-", ".-..", "--", "-.", "---",
	".--.", "--.-", ".-.", "...", "-", "..-", "...-", ".--",
	"-..-", "-.--", "--..", NULL, NULL, NULL, NULL, "..--.-",
	NULL, ".-", "-...", "-.-.", "-..", ".", "..-.", "--.",
	"....", "..", ".---", "-.-", ".-..", "--", "-.", "---",
	".--.", "--.-", ".-.", "...", "-", "..-", "...-", ".--",
	"-..-", "-.--", "--..", NULL, NULL, NULL, NULL, NULL
};

static int panic_morse;


static DEFINE_MUTEX(write_lock);
#define BUFFER_SIZE 16

struct write_buffer {
	char* data[BUFFER_SIZE];
	int next_in, next_out;
	int size;
};

static void buffer_put(struct write_buffer* wb, char* c);
static char* buffer_get(struct write_buffer* wb);


struct morse_trig_data {
	// how fast should the morse be rendered
	unsigned int speed;

	// current message
	char* message;

	// where in the message are we
	const char* message_location;

	// what part of the morse code are we at
	const char* morse_location;

	// should the message be repeated
	unsigned int do_repeat;

	unsigned int phase;
	struct timer_list timer;
};

static DEVICE_ATTR(speed, 0644, led_speed_show, led_speed_store);
static DEVICE_ATTR(do_repeat, 0644, led_do_repeat_show, led_do_repeat_store);

static struct notifier_block morse_reboot_nb = {
	.notifier_call = morse_reboot_notifier
};

static struct notifier_block morse_panic_nb = {
	.notifier_call = morse_panic_notifier	
};

static struct led_trigger morse_led_trigger = {
	.name = "morse",
	.activate = morse_trig_activate,
	.deactivate = morse_trig_deactivate
};

static dev_t dev;
static struct cdev c_dev;
static struct class* cl;


static struct write_buffer message_buffer;

static struct file_operations fops = {
	.owner = THIS_MODULE,
	.open = on_open,
	.read = on_read,
	.write = on_write,
	.release = on_close
};



/** this function determines the state of the led and how long it will 
 *  be on for
 * 
 * Standard Morse code is defined to use dahs that are 3 times longer than the dits. 
 * The Space between dits and dahs is equal to the length of a dit
 * The Space between characters is equal to the length of a dah (3 dits)
 * The Space between words is twice the space between characters
 *
 */ 
static void led_morse_function(unsigned long data) {

	struct led_classdev* led_cdev = (struct led_classdev*) data;
	struct morse_trig_data* morse_data = led_cdev->trigger_data;

	unsigned long brightness = LED_ON;
	unsigned long delay = 0;
	unsigned long dit = morse_data->speed * 100;


	if(morse_data->message == 0) {
		if(message_buffer.size) {
			if(mutex_lock_interruptible(&write_lock)) {
				// fail
			
				delay = dit;
				brightness = LED_OFF;
				goto update;

			} else {
				morse_data->message = buffer_get(&message_buffer);

				// validate current string, should be done elsewhere

				morse_data->message_location = morse_data->message;
				morse_data->morse_location = CHAR_TO_MORSE[(int)(*morse_data->message_location)];
			}


			mutex_unlock(&write_lock);
		} else {
			// nothing todo
			delay = dit;
			brightness = LED_OFF;
			goto update;
		}
	} 


	if (morse_data->phase == 1)
	{
		delay = dit;
		brightness = LED_OFF;
		morse_data->phase = 0;
		goto update;
	}

	switch(*morse_data->morse_location) {
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
			brightness = LED_OFF;
			switch (*(morse_data->message_location+1))
			{
				case ' ':
					delay = 7 * dit;
					morse_data->message_location += 2;
					break;
				case '\0':
					delay = 20 * dit; 
					if(morse_data->do_repeat) {
						morse_data->message_location = morse_data->message;
					} else {
						kfree(morse_data->message);
						morse_data->message = 0;
					}
					break;
				default:
					delay = 3 * dit;
					morse_data->message_location++;
			};

			morse_data->morse_location = CHAR_TO_MORSE[(int)(*morse_data->message_location)];
	};

update:
	led_set_brightness_nosleep(led_cdev, brightness);
	mod_timer(&morse_data->timer, jiffies + msecs_to_jiffies(delay));
}

static ssize_t led_speed_show(struct device* dev, struct device_attribute* attr, char* buf) {
	struct led_classdev* led_cdev = dev_get_drvdata(dev);
	struct morse_trig_data* morse_data = led_cdev->trigger_data;

	return sprintf(buf,"%u\n", morse_data->speed);
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

static ssize_t led_do_repeat_show(struct device* dev, struct device_attribute* attr, char* buf) {
	struct led_classdev* led_cdev = dev_get_drvdata(dev);
	struct morse_trig_data* morse_data = led_cdev->trigger_data;

	return sprintf(buf, "%u\n", morse_data->do_repeat);
}

static ssize_t led_do_repeat_store(struct device* dev, struct device_attribute* attr, const char* buf, size_t size) {
	struct led_classdev* led_cdev = dev_get_drvdata(dev);
	struct morse_trig_data* morse_data = led_cdev->trigger_data;

	unsigned long state;
	int ret;

	ret = kstrtoul(buf, 0, &state);

	if(ret)
		return ret;

	morse_data->do_repeat = state;

	return size;

}

static void morse_trig_activate(struct led_classdev* led_cdev) {
	struct morse_trig_data* morse_data;
	int rc;

	int ret;
	struct device* dev_ret;

	/**********************************************
	 * morse trigger setup
	 **********************************************/
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

	morse_data->message = 0;
	morse_data->do_repeat = 0;
	morse_data->phase = 0;
	morse_data->speed = 1;
	morse_data->message_location = 0;
	morse_data->morse_location = CHAR_TO_MORSE[(int)(*morse_data->message_location)];

	led_morse_function(morse_data->timer.data);
	set_bit(LED_BLINK_SW, &led_cdev->work_flags);
	led_cdev->activated = true;

	/********************************************
	 * setup the character device
	 ********************************************/
	if((ret = alloc_chrdev_region(&dev, FIRST_MINOR, MINOR_CNT, "morse_chr_dev")) < 0) {
		// fail
	}

	cdev_init(&c_dev, &fops);

	if((ret = cdev_add(&c_dev, dev, MINOR_CNT)) < 0) {
		// fail
	}

	if(IS_ERR(cl = class_create(THIS_MODULE, "char"))) {
		cdev_del(&c_dev);
		unregister_chrdev_region(dev, MINOR_CNT);
		// fail
	}

	if(IS_ERR(dev_ret = device_create(cl, NULL, dev, NULL, "morse_chr_dev"))) {
		class_destroy(cl);
		cdev_del(&c_dev);
		unregister_chrdev_region(dev, MINOR_CNT);
		// fail
	}


	/*******************************************
	 * setup the buffer
	 *******************************************/
	message_buffer.size = 0;
	message_buffer.next_in = 0;
	message_buffer.next_out = 0;
	memset(message_buffer.data, 0, sizeof(message_buffer.data));
}

static void morse_trig_deactivate(struct led_classdev* led_cdev) {
	struct morse_trig_data* morse_data = led_cdev->trigger_data;

	if(led_cdev->activated) {
		del_timer_sync(&morse_data->timer);
		device_remove_file(led_cdev->dev, &dev_attr_speed);
		kfree(morse_data);
		clear_bit(LED_BLINK_SW, &led_cdev->work_flags);
		led_cdev->activated = false;

		device_destroy(cl, dev);
		class_destroy(cl);
		cdev_del(&c_dev);
		unregister_chrdev_region(dev, MINOR_CNT);
	}
}


static int morse_reboot_notifier(struct notifier_block* nb, unsigned long code, void* unused) {
	led_trigger_unregister(&morse_led_trigger);
	return NOTIFY_DONE;
}

static int morse_panic_notifier(struct notifier_block* nb, unsigned long code, void* unused) {
	panic_morse = 1;
	return NOTIFY_DONE;
}
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

static int on_open(struct inode* i, struct file* f) {
	printk("morse character driver open!\r\n");
	return 0;
}

static int on_close(struct inode* i, struct file* f) {
	printk("morse character driver close!\r\n");
	return 0;
}

static ssize_t on_read(struct file* f, char __user *buf, size_t size, loff_t* ppos) {
	printk("morse character driver read\r\n");
	snprintf(buf, size, "hello there");
	return 0;
}

static ssize_t on_write(struct file* f, const char __user *buf, size_t size, loff_t* ppos) {

	char* local;
	local = 0;

	if(message_buffer.size == BUFFER_SIZE)
		return 0;

	local = kmalloc(size+1, GFP_KERNEL);

	local[size + 1] = 0;

	if(copy_from_user(local, buf, size)) {
		return -EACCES;
	}

	if(mutex_lock_interruptible(&write_lock))
		return -ERESTARTSYS;

	buffer_put(&message_buffer, local);

	mutex_unlock(&write_lock);

	return size;
}

static void buffer_put(struct write_buffer* wb, char* c) {
	wb->data[wb->next_in] = c;
	wb->size++;
	wb->next_in = ((wb->next_in + 1) % BUFFER_SIZE); 
}

static char* buffer_get(struct write_buffer* wb) {
	char* result;

	result = wb->data[wb->next_out];
	wb->size--;
	wb->next_out = ((wb->next_out + 1)  % BUFFER_SIZE);

	return result;
}



module_init(morse_trig_init);
module_exit(morse_trig_exit);

MODULE_AUTHOR("Dakota Alton, Ross Shoger");
MODULE_DESCRIPTION("Morse LED Trigger");
MODULE_LICENSE("GPL");
