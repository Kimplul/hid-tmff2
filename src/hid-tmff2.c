// SPDX-License-Identifier: GPL-2.0-or-later
#include <linux/workqueue.h>
#include <linux/module.h>
#include <linux/hid.h>
#include <linux/version.h>
#include <linux/usb.h>
#include "hid-tmff2.h"

#if LINUX_VERSION_CODE < KERNEL_VERSION(5,16,0)
/* hid_is_usb() was added in 5.16, older kernels only have the generic check */
static inline bool hid_is_usb(struct hid_device *hdev)
{
	return hid_is_using_ll_driver(hdev, &usb_hid_driver);
}
#endif

static int open_mode = 1;
module_param(open_mode, int, 0660);
MODULE_PARM_DESC(open_mode,
		"Whether to send mode change commands on open/close");

#define TIMER_MSECS_MIN 1
#define TIMER_MSECS_MAX 1000
static int timer_msecs = DEFAULT_TIMER_PERIOD;

/* timer_msecs is writable at runtime, so reject values that would make the
 * work handler spin (0) or end up with nonsensical delays (negative) */
static int timer_msecs_set(const char *val, const struct kernel_param *kp)
{
	int value, ret;

	ret = kstrtoint(val, 0, &value);
	if (ret)
		return ret;

	if (value < TIMER_MSECS_MIN || value > TIMER_MSECS_MAX)
		return -EINVAL;

	WRITE_ONCE(*(int *)kp->arg, value);
	return 0;
}

static const struct kernel_param_ops timer_msecs_ops = {
	.set = timer_msecs_set,
	.get = param_get_int,
};
module_param_cb(timer_msecs, &timer_msecs_ops, &timer_msecs, 0660);
MODULE_PARM_DESC(timer_msecs,
		"Timer resolution in msecs (1-1000)");

/* the following parameters are only defaults for newly connected wheels, each
 * wheel keeps its own copy that can be changed through sysfs */
static int spring_level = 30;
module_param(spring_level, int, 0);
MODULE_PARM_DESC(spring_level,
		"Level of spring force (0-100), as per Oversteer standards");

static int damper_level = 30;
module_param(damper_level, int, 0);
MODULE_PARM_DESC(damper_level,
		"Level of damper force (0-100), as per Oversteer standards");

static int friction_level = 30;
module_param(friction_level, int, 0);
MODULE_PARM_DESC(friction_level,
		"Level of friction force (0-100), as per Oversteer standards");

static int range = 900;
module_param(range, int, 0);
MODULE_PARM_DESC(range,
		"Range of wheel, depends on the wheel. Invalid values are ignored");

#define ALT_MODE_KEEP -1
static int alt_mode = ALT_MODE_KEEP;
module_param(alt_mode, int, 0);
MODULE_PARM_DESC(alt_mode,
		"Alternate mode to switch to on connect, eg. 1 for F1 mode on the T300RS (-1 = keep the current mode)");

#define GAIN_MAX 65535
static u16 gain = 40000;
module_param(gain, ushort, 0);
MODULE_PARM_DESC(gain,
		"Level of gain (0-65535)");

#define LEVEL_MAX 100

static u16 tmff2_scale_gain(struct tmff2_device_entry *tmff2, u16 value)
{
	return DIV_ROUND_CLOSEST((u32)value * READ_ONCE(tmff2->gain), GAIN_MAX);
}

static struct tmff2_device_entry *tmff2_from_hdev(struct hid_device *hdev)
{
	struct tmff2_device_entry *tmff2 = hid_get_drvdata(hdev);

	if (!tmff2)
		dev_err(&hdev->dev, "hdev private data not found\n");

	return tmff2;
}

static struct tmff2_device_entry *tmff2_from_input(struct input_dev *input_dev)
{
	struct hid_device *hdev = input_get_drvdata(input_dev);

	if (!hdev) {
		dev_err(&input_dev->dev, "input_dev private data not found\n");
		return NULL;
	}

	return tmff2_from_hdev(hdev);
}

