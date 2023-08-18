// SPDX-License-Identifier: GPL-2.0
#include <linux/workqueue.h>
#include <linux/module.h>
#include <linux/hid.h>
#include "hid-tmff2.h"


int open_mode = 1;
module_param(open_mode, int, 0660);
MODULE_PARM_DESC(open_mode,
		"Whether to send mode change commands on open/close");

int timer_msecs = DEFAULT_TIMER_PERIOD;
module_param(timer_msecs, int, 0660);
MODULE_PARM_DESC(timer_msecs,
		"Timer resolution in msecs");

/* should these be removed and just rely on /sys? */
int spring_level = 30;
module_param(spring_level, int, 0);
MODULE_PARM_DESC(spring_level,
		"Level of spring force (0-100), as per Oversteer standards");

int damper_level = 30;
module_param(damper_level, int, 0);
MODULE_PARM_DESC(damper_level,
		"Level of damper force (0-100), as per Oversteer standards");

int friction_level = 30;
module_param(friction_level, int, 0);
MODULE_PARM_DESC(friction_level,
		"Level of friction force (0-100), as per Oversteer standards");

int range = 900;
module_param(range, int, 0);
MODULE_PARM_DESC(range,
		"Range of wheel, depends on the wheel. Invalid values are ignored");

int alt_mode = 0;
module_param(alt_mode, int, 0);
MODULE_PARM_DESC(alt_mode,
		"Alternate mode, eg. F1 mode");

#define GAIN_MAX 65535
int gain = 40000;
module_param(gain, int, 0);
MODULE_PARM_DESC(gain,
		"Level of gain (0-65535)");

static spinlock_t lock;
static unsigned long lock_flags;

static struct tmff2_device_entry *tmff2_from_hdev(struct hid_device *hdev)
{
	struct tmff2_device_entry *tmff2;
	spin_lock_irqsave(&lock, lock_flags);

	if (!(tmff2 = hid_get_drvdata(hdev)))
		dev_err(&hdev->dev, "hdev private data not found\n");

	spin_unlock_irqrestore(&lock, lock_flags);

	return tmff2;
}

static struct tmff2_device_entry *tmff2_from_input(struct input_dev *input_dev)
{
	struct hid_device *hdev;
	spin_lock_irqsave(&lock, lock_flags);

	if (!(hdev = input_get_drvdata(input_dev)))
		dev_err(&input_dev->dev, "input_dev private data not found\n");

	spin_unlock_irqrestore(&lock, lock_flags);

	return tmff2_from_hdev(hdev);
}

static ssize_t spring_level_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int value;
	int ret;

	ret = kstrtouint(buf, 0, &value);
	if (ret) {
		dev_err(dev, "kstrtouint failed at spring_level_store: %i", ret);
		return ret;
	}

	if (value > 100) {
		dev_info(dev, "value %i larger than max 100, clamping to 100.\n", value);
		value = 100;
	}

	spring_level = value;

	return count;
}

static ssize_t spring_level_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{

	return scnprintf(buf, PAGE_SIZE, "%u\n", spring_level);
}
static DEVICE_ATTR_RW(spring_level);

static ssize_t damper_level_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int value;
	int ret;


	ret = kstrtouint(buf, 0, &value);
	if (ret) {
		dev_err(dev, "kstrtouint failed at damper_level_store: %i", ret);
		return ret;
	}

	if (value > 100) {
		dev_info(dev, "value %i larger than max 100, clamping to 100.\n", value);
		value = 100;
	}

	damper_level = value;

	return count;
}

static ssize_t damper_level_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{

	return scnprintf(buf, PAGE_SIZE, "%u\n", damper_level);
}
static DEVICE_ATTR_RW(damper_level);

