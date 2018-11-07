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
#include "../leds.h"

static int panic_morse;

struct morse_trig_data {
	// fill out with stuff pertaining to 
	// how the led should act
};

// this function ddtermines the state of the led and how long it will 
// be on for
static void led_morse_function(unsigned long data) {

}

// learn what this does 
static ssize_t led_invert_show(struct device* dev, struct device_attribute* attr, char* buf) {
	return 0;
}

// same as above 	
static ssize_t led_invert_store(struct device* dev, struct device_attribute* attr, const char* buf, size_t size) {
	return 0;
}

static DEVICE_ATTR(invert, 0644, led_invert_show, led_invert_store);

// actually triggers the led_morse_function? (probably just copy)
static void morse_trig_activate(struct led_classdev* led_cdev) {

}

// stop the led, (probably just copy)
static void morse_trig_deactivate(struct led_classdef* led_cdev) {

}

// data for registering this driver
static struct led_trigger morse_led_trigger = {
	.name = "morse",
	.activate = morse_trig_activate,
	.deactivate = morse_trig_deactivate
};

// learn what this does
static int morse_reboot_notifier(struct notifier_block* nb, unsigned long code, void* unused) {
	return 0;
}

// learn what this does
static int morse_panic_notifier(struct notifier_block* nb, unsigned long code, void* unused) {
	return 0;
}

static struct notifier_block morse_reboot_nb = {
	.notifier_call = morse_reboot_notifier
};

static struct notifier_block morse_panic_nb = {
	.notifier_call = morse_panic_notifier	
};

static int __init morse_trig_init() {
	int rc = led_trigger_register(&morse_led_trigger);

	if(!rc) {
		atomic_notifier_chain_register(&panic_notifier_list, &morse_panic_nb);
		register_reboot_notifer(&morse_reboot_nb);
	}
	
	return rc;
}

static void __exit morse_trig_exit() {
	unregister_reboot_notifier(&morse_reboot_nb);
	atomic_notifier_chain_unregister(&panic_notifier_list, &morse_panic_nb);
	led_trigger_unregister(&morse_led_trigger);
}

module_init(morse_trig_init);
module_exit(morse_trig_exit);

MODULE_AUTHOR("Dakota Alton <altond@oregonstate.edu>");
MODULE_DESCRIPTION("Morse LED Trigger");
MODULE_LICENSE("GPL");