static struct tmff2_device_entry *tmff2_from_dev(struct device *dev)
{
	return tmff2_from_hdev(to_hid_device(dev));
}

/* common parsing for the spring/damper/friction level attributes */
static ssize_t tmff2_level_store(struct device *dev, int *level,
		const char *buf, size_t count)
{
	unsigned int value;
	int ret;

	ret = kstrtouint(buf, 0, &value);
	if (ret)
		return ret;

	if (value > LEVEL_MAX) {
		dev_info_ratelimited(dev, "value %u larger than max %d, clamping to %d\n",
				value, LEVEL_MAX, LEVEL_MAX);
		value = LEVEL_MAX;
	}

	WRITE_ONCE(*level, value);
	return count;
}

static ssize_t spring_level_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct tmff2_device_entry *tmff2 = tmff2_from_dev(dev);

	if (!tmff2)
		return -ENODEV;

	return tmff2_level_store(dev, &tmff2->spring_level, buf, count);
}

static ssize_t spring_level_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tmff2_device_entry *tmff2 = tmff2_from_dev(dev);

	if (!tmff2)
		return -ENODEV;

	return scnprintf(buf, PAGE_SIZE, "%d\n", READ_ONCE(tmff2->spring_level));
}
static DEVICE_ATTR_RW(spring_level);

static ssize_t damper_level_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct tmff2_device_entry *tmff2 = tmff2_from_dev(dev);

	if (!tmff2)
		return -ENODEV;

	return tmff2_level_store(dev, &tmff2->damper_level, buf, count);
}

static ssize_t damper_level_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tmff2_device_entry *tmff2 = tmff2_from_dev(dev);

	if (!tmff2)
		return -ENODEV;

	return scnprintf(buf, PAGE_SIZE, "%d\n", READ_ONCE(tmff2->damper_level));
}
static DEVICE_ATTR_RW(damper_level);

static ssize_t friction_level_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct tmff2_device_entry *tmff2 = tmff2_from_dev(dev);

	if (!tmff2)
		return -ENODEV;

	return tmff2_level_store(dev, &tmff2->friction_level, buf, count);
}

static ssize_t friction_level_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tmff2_device_entry *tmff2 = tmff2_from_dev(dev);

	if (!tmff2)
		return -ENODEV;

	return scnprintf(buf, PAGE_SIZE, "%d\n", READ_ONCE(tmff2->friction_level));
}
static DEVICE_ATTR_RW(friction_level);

static ssize_t range_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct tmff2_device_entry *tmff2 = tmff2_from_dev(dev);
	u16 value;
	int ret;

	if (!tmff2)
		return -ENODEV;

	if ((ret = kstrtou16(buf, 0, &value)))
		return ret;

	if (!tmff2->set_range)
		return -EOPNOTSUPP;

	if ((ret = tmff2->set_range(tmff2->data, value)))
		return ret;

	return count;
}

static ssize_t range_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tmff2_device_entry *tmff2 = tmff2_from_dev(dev);

	if (!tmff2)
		return -ENODEV;

	return scnprintf(buf, PAGE_SIZE, "%u\n", READ_ONCE(tmff2->range));
}
static DEVICE_ATTR_RW(range);

static ssize_t alternate_modes_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct tmff2_device_entry *tmff2 = tmff2_from_dev(dev);

	if (!tmff2)
		return -ENODEV;

	/* returning 0 from a store makes some userspace tools retry forever */
	if (!tmff2->alt_mode_store)
		return -EOPNOTSUPP;

	return tmff2->alt_mode_store(tmff2->data, buf, count);
}

static ssize_t alternate_modes_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tmff2_device_entry *tmff2 = tmff2_from_dev(dev);

	if (!tmff2)
		return -ENODEV;

	if (tmff2->alt_mode_show)
		return tmff2->alt_mode_show(tmff2->data, buf);

	return 0;
}
static DEVICE_ATTR_RW(alternate_modes);