static ssize_t friction_level_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned int value;
	int ret;


	ret = kstrtouint(buf, 0, &value);
	if (ret) {
		dev_err(dev, "kstrtouint failed at friction_level_store: %i", ret);
		return ret;
	}

	if (value > 100) {
		dev_info(dev, "value %i larger than max 100, clamping to 100.\n", value);
		value = 100;
	}

	friction_level = value;

	return count;
}

static ssize_t friction_level_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	size_t count;


	count = scnprintf(buf, PAGE_SIZE, "%u\n", friction_level);

	return count;
}
static DEVICE_ATTR_RW(friction_level);

static ssize_t range_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct tmff2_device_entry *tmff2 = tmff2_from_hdev(to_hid_device(dev));
	unsigned int value;
	int ret;


	if (!tmff2)
		return -ENODEV;

	if ((ret = kstrtouint(buf, 0, &value))) {
		hid_err(tmff2->hdev, "kstrtouint failed at range_store: %i", ret);
		return ret;
	}

	if (tmff2->set_range) {
		if ((ret = tmff2->set_range(tmff2->data, value)))
			return ret;
	}

	return count;
}

static ssize_t range_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{

	return scnprintf(buf, PAGE_SIZE, "%u\n", range);
}
static DEVICE_ATTR_RW(range);

static ssize_t alternate_modes_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct tmff2_device_entry *tmff2 = tmff2_from_hdev(to_hid_device(dev));

	if (!tmff2)
		return -ENODEV;

	if (tmff2->alt_mode_store)
		return tmff2->alt_mode_store(tmff2->data, buf, count);

	return 0;
}

static ssize_t alternate_modes_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tmff2_device_entry *tmff2 = tmff2_from_hdev(to_hid_device(dev));

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
	struct tmff2_device_entry *tmff2 = tmff2_from_hdev(to_hid_device(dev));
	unsigned int value;
	int ret;

	if (!tmff2)
		return -ENODEV;

	if ((ret = kstrtouint(buf, 0, &value))) {
		dev_err(dev, "kstrtouint failed at gain_store: %i", ret);
		return ret;
	}

	gain = value;
	if (tmff2->set_gain) /* if we can, update gain immediately */
		tmff2->set_gain(tmff2->data, (GAIN_MAX * gain) / GAIN_MAX);

	return count;
}

static ssize_t gain_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%i\n", gain);
}
static DEVICE_ATTR_RW(gain);

