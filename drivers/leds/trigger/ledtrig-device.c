/*
 * LED Device Activity Trigger
 *
 * Copyright 2015 Maciej Borzecki <maciek.borzecki@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/leds.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/rwsem.h>
#include <linux/kdev_t.h>

#define BLINK_DELAY 30
static unsigned long blink_delay = BLINK_DELAY;

static DECLARE_RWSEM(devs_list_lock);
static LIST_HEAD(devs_list);

#define MAX_NAME_LEN 20

struct ledtrig_dev_data {
	char name[MAX_NAME_LEN];
	dev_t dev;
	struct led_trigger *trig;
	struct list_head node;
};

/**
 * ledtrig_dev_activity - signal activity on device
 * @dev: device
 *
 * Fires a trigger assigned to @dev device.
 */
void ledtrig_dev_activity(dev_t dev)
{
	struct ledtrig_dev_data *dev_trig;

	if (!down_read_trylock(&devs_list_lock))
		return;

	list_for_each_entry(dev_trig, &devs_list, node) {
		if (dev_trig->dev == dev) {
			led_trigger_blink_oneshot(dev_trig->trig,
						  &blink_delay,
						  &blink_delay,
						  0);
			break;
		}
	}
	up_read(&devs_list_lock);
}
EXPORT_SYMBOL(ledtrig_dev_activity);

static struct ledtrig_dev_data *ledtrig_dev_new(dev_t dev)
{
	struct ledtrig_dev_data *dev_trig;

	dev_trig = kzalloc(sizeof(*dev_trig), GFP_KERNEL);
	if (!dev_trig)
		return NULL;

	INIT_LIST_HEAD(&dev_trig->node);
	dev_trig->dev = dev;
	snprintf(dev_trig->name, sizeof(dev_trig->name),
		 "dev-%u:%u", MAJOR(dev), MINOR(dev));

	return dev_trig;
}

static void ledtrig_dev_release(struct ledtrig_dev_data *dev_trig)
{
	led_trigger_unregister_simple(dev_trig->trig);

	kfree(dev_trig);
}

/**
 * ledtrig_dev_add - add a trigger for device
 * @dev: device for which the trigger is to be added
 *
 * Create and register a new trigger for device @dev. The trigger will
 * show up as dev-<major>:<minor> in the list of avaialble LED
 * triggers.
 */
void ledtrig_dev_add(dev_t dev)
{
	int found = 0;
	struct ledtrig_dev_data *new_dev_trig;
	struct ledtrig_dev_data *dev_trig;

	new_dev_trig = ledtrig_dev_new(dev);
	if (!new_dev_trig)
		return;

	down_write(&devs_list_lock);
	list_for_each_entry(dev_trig, &devs_list, node) {
		if (dev_trig->dev == dev) {
			found = 1;
			break;
		}
	}
	if (!found)
		list_add(&new_dev_trig->node, &devs_list);
	up_write(&devs_list_lock);

	if (!found)
		/* register with led triggers */
		led_trigger_register_simple(new_dev_trig->name,
					    &new_dev_trig->trig);
	else
		kfree(new_dev_trig);
}
EXPORT_SYMBOL(ledtrig_dev_add);

/**
 * ledtrig_dev_del - delete a trigger
 * @dev: device for which to delete a trigger
 */
void ledtrig_dev_del(dev_t dev)
{

	struct ledtrig_dev_data *dev_trig;

	down_write(&devs_list_lock);
	list_for_each_entry(dev_trig, &devs_list, node) {
		if (dev_trig->dev == dev) {
			/* remove from devs list */
			list_del(&dev_trig->node);

			/* unregister & release data */
			ledtrig_dev_release(dev_trig);
			break;
		}
	}
	up_write(&devs_list_lock);

}
EXPORT_SYMBOL(ledtrig_dev_del);

static void ledtrig_dev_remove_all(void)
{
	struct list_head *en;

	down_write(&devs_list_lock);
	list_for_each(en, &devs_list) {
		struct list_head *prev = en->prev;
		struct ledtrig_dev_data *dev_trig;

		dev_trig = list_entry(en, struct ledtrig_dev_data,
				      node);
		/* remove from list */
		list_del(en);

		/* unregister & release data */
		ledtrig_dev_release(dev_trig);

		/* and go back */
		en = prev;
	}
	up_write(&devs_list_lock);
}

static int __init ledtrig_dev_init(void)
{
	return 0;
}

static void __exit ledtrig_dev_exit(void)
{
	ledtrig_dev_remove_all();
}

module_init(ledtrig_dev_init);
module_exit(ledtrig_dev_exit);

MODULE_AUTHOR("Maciej Borzecki <maciek.borzecki@gmail.com>");
MODULE_DESCRIPTION("LED Device Activity Trigger");
MODULE_LICENSE("GPL");