static ssize_t gain_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct tmff2_device_entry *tmff2 = tmff2_from_dev(dev);
	u16 value;
	int ret;

	if (!tmff2)
		return -ENODEV;

	ret = kstrtou16(buf, 0, &value);
	if (ret)
		return ret;

	WRITE_ONCE(tmff2->gain, value);
	/* if we can, update gain immediately, combined with the gain the
	 * application set, just like tmff2_set_gain() does */
	if (tmff2->set_gain)
		tmff2->set_gain(tmff2->data,
				tmff2_scale_gain(tmff2, READ_ONCE(tmff2->ff_gain)));

	return count;
}

static ssize_t gain_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tmff2_device_entry *tmff2 = tmff2_from_dev(dev);

	if (!tmff2)
		return -ENODEV;

	return scnprintf(buf, PAGE_SIZE, "%u\n", READ_ONCE(tmff2->gain));
}
static DEVICE_ATTR_RW(gain);

static struct attribute *tmff2_attrs[] = {
	&dev_attr_gain.attr,
	&dev_attr_alternate_modes.attr,
	&dev_attr_range.attr,
	&dev_attr_spring_level.attr,
	&dev_attr_damper_level.attr,
	&dev_attr_friction_level.attr,
	NULL
};

/* only show the attributes the connected wheel actually supports */
static umode_t tmff2_attr_is_visible(struct kobject *kobj,
		struct attribute *attr, int n)
{
	struct device *dev = kobj_to_dev(kobj);
	struct tmff2_device_entry *tmff2 = hid_get_drvdata(to_hid_device(dev));
	unsigned long param;

	if (!tmff2)
		return 0;

	if (attr == &dev_attr_gain.attr)
		param = PARAM_GAIN;
	else if (attr == &dev_attr_alternate_modes.attr)
		param = PARAM_ALT_MODE;
	else if (attr == &dev_attr_range.attr)
		param = PARAM_RANGE;
	else if (attr == &dev_attr_spring_level.attr)
		param = PARAM_SPRING_LEVEL;
	else if (attr == &dev_attr_damper_level.attr)
		param = PARAM_DAMPER_LEVEL;
	else if (attr == &dev_attr_friction_level.attr)
		param = PARAM_FRICTION_LEVEL;
	else
		return 0;

	return (tmff2->params & param) ? attr->mode : 0;
}

static const struct attribute_group tmff2_attr_group = {
	.attrs = tmff2_attrs,
	.is_visible = tmff2_attr_is_visible,
};

static void tmff2_set_gain(struct input_dev *dev, uint16_t value)
{
	struct tmff2_device_entry *tmff2 = tmff2_from_input(dev);

	if (!tmff2)
		return;

	if (!tmff2->set_gain) {
		hid_err(tmff2->hdev, "missing set_gain\n");
		return;
	}

	WRITE_ONCE(tmff2->ff_gain, value);
	if (tmff2->set_gain(tmff2->data, tmff2_scale_gain(tmff2, value)))
		hid_warn(tmff2->hdev, "unable to set gain\n");
}

static void tmff2_set_autocenter(struct input_dev *dev, uint16_t value)
{
	struct tmff2_device_entry *tmff2 = tmff2_from_input(dev);

	if (!tmff2)
		return;

	if (!tmff2->set_autocenter) {
		hid_err(tmff2->hdev, "missing set_autocenter\n");
		return;
	}

	if (tmff2->set_autocenter(tmff2->data, value))
		hid_warn(tmff2->hdev, "unable to set autocenter\n");
}