static void tmff2_set_gain(struct input_dev *dev, uint16_t value)
{
	struct tmff2_device_entry *tmff2 = tmff2_from_input(dev);

	if (!tmff2)
		return;

	if (!tmff2->set_gain) {
		hid_err(tmff2->hdev, "missing set_gain\n");
		return;
	}

	if (tmff2->set_gain(tmff2->data, (value * gain) / GAIN_MAX))
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
	struct delayed_work *dw = container_of(w, struct delayed_work, work);
	struct tmff2_device_entry *tmff2 = container_of(dw, struct tmff2_device_entry, work);
	struct tmff2_effect_state *state;
	int max_count = 0, effect_id;
	unsigned long time_now;
	__u16 effect_length;


	if (!tmff2)
		return;

	for (effect_id = 0; effect_id < tmff2->max_effects; ++effect_id) {
		spin_lock(&tmff2->lock);

		time_now = JIFFIES2MS(jiffies);
		state = &tmff2->states[effect_id];

		effect_length = state->effect.replay.length;
		if (test_bit(FF_EFFECT_PLAYING, &state->flags) && effect_length) {
			if ((time_now - state->start_time) >= effect_length) {
				__clear_bit(FF_EFFECT_PLAYING, &state->flags);
				__clear_bit(FF_EFFECT_QUEUE_UPDATE, &state->flags);

				if (state->count)
					state->count--;

				if (state->count)
					__set_bit(FF_EFFECT_QUEUE_START, &state->flags);
			}
		}

		if (test_bit(FF_EFFECT_QUEUE_UPLOAD, &state->flags)) {
			if (tmff2->upload_effect(tmff2->data, state)) {
				hid_warn(tmff2->hdev, "failed uploading effect\n");
			} else {
				__clear_bit(FF_EFFECT_QUEUE_UPLOAD, &state->flags);
				/* if we're uploading an effect, it's bound to be the up
				 * to date available */
				__clear_bit(FF_EFFECT_QUEUE_UPDATE, &state->flags);
			}
		}

		if (test_bit(FF_EFFECT_QUEUE_UPDATE, &state->flags)) {
			if (tmff2->update_effect(tmff2->data, state))
				hid_warn(tmff2->hdev, "failed updating effect\n");
			else
				__clear_bit(FF_EFFECT_QUEUE_UPDATE, &state->flags);
		}

		if (test_bit(FF_EFFECT_QUEUE_START, &state->flags)) {
			if (tmff2->play_effect(tmff2->data, state)) {
				hid_warn(tmff2->hdev, "failed starting effect\n");
			} else {
				__clear_bit(FF_EFFECT_QUEUE_START, &state->flags);
				__set_bit(FF_EFFECT_PLAYING, &state->flags);
			}

		}

		if (test_bit(FF_EFFECT_QUEUE_STOP, &state->flags)) {
			if (tmff2->stop_effect(tmff2->data, state)) {
				hid_warn(tmff2->hdev, "failed stopping effect\n");
			} else {
				__clear_bit(FF_EFFECT_PLAYING, &state->flags);
				__clear_bit(FF_EFFECT_QUEUE_STOP, &state->flags);
			}
		}

		if (state->count > max_count)
			max_count = state->count;

		spin_unlock(&tmff2->lock);
	}

	if (max_count && tmff2->allow_scheduling)
		schedule_delayed_work(&tmff2->work, msecs_to_jiffies(timer_msecs));
}

static int tmff2_upload(struct input_dev *dev,
		struct ff_effect *effect, struct ff_effect *old)
{
	struct tmff2_effect_state *state;
	struct tmff2_device_entry *tmff2 = tmff2_from_input(dev);

	if (!tmff2)
		return -ENODEV;

	if (effect->type == FF_PERIODIC && effect->u.periodic.period == 0)
		return -EINVAL;

	state = &tmff2->states[effect->id];

	spin_lock(&tmff2->lock);

	state->effect = *effect;

	if (old) {
		if (!test_bit(FF_EFFECT_QUEUE_UPDATE, &state->flags))
			state->old = *old;

		__set_bit(FF_EFFECT_QUEUE_UPDATE, &state->flags);
	} else {
		__set_bit(FF_EFFECT_QUEUE_UPLOAD, &state->flags);
	}

	spin_unlock(&tmff2->lock);
	return 0;
}

static int tmff2_play(struct input_dev *dev, int effect_id, int value)
{
	struct tmff2_effect_state *state;
	struct tmff2_device_entry *tmff2 = tmff2_from_input(dev);

	if (!tmff2)
		return -ENODEV;

	state = &tmff2->states[effect_id];
	if (!state)
		return 0;

	spin_lock(&tmff2->lock);
	if (value > 0) {
		state->count = value;
		state->start_time = JIFFIES2MS(jiffies);
		__set_bit(FF_EFFECT_QUEUE_START, &state->flags);
		__clear_bit(FF_EFFECT_QUEUE_STOP, &state->flags);
	} else {
		__set_bit(FF_EFFECT_QUEUE_STOP, &state->flags);
		__clear_bit(FF_EFFECT_QUEUE_START, &state->flags);
	}

	spin_unlock(&tmff2->lock);

	if (!delayed_work_pending(&tmff2->work) && tmff2->allow_scheduling)
		schedule_delayed_work(&tmff2->work, 0);

	return 0;
}

