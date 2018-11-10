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
	char message[81];	

	// where in the message are we
	char* message_location;
	
	// what part of the morse code are we at
	char* morse_location;

	
	unsigned int period;
	struct timer_list timer;
	unsigned int invert;
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
	// dit and char-wise space
	long dit = msecs_to_jiffies(70);
	// dah and word-wise space
	long dah = msecs_to_jiffies(3 * 70);


	struct led_classdev* led_cdev = (struct led_classdev*) data;
	struct morse_trig_data* morse_data = led_cdev->trigger_data;
	unsigned long brightness = LED_OFF;

	// time to next call of this function
	unsigned long delay = 0;

	if(unlikely(panic_morse)) {
		led_set_brightness_nosleep(led_cdev, LED_OFF);
		return;
	}

	if(test_and_clear_bit(LED_BLINK_BRIGHTNESS_CHANGE, &led_cdev->work_flags)) 
		led_cdev->blink_brightness = led_cdev->new_blink_brightness;

	/* just flicker between on and off for simple 
	 * test`
	 */
	switch(*morse_data->morse_location) {
		case '-':
			morse_data->period = dah;
			delay = dah;

			morse_data->morse_location++;

			if(!morse_data->invert)
				brightness = led_cdev->blink_brightness;
			break;

		case '.':
			morse_data->period = dit;
			delay = dah;

			morse_data->morse_location++;
			
			if(morse_data->invert)
				brightness = led_cdev->blink_brightness;

			break;

		// morse_location = '\0', this also represents moving to a new word
		default:
			delay = 2 * dah;

			morse_data->message_location++;
			// check to see if we are at the last character in the message
			if(*morse_data->message_location == '\0') 
				morse_data->message_location = morse_data->message;	

			morse_data->morse_location = CHAR_TO_MORSE[(int)(*morse_data->message_location)];

			if(morse_data->invert)
				brightness = led_cdev->blink_brightness;

			break;
	};

	led_set_brightness_nosleep(led_cdev, brightness);
	mod_timer(&morse_data->timer, jiffies + delay);
}

static ssize_t led_invert_show(struct device* dev, struct device_attribute* attr, char* buf) {
	struct led_classdev* led_cdev = dev_get_drvdata(dev);
	struct morse_trig_data* morse_data = led_cdev->trigger_data;

	return sprintf(buf,"%u\n", morse_data->invert);
}

static ssize_t led_invert_store(struct device* dev, struct device_attribute* attr, const char* buf, size_t size) {
	struct led_classdev* led_cdev = dev_get_drvdata(dev);
	struct morse_trig_data* morse_data = led_cdev->trigger_data;

	unsigned long state;
	int ret;

	ret = kstrtoul(buf, 0, &state);

	if(ret)
		return ret;

	// !! - seems strange
	morse_data->invert = !!state;

	return size;
}

static DEVICE_ATTR(invert, 0644, led_invert_show, led_invert_store);

static void morse_trig_activate(struct led_classdev* led_cdev) {
	struct morse_trig_data* morse_data;
	int rc;
	char* message;
	char *dst, *src;
	
	morse_data = kzalloc(sizeof(*morse_data), GFP_KERNEL);
	if(!morse_data)
		return;

	led_cdev->trigger_data = morse_data;
	rc = device_create_file(led_cdev->dev, &dev_attr_invert);
	if(rc) {
		kfree(led_cdev->trigger_data);
		return;
	}

	setup_timer(&morse_data->timer, led_morse_function, (unsigned long)led_cdev);

	// this will probably change as it seems like 
	// this info is used to determine when the led is 
	// to be on/off
	message = "sos";
	dst = morse_data->message;
	
	for(src = message; src != '\0'; src++) {
		*dst = (*src);
		dst++;
	}

	*dst = '\0';

	morse_data->message_location = morse_data->message;
	morse_data->morse_location = CHAR_TO_MORSE[(int)(*morse_data->message_location)];

	if(!led_cdev->blink_brightness)
		led_cdev->blink_brightness = led_cdev->max_brightness;

	led_morse_function(morse_data->timer.data);
	set_bit(LED_BLINK_SW, &led_cdev->work_flags);
	led_cdev->activated = true;
}

static void morse_trig_deactivate(struct led_classdev* led_cdev) {
	struct morse_trig_data* morse_data = led_cdev->trigger_data;

	if(led_cdev->activated) {
		del_timer_sync(&morse_data->timer);
		device_remove_file(led_cdev->dev, &dev_attr_invert);
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