static void tmff2_work_handler(struct work_struct *w)
{
	unsigned long lock_flags = 0;
	struct delayed_work *dw = container_of(w, struct delayed_work, work);
	struct tmff2_device_entry *tmff2 = container_of(dw, struct tmff2_device_entry, work);
	struct tmff2_effect_state *state;
	int max_count = 0, effect_id;
	unsigned int elapsed;
	__u16 effect_delay, effect_length;


	if (!tmff2)
		return;

	for (effect_id = 0; effect_id < tmff2->max_effects; ++effect_id) {
		unsigned long actions = 0;
		struct tmff2_effect_state effect;

		state = &tmff2->states[effect_id];

		/* critical section for updating state flags, keep log of what
		 * actions to take after the critical section with actions */
		spin_lock_irqsave(&tmff2->lock, lock_flags);

		effect_delay = state->effect.replay.delay;
		effect_length = state->effect.replay.length;
		if (test_bit(FF_EFFECT_PLAYING, &state->flags) && effect_length) {
			/* jiffies_to_msecs() of the difference is safe against
			 * jiffies wrapping around, 64 bit math avoids overflows
			 * with large repeat counts */
			elapsed = jiffies_to_msecs(jiffies - state->start_time);
			if ((u64)elapsed >=
					(u64)(effect_delay + effect_length) * state->count) {
				__clear_bit(FF_EFFECT_PLAYING, &state->flags);
				__clear_bit(FF_EFFECT_QUEUE_UPDATE, &state->flags);

				state->count = 0;
			}
		}

		if (test_bit(FF_EFFECT_QUEUE_UPLOAD, &state->flags)) {
			__set_bit(FF_EFFECT_QUEUE_UPLOAD, &actions);
			__clear_bit(FF_EFFECT_QUEUE_UPLOAD, &state->flags);
			/* if we're uploading an effect, it's bound to be up
			 * to date */
			__clear_bit(FF_EFFECT_QUEUE_UPDATE, &state->flags);
		}

		if (test_bit(FF_EFFECT_QUEUE_UPDATE, &state->flags)) {
			__set_bit(FF_EFFECT_QUEUE_UPDATE, &actions);
			__clear_bit(FF_EFFECT_QUEUE_UPDATE, &state->flags);
		}

		if (test_bit(FF_EFFECT_QUEUE_START, &state->flags)) {
			__set_bit(FF_EFFECT_QUEUE_START, &actions);
			__clear_bit(FF_EFFECT_QUEUE_START, &state->flags);
			/* effect is playing since we're started it right now */
			__set_bit(FF_EFFECT_PLAYING, &state->flags);
		}

		if (test_bit(FF_EFFECT_QUEUE_STOP, &state->flags)) {
			__set_bit(FF_EFFECT_QUEUE_STOP, &actions);
			__clear_bit(FF_EFFECT_QUEUE_STOP, &state->flags);

			/* the effect can't be playing if we're stopped, aye? */
			__clear_bit(FF_EFFECT_PLAYING, &state->flags);
		}

		if (state->count > max_count)
			max_count = state->count;

		/* copy effect state to local variable so we can pass it around
		 * after the atomic section */
		effect = *state;

		spin_unlock_irqrestore(&tmff2->lock, lock_flags);

		/* perform the identified actions */
		if (test_bit(FF_EFFECT_QUEUE_UPLOAD, &actions)
				&& tmff2->upload_effect(tmff2->data, &effect)) {
			hid_warn(tmff2->hdev, "failed uploading effect\n");
		}

		if (test_bit(FF_EFFECT_QUEUE_UPDATE, &actions)
				&& tmff2->update_effect(tmff2->data, &effect)) {
			hid_warn(tmff2->hdev, "failed updating effect\n");
		}

		if (test_bit(FF_EFFECT_QUEUE_START, &actions)
				&& tmff2->play_effect(tmff2->data, &effect)) {
			hid_warn(tmff2->hdev, "failed starting effect\n");
		}

		if (test_bit(FF_EFFECT_QUEUE_STOP, &actions)
				&& tmff2->stop_effect(tmff2->data, &effect)) {
			hid_warn(tmff2->hdev, "failed stopping effect\n");
		}


		/* wait for each effect update to actually be sent out to avoid
		 * filling up usb output queue, nothing to wait for if nothing
		 * was sent */
		if (actions)
			hid_hw_wait(tmff2->hdev);
	}

	if (max_count && READ_ONCE(tmff2->allow_scheduling))
		schedule_delayed_work(&tmff2->work,
				msecs_to_jiffies(READ_ONCE(timer_msecs)));
}