static int tmff2_open(struct input_dev *dev)
{
	struct tmff2_device_entry *tmff2 = tmff2_from_input(dev);

	if (!tmff2)
		return -ENODEV;

	if (tmff2->open)
		return tmff2->open(tmff2->data, open_mode);

	hid_err(tmff2->hdev, "no open callback set\n");
	return -EINVAL;
}

static void tmff2_close(struct input_dev *dev)
{
	struct tmff2_device_entry *tmff2 = tmff2_from_input(dev);

	if (!tmff2)
		return;

	/* since we're closing the device, no need to continue feeding it new data */
	cancel_delayed_work_sync(&tmff2->work);

	if (tmff2->close) {
		tmff2->close(tmff2->data, open_mode);
		return;
	}

	hid_err(tmff2->hdev, "no close callback set\n");
}

static int tmff2_create_files(struct tmff2_device_entry *tmff2)
{
	struct device *dev = &tmff2->hdev->dev;
	int ret;


	/* could use short circuiting but this is more explicit */
	if (tmff2->params & PARAM_GAIN) {
		if ((ret = device_create_file(dev, &dev_attr_gain))) {
			hid_err(tmff2->hdev, "unable to create sysfs for gain\n");
			goto gain_err;
		}
	}

	if (tmff2->params & PARAM_ALT_MODE) {
		if ((ret = device_create_file(dev, &dev_attr_alternate_modes))) {
			hid_err(tmff2->hdev, "unable to create sysfs for alternate_modes\n");
			goto alt_err;
		}
	}

	if (tmff2->params & PARAM_RANGE) {
		if ((ret = device_create_file(dev, &dev_attr_range))) {
			hid_warn(tmff2->hdev, "unable to create sysfs for range\n");
			goto range_err;
		}
	}

	if (tmff2->params & PARAM_SPRING_LEVEL) {
		if ((ret = device_create_file(dev, &dev_attr_spring_level))) {
			hid_warn(tmff2->hdev, "unable to create sysfs for spring_level\n");
			goto spring_err;
		}
	}

	if (tmff2->params & PARAM_DAMPER_LEVEL) {
		if ((ret = device_create_file(dev, &dev_attr_damper_level))) {
			hid_warn(tmff2->hdev, "unable to create sysfs for damper_level\n");
			goto damper_err;
		}
	}

	if (tmff2->params & PARAM_FRICTION_LEVEL) {
		if ((ret = device_create_file(dev, &dev_attr_friction_level))) {
			hid_warn(tmff2->hdev, "unable to create sysfs for friction_level\n");
			goto friction_err;
		}
	}

	return 0;

friction_err:
	device_remove_file(dev, &dev_attr_damper_level);
damper_err:
	device_remove_file(dev, &dev_attr_spring_level);
spring_err:
	device_remove_file(dev, &dev_attr_range);
range_err:
	device_remove_file(dev, &dev_attr_alternate_modes);
alt_err:
	device_remove_file(dev, &dev_attr_gain);
gain_err:
	return ret;
}

