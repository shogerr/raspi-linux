/*
 * LED Morse Trigger Trigger
 *
 * Copyright (C) 2006 Atsushi Nemoto <anemo@mba.ocn.ne.jp>
 *
 * Based on Atsushi Nemoto's ledtrig-heartbeast.c.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
=======
/**
 *
 * LED Morse Trigger
 *
 *
>>>>>>> altond_working
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
#include "../leds.h"

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

struct morse_trig_data {

	// start with an 81 char message, 
	// may have to change when it comes
	// time to do project4
	char message[10];	
	unsigned int message_;

	// where in the message are we
	const char* message_location;
	
	// what part of the morse code are we at
	const char* morse_location;

	unsigned int phase;
	struct timer_list timer;
};

/** this function ddtermines the state of the led and how long it will 
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
	unsigned long dit = 200;

	if (morse_data->phase == 1)
	{
		delay = dit;
		brightness = LED_OFF;
		morse_data->phase = 0;
		goto update;
	}

	//printk(KERN_INFO "letter is: %c\n", *morse_data->message_location);
	//printk(KERN_INFO "morse is: %c\n", *morse_data->morse_location);

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
			morse_data->message_location = morse_data->message;	
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

static ssize_t led_message_show(struct device* dev, struct device_attribute* attr, char* buf) {
	struct led_classdev* led_cdev = dev_get_drvdata(dev);
	struct morse_trig_data* morse_data = led_cdev->trigger_data;

	return sprintf(buf,"%u\n", morse_data->message_);
}

static ssize_t led_message_store(struct device* dev, struct device_attribute* attr, const char* buf, size_t size) {
	struct led_classdev* led_cdev = dev_get_drvdata(dev);
	struct morse_trig_data* morse_data = led_cdev->trigger_data;

	unsigned long state;
	int ret;

	ret = kstrtoul(buf, 0, &state);
	if (ret)
		return ret;

	morse_data->invert = !!state;

	return size;
}

static DEVICE_ATTR(message, 0644, led_message_show, led_message_store);

static void morse_trig_activate(struct led_classdev* led_cdev) {
	struct morse_trig_data* morse_data;
	int rc;
	char* message;
	char *dst, *src;
	
	morse_data = kzalloc(sizeof(*morse_data), GFP_KERNEL);
	if(!morse_data)
		return;

	led_cdev->trigger_data = morse_data;
	rc = device_create_file(led_cdev->dev, &dev_attr_message);
	if(rc) {
		kfree(led_cdev->trigger_data);
		return;
	}

	setup_timer(&morse_data->timer, led_morse_function, (unsigned long)led_cdev);

	// this will probably change as it seems like 
	// this info is used to determine when the led is 
	// to be on/off
	message = "sos com";
	dst = morse_data->message;
	
	for(src = message; *src != '\0'; src++) {
		*dst = (*src);
		dst++;
	}

	*dst = '\0';
	printk(KERN_INFO "activate 2 message is %s\n", morse_data->message);

	morse_data->phase = 0;
	morse_data->message_location = morse_data->message;
	morse_data->morse_location = CHAR_TO_MORSE[(int)(*morse_data->message_location)];

	led_morse_function(morse_data->timer.data);
	set_bit(LED_BLINK_SW, &led_cdev->work_flags);
	led_cdev->activated = true;
}

static void morse_trig_deactivate(struct led_classdev* led_cdev) {
	struct morse_trig_data* morse_data = led_cdev->trigger_data;

	if(led_cdev->activated) {
		del_timer_sync(&morse_data->timer);
		device_remove_file(led_cdev->dev, &dev_attr_message);
		kfree(morse_data);
		clear_bit(LED_BLINK_SW, &led_cdev->work_flags);
		led_cdev->activated = false;
	}
}

static struct led_trigger morse_led_trigger = {
	.name     = "morse",
	.activate = morse_trig_activate,
	.deactivate = morse_trig_deactivate,
};

static int morse_reboot_notifier(struct notifier_block *nb,
				     unsigned long code, void *unused)
{
	led_trigger_unregister(&morse_led_trigger);
	return NOTIFY_DONE;
}

static int morse_panic_notifier(struct notifier_block *nb,
				     unsigned long code, void *unused)
{
	panic_morses = 1;
	return NOTIFY_DONE;
}

static struct notifier_block morse_reboot_nb = {
	.notifier_call = morse_reboot_notifier,
};

static struct notifier_block morse_panic_nb = {
	.notifier_call = morse_panic_notifier,
};

static int __init morse_trig_init(void)
{
	int rc = led_trigger_register(&morse_led_trigger);

	if (!rc) {
		atomic_notifier_chain_register(&panic_notifier_list,
					       &morse_panic_nb);
		register_reboot_notifier(&morse_reboot_nb);
	}
	return rc;
}

static void __exit morse_trig_exit(void)
{
	unregister_reboot_notifier(&morse_reboot_nb);
	atomic_notifier_chain_unregister(&panic_notifier_list,
					 &morse_panic_nb);
	led_trigger_unregister(&morse_led_trigger);
}

module_init(morse_trig_init);
module_exit(morse_trig_exit);

MODULE_AUTHOR("Dakota Alton, Ross Shoger");
MODULE_DESCRIPTION("Morse LED trigger");
MODULE_LICENSE("GPL");