static void tmff2_rewrite_rumble(struct ff_effect *effect)
{
	/* this is more or less directly copied from
	 * https://elixir.bootlin.com/linux/latest/source/drivers/input/ff-core.c#L48
	 * except we set the direction to east, to make sure the direction
	 * scaling doesn't do any funny business */
	int magnitude = 0;
	if (effect->type != FF_RUMBLE)
		return;

	magnitude = effect->u.rumble.strong_magnitude / 3
		+ effect->u.rumble.weak_magnitude / 6;

	effect->type = FF_PERIODIC;
	effect->direction = 16384;
	effect->u.periodic.waveform = FF_SINE;
	effect->u.periodic.period = 50;
	effect->u.periodic.magnitude = magnitude;
	effect->u.periodic.offset = 0;
	effect->u.periodic.phase = 0;
	effect->u.periodic.envelope.attack_length = 0;
	effect->u.periodic.envelope.attack_level = 0;
	effect->u.periodic.envelope.fade_length = 0;
	effect->u.periodic.envelope.fade_level = 0;
}

static int tmff2_upload(struct input_dev *dev,
		struct ff_effect *effect, struct ff_effect *old)
{
	unsigned long lock_flags = 0;
	struct tmff2_effect_state *state;
	struct tmff2_device_entry *tmff2 = tmff2_from_input(dev);

	if (!tmff2)
		return -ENODEV;

	if (effect->type == FF_PERIODIC && effect->u.periodic.period == 0)
		return -EINVAL;

	state = &tmff2->states[effect->id];

	spin_lock_irqsave(&tmff2->lock, lock_flags);

	state->effect = *effect;
	tmff2_rewrite_rumble(&state->effect);

	if (old) {
		if (!test_bit(FF_EFFECT_QUEUE_UPDATE, &state->flags)) {
			state->old = *old;
			tmff2_rewrite_rumble(&state->old);
		}

		__set_bit(FF_EFFECT_QUEUE_UPDATE, &state->flags);
	} else {
		__set_bit(FF_EFFECT_QUEUE_UPLOAD, &state->flags);
	}

	spin_unlock_irqrestore(&tmff2->lock, lock_flags);
	return 0;
}

static int tmff2_play(struct input_dev *dev, int effect_id, int value)
{
	unsigned long lock_flags = 0;
	struct tmff2_effect_state *state;
	struct tmff2_device_entry *tmff2 = tmff2_from_input(dev);

	if (!tmff2)
		return -ENODEV;

	state = &tmff2->states[effect_id];

	spin_lock_irqsave(&tmff2->lock, lock_flags);
	if (value > 0) {
		state->count = value;
		state->start_time = jiffies;
		__set_bit(FF_EFFECT_QUEUE_START, &state->flags);
		__clear_bit(FF_EFFECT_QUEUE_STOP, &state->flags);
	} else {
		/* a stopped effect doesn't need the work handler anymore, which
		 * keeps rescheduling itself as long as any count is set */
		state->count = 0;
		__set_bit(FF_EFFECT_QUEUE_STOP, &state->flags);
		__clear_bit(FF_EFFECT_QUEUE_START, &state->flags);
	}

	spin_unlock_irqrestore(&tmff2->lock, lock_flags);

	if (!delayed_work_pending(&tmff2->work) && READ_ONCE(tmff2->allow_scheduling))
		schedule_delayed_work(&tmff2->work, 0);

	return 0;
}

/* Installed as the input device's open() in tmff2_input_configured(), before
 * the input device is registered, so no open can slip past it. The input core
 * calls open() and close() with the input device's mutex held, which also
 * protects tmff2->ready. */
static int tmff2_open(struct input_dev *dev)
{
	struct tmff2_device_entry *tmff2 = tmff2_from_input(dev);

	if (!tmff2)
		return -ENODEV;

	/* until the wheel is initialized, tmff2_wheel_init() takes care of
	 * sending the open command */
	if (tmff2->ready && tmff2->open)
		tmff2->open(tmff2->data, READ_ONCE(open_mode));

	return tmff2->hid_open ? tmff2->hid_open(dev) : 0;
}