static int tmff2_wheel_init(struct tmff2_device_entry *tmff2)
{
	int ret, i;
	struct ff_device *ff;

	spin_lock_init(&lock);
	spin_lock_init(&tmff2->lock);
	INIT_DELAYED_WORK(&tmff2->work, tmff2_work_handler);

	/* get parameters etc from backend */
	if ((ret = tmff2->wheel_init(tmff2, open_mode)))
		goto err;


	tmff2->states = kzalloc(sizeof(struct tmff2_effect_state) * tmff2->max_effects,
			GFP_KERNEL);

	if (!tmff2->states) {
		ret = -ENOMEM;
		goto err;
	}

	/* set supported effects into input_dev->ffbit */
	for (i = 0; tmff2->supported_effects[i] >= 0; ++i)
		__set_bit(tmff2->supported_effects[i], tmff2->input_dev->ffbit);

	/* create actual ff device*/
	if ((ret = input_ff_create(tmff2->input_dev, tmff2->max_effects))) {
		hid_err(tmff2->hdev, "could not create input_ff\n");
		goto err;
	}

	/* set ff callbacks */
	ff = tmff2->input_dev->ff;
	ff->upload = tmff2_upload;
	ff->playback = tmff2_play;

	if (tmff2->open)
		tmff2->input_dev->open = tmff2_open;

	if (tmff2->close)
		tmff2->input_dev->close = tmff2_close;

	/* set defaults wherever possible */
	if (tmff2->set_gain) {
		ff->set_gain = tmff2_set_gain;
		tmff2->set_gain(tmff2->data, (GAIN_MAX * gain) / GAIN_MAX);
	}

	if (tmff2->set_autocenter)
		ff->set_autocenter = tmff2_set_autocenter;

	if (tmff2->set_range)
		tmff2->set_range(tmff2->data, range);

	if (tmff2->switch_mode)
		tmff2->switch_mode(tmff2->data, alt_mode);

	/* create files */
	if ((ret = tmff2_create_files(tmff2)))
		goto err;

	tmff2->allow_scheduling = 1;
	return 0;

	input_ff_destroy(tmff2->input_dev);
err:
	return ret;
}

static int tmff2_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	struct tmff2_device_entry *tmff2 =
		kzalloc(sizeof(struct tmff2_device_entry), GFP_KERNEL);

	int ret;


	if (!tmff2) {
		ret = -ENOMEM;
		goto oom_err;
	}

	tmff2->hdev = hdev;
	hid_set_drvdata(tmff2->hdev, tmff2);

	switch (tmff2->hdev->product) {
		/* t300rs */
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

	tmff2->input_dev = list_entry(hdev->inputs.next, struct hid_input, list)->input;

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
	kfree(tmff2);
oom_err:
	return ret;
}

static __u8 *tmff2_report_fixup(struct hid_device *hdev, __u8 *rdesc,
		unsigned int *rsize)
{
	struct tmff2_device_entry *tmff2 = tmff2_from_hdev(hdev);

	if (!tmff2) /* not entirely sure what the best course of action would be here */
		return rdesc;

	if (tmff2->wheel_fixup)
		return tmff2->wheel_fixup(hdev, rdesc, rsize);

	return rdesc;
}

static void tmff2_remove(struct hid_device *hdev)
{
	struct tmff2_device_entry *tmff2 = tmff2_from_hdev(hdev);
	struct device *dev;

	if (!tmff2)
		return;

	tmff2->allow_scheduling = 0;
	cancel_delayed_work_sync(&tmff2->work);

	dev = &tmff2->hdev->dev;
	if (tmff2->params & PARAM_FRICTION_LEVEL)
		device_remove_file(dev, &dev_attr_friction_level);

	if (tmff2->params & PARAM_DAMPER_LEVEL)
		device_remove_file(dev, &dev_attr_damper_level);

	if (tmff2->params & PARAM_SPRING_LEVEL)
		device_remove_file(dev, &dev_attr_spring_level);

	if (tmff2->params & PARAM_RANGE)
		device_remove_file(dev, &dev_attr_range);

	if (tmff2->params & PARAM_ALT_MODE)
		device_remove_file(dev, &dev_attr_alternate_modes);

	if (tmff2->params & PARAM_GAIN)
		device_remove_file(dev, &dev_attr_gain);

	hid_hw_stop(hdev);
	tmff2->wheel_destroy(tmff2->data);

	kfree(tmff2->states);
	kfree(tmff2);
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

	{}
};
MODULE_DEVICE_TABLE(hid, tmff2_devices);

static struct hid_driver tmff2_driver = {
	.name = "tmff2",
	.id_table = tmff2_devices,
	.probe = tmff2_probe,
	.remove = tmff2_remove,
	.report_fixup = tmff2_report_fixup,
};
module_hid_driver(tmff2_driver);

MODULE_LICENSE("GPL");