static void tmff2_close(struct input_dev *dev)
{
	struct tmff2_device_entry *tmff2 = tmff2_from_input(dev);

	if (!tmff2)
		return;

	/* The ff core stops the effects of the closing file right before the
	 * device is closed, which only queues the stop commands. Cancelling
	 * the work would drop them, and without the close command
	 * (open_mode=0) effects would keep playing on the wheel. Send whatever
	 * is still queued, then make sure the work doesn't run again.
	 *
	 * The input core only calls close() when the last user goes away, so
	 * there is no need to check for other users here. */
	if (tmff2->ready) {
		cancel_delayed_work_sync(&tmff2->work);
		tmff2_work_handler(&tmff2->work.work);
		cancel_delayed_work_sync(&tmff2->work);

		if (tmff2->close)
			tmff2->close(tmff2->data, READ_ONCE(open_mode));
	}

	if (tmff2->hid_close)
		tmff2->hid_close(dev);
}

static int tmff2_wheel_init(struct tmff2_device_entry *tmff2)
{
	int ret, i;
	struct ff_device *ff;

	spin_lock_init(&tmff2->lock);
	INIT_DELAYED_WORK(&tmff2->work, tmff2_work_handler);

	/* get parameters etc from backend */
	if ((ret = tmff2->wheel_init(tmff2, open_mode)))
		goto err;


	tmff2->states = kcalloc(tmff2->max_effects, sizeof(struct tmff2_effect_state),
			GFP_KERNEL);

	if (!tmff2->states) {
		ret = -ENOMEM;
		goto err;
	}

	/* set supported effects into input_dev->ffbit */
	for (i = 0; tmff2->supported_effects[i] >= 0; ++i)
		__set_bit(tmff2->supported_effects[i], tmff2->input_dev->ffbit);

	/* tell ffb subsystem that we want to handle our own rumble effects,
	 * this is a (maybe kind of silly) workaround to the ffb subsystem
	 * setting the direction of rumble to 0, which in our case means 0
	 * force. Instead, do exactly what the subsystem would do, but update
	 * the direction.
	 *
	 * It *might* be a good idea to change this in the subsystem proper, but
	 * I don't know if anyone relies on the rumble to be 0 degrees, consider
	 * this just a proof of concept for now */
	if (test_bit(FF_PERIODIC, tmff2->input_dev->ffbit))
		__set_bit(FF_RUMBLE, tmff2->input_dev->ffbit);

	/* create actual ff device*/
	if ((ret = input_ff_create(tmff2->input_dev, tmff2->max_effects))) {
		hid_err(tmff2->hdev, "could not create input_ff\n");
		goto ff_err;
	}

	/* set ff callbacks */
	ff = tmff2->input_dev->ff;
	ff->upload = tmff2_upload;
	ff->playback = tmff2_play;

	/* set defaults wherever possible */
	if (tmff2->set_gain) {
		ff->set_gain = tmff2_set_gain;
		tmff2->set_gain(tmff2->data,
				tmff2_scale_gain(tmff2, tmff2->ff_gain));
	}

	if (tmff2->set_autocenter)
		ff->set_autocenter = tmff2_set_autocenter;

	if (tmff2->set_range)
		tmff2->set_range(tmff2->data, tmff2->range);

	/* only switch if a mode was requested, the wheel reconnects in the
	 * new mode, and switching back to a default here would undo mode
	 * changes made through sysfs */
	if (tmff2->switch_mode && alt_mode != ALT_MODE_KEEP)
		tmff2->switch_mode(tmff2->data, alt_mode);

	/* create sysfs files, which ones are visible depends on params */
	if ((ret = sysfs_create_group(&tmff2->hdev->dev.kobj, &tmff2_attr_group))) {
		hid_err(tmff2->hdev, "unable to create sysfs files\n");
		goto file_err;
	}

	WRITE_ONCE(tmff2->allow_scheduling, 1);

	/* someone may have opened the input device while the wheel was being
	 * initialized, in that case the open command is still missing */
	mutex_lock(&tmff2->input_dev->mutex);
	if (tmff2->input_dev->users && tmff2->open)
		tmff2->open(tmff2->data, READ_ONCE(open_mode));
	tmff2->ready = 1;
	mutex_unlock(&tmff2->input_dev->mutex);

	return 0;

file_err:
	input_ff_destroy(tmff2->input_dev);
ff_err:
	kfree(tmff2->states);
	tmff2->states = NULL;
err:
	return ret;
}

static int tmff2_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	struct tmff2_device_entry *tmff2;
	int ret;

	/* the wheel backends talk to the usb device directly, which is only
	 * valid if the device really is connected via usbhid (and not e.g.
	 * emulated through uhid with a matching vendor/product id) */
	if (!hid_is_usb(hdev))
		return -ENODEV;

	tmff2 = kzalloc(sizeof(struct tmff2_device_entry), GFP_KERNEL);
	if (!tmff2) {
		ret = -ENOMEM;
		goto oom_err;
	}

	tmff2->hdev = hdev;
	hid_set_drvdata(tmff2->hdev, tmff2);

	/* per-device settings, initialized from the module parameters */
	tmff2->spring_level = clamp(spring_level, 0, LEVEL_MAX);
	tmff2->damper_level = clamp(damper_level, 0, LEVEL_MAX);
	tmff2->friction_level = clamp(friction_level, 0, LEVEL_MAX);
	tmff2->range = clamp(range, 0, U16_MAX);
	tmff2->gain = gain;
	/* until an application sets a gain, only the gain above applies */
	tmff2->ff_gain = GAIN_MAX;

	switch (tmff2->hdev->product) {
		case TMT300RS_PS3_NORM_ID:
		case TMT300RS_PS3_ADV_ID:
		case TMT300RS_PS4_NORM_ID:
			if ((ret = t300rs_populate_api(tmff2)))
				goto wheel_err;
			break;

		case TMT248_PC_ID:
			if ((ret = t248_populate_api(tmff2)))
				goto wheel_err;
			break;

		case TX_ACTIVE:
			if ((ret = tx_populate_api(tmff2)))
				goto wheel_err;
			break;

		case TSXW_ACTIVE:
			if ((ret = tsxw_populate_api(tmff2)))
				goto wheel_err;
			break;

		case TMTS_PC_RACER_ID:
			if ((ret = tspc_populate_api(tmff2)))
				goto wheel_err;
			break;

		default:
			ret = -ENODEV;
			goto wheel_err;
	}

	if ((ret = hid_parse(tmff2->hdev))) {
		hid_err(hdev, "parse failed\n");
		goto hid_err;
	}

	if ((ret = hid_hw_start(tmff2->hdev, HID_CONNECT_DEFAULT & ~HID_CONNECT_FF))) {
		hid_err(hdev, "hw start failed\n");
		goto hid_err;
	}

	/* set by tmff2_input_configured() */
	if (!tmff2->input_dev) {
		hid_err(hdev, "no input device found\n");
		ret = -ENODEV;
		goto init_err;
	}

	if ((ret = tmff2_wheel_init(tmff2))) {
		hid_err(hdev, "init failed\n");
		goto init_err;
	}

	return 0;

init_err:
	hid_hw_stop(hdev);
hid_err:
	tmff2->wheel_destroy(tmff2->data);
wheel_err:
	hid_set_drvdata(hdev, NULL);
	kfree(tmff2);
oom_err:
	return ret;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(6,12,0)
static __u8 *tmff2_report_fixup(struct hid_device *hdev, __u8 *rdesc,
		unsigned int *rsize)
#else
static const __u8 *tmff2_report_fixup(struct hid_device *hdev, __u8 *rdesc,
		unsigned int *rsize)
#endif
{
	struct tmff2_device_entry *tmff2 = tmff2_from_hdev(hdev);

	if (!tmff2) /* not entirely sure what the best course of action would be here */
		return rdesc;

	/* hid-core copies the returned descriptor and never writes to it, so
	 * casting away const for kernels before 6.12 is fine */
	if (tmff2->wheel_fixup)
		return (__u8 *)tmff2->wheel_fixup(hdev, rdesc, rsize);

	return rdesc;
}

static void tmff2_remove(struct hid_device *hdev)
{
	struct tmff2_device_entry *tmff2 = tmff2_from_hdev(hdev);

	if (!tmff2)
		return;

	/* stop the work handler from rescheduling itself. tmff2_play() may
	 * still have seen allow_scheduling set and queue the work once more,
	 * which is why the work is cancelled again after hid_hw_stop(), when
	 * the input device (and with it any ff playback) is gone. */
	WRITE_ONCE(tmff2->allow_scheduling, 0);
	cancel_delayed_work_sync(&tmff2->work);

	sysfs_remove_group(&tmff2->hdev->dev.kobj, &tmff2_attr_group);

	hid_hw_stop(hdev);
	cancel_delayed_work_sync(&tmff2->work);

	tmff2->wheel_destroy(tmff2->data);

	hid_set_drvdata(hdev, NULL);
	kfree(tmff2->states);
	kfree(tmff2);
}

static int tmff2_input_configured(struct hid_device *hdev,
				  struct hid_input *hidinput)
{
	struct tmff2_device_entry *tmff2 = hid_get_drvdata(hdev);
	struct input_dev *input = hidinput->input;
	int axis;

	/* hook into open/close of the first input device here, before it is
	 * registered. Doing it after hid_hw_start() would race with userspace
	 * opening the device in between, which then never gets the open
	 * command. */
	if (tmff2 && !tmff2->input_dev) {
		tmff2->input_dev = input;
		tmff2->hid_open = input->open;
		tmff2->hid_close = input->close;
		input->open = tmff2_open;
		input->close = tmff2_close;
	}

	if (!input->absinfo)
		return 0;

	for (axis = ABS_X; axis <= ABS_BRAKE; axis++) {
		if (!test_bit(axis, input->absbit))
			continue;

		input_abs_set_fuzz(input, axis, 0);
		input_abs_set_flat(input, axis, 0);
	}

	return 0;
}

static const struct hid_device_id tmff2_devices[] = {
	/* t300rs and variations */
	{HID_USB_DEVICE(USB_VENDOR_ID_THRUSTMASTER, TMT300RS_PS3_NORM_ID)},
	{HID_USB_DEVICE(USB_VENDOR_ID_THRUSTMASTER, TMT300RS_PS3_ADV_ID)},
	{HID_USB_DEVICE(USB_VENDOR_ID_THRUSTMASTER, TMT300RS_PS4_NORM_ID)},
	/* t248 PC*/
	{HID_USB_DEVICE(USB_VENDOR_ID_THRUSTMASTER, TMT248_PC_ID)},
	/* tx */
	{HID_USB_DEVICE(USB_VENDOR_ID_THRUSTMASTER, TX_ACTIVE)},
	/* TS-PC RACER */
	{HID_USB_DEVICE(USB_VENDOR_ID_THRUSTMASTER, TMTS_PC_RACER_ID)},
	/* tsxw */
	{HID_USB_DEVICE(USB_VENDOR_ID_THRUSTMASTER, TSXW_ACTIVE)},

	{}
};
MODULE_DEVICE_TABLE(hid, tmff2_devices);

static struct hid_driver tmff2_driver = {
	.name = "tmff2",
	.id_table = tmff2_devices,
	.probe = tmff2_probe,
	.remove = tmff2_remove,
	.report_fixup = tmff2_report_fixup,
	.input_configured = tmff2_input_configured,
};
module_hid_driver(tmff2_driver);

MODULE_DESCRIPTION("FFB for Thrustmaster wheels");
MODULE_LICENSE("GPL");
