// SPDX-License-Identifier: GPL-2.0-or-later
#include <linux/usb.h>
#include <linux/hid.h>
#include "../hid-tmff2.h"
#include "../tmt300rs/hid-tmt300rs.h"

#define T598_MAX_EFFECTS 16
#define T598_BUFFER_LENGTH 63

static const unsigned long t598_params =
	PARAM_SPRING_LEVEL
	| PARAM_DAMPER_LEVEL
	| PARAM_FRICTION_LEVEL
	| PARAM_RANGE
	| PARAM_GAIN
	;

static const signed short t598_effects[] = {
	FF_CONSTANT,
	FF_RAMP,
	FF_SPRING,
	FF_DAMPER,
	FF_FRICTION,
	FF_INERTIA,
	FF_PERIODIC,
	FF_SINE,
	FF_TRIANGLE,
	FF_SQUARE,
	FF_SAW_UP,
	FF_SAW_DOWN,
	FF_AUTOCENTER,
	FF_GAIN,
	-1
};

static u8 t598_pc_rdesc_fixed[] = {
	0x05, 0x01,
	0x09, 0x04,
	0xa1, 0x01,
	0x09, 0x01,
	0xa1, 0x00,
	0x85, 0x07,
	0x09, 0x30,
	0x15, 0x00,
	0x27, 0xff, 0xff, 0x00, 0x00,
	0x35, 0x00,
	0x47, 0xff, 0xff, 0x00, 0x00,
	0x75, 0x10,
	0x95, 0x01,
	0x81, 0x02,
	0x09, 0x31,
	0x26, 0xff, 0x03,
	0x46, 0xff, 0x03,
	0x81, 0x02,
	0x09, 0x35,
	0x81, 0x02,
	0x09, 0x36,
	0x81, 0x02,
	0x75, 0x08,
	0x26, 0xff, 0x00,
	0x46, 0xff, 0x00,
	0x09, 0x40,
	0x81, 0x02,
	0x09, 0x41,
	0x81, 0x02,
	0x09, 0x33,
	0x81, 0x02,
	0x09, 0x34,
	0x81, 0x02,
	0x09, 0x32,
	0x81, 0x02,
	0x09, 0x37,
	0x81, 0x02,
	0x05, 0x09,
	0x19, 0x01,
	0x29, 0x1a,
	0x25, 0x01,
	0x45, 0x01,
	0x75, 0x01,
	0x95, 0x1a,
	0x81, 0x02,
	0x75, 0x06,
	0x95, 0x01,
	0x81, 0x03,
	0x05, 0x01,
	0x09, 0x39,
	0x25, 0x07,
	0x46, 0x3b, 0x01,
	0x55, 0x00,
	0x65, 0x14,
	0x75, 0x04,
	0x81, 0x42,
	0x65, 0x00,
	0x81, 0x03,
	0x85, 0x60,
	0x06, 0x00, 0xff,
	0x09, 0x60,
	0x75, 0x08,
	0x95, 0x3f,
	0x26, 0xff, 0x00,
	0x46, 0xff, 0x00,
	0x91, 0x02,
	0x85, 0x02,
	0x09, 0x02,
	0x81, 0x02,
	0x09, 0x14,
	0x85, 0x14,
	0x81, 0x02,
	0xc0,
	0xc0,
};

static int t598_wheel_destroy(void *data)
{
	struct t300rs_device_entry *t598 = data;

	if (!t598)
		return -ENODEV;

	kfree(t598->send_buffer);
	kfree(t598);
	return 0;
}

static int t598_set_range(void *data, uint16_t value)
{
	struct t300rs_device_entry *t598 = data;

	if (value < 140) {
		hid_info(t598->hdev, "value %i too small, clamping to 140\n", value);
		value = 140;
	}

	if (value > 1080) {
		hid_info(t598->hdev, "value %i too large, clamping to 1080\n", value);
		value = 1080;
	}

	return t300rs_set_range(data, value);
}

static int t598_send_open(struct t300rs_device_entry *t598)
{
	int r1, r2;

	t598->send_buffer[0] = 0x01;
	t598->send_buffer[1] = 0x04;
	if ((r1 = t300rs_send_int(t598)))
		return r1;

	t598->send_buffer[0] = 0x01;
	t598->send_buffer[1] = 0x05;
	if ((r2 = t300rs_send_int(t598)))
		return r2;

	return 0;
}

static int t598_open(void *data, int open_mode)
{
	struct t300rs_device_entry *t598 = data;

	if (!t598)
		return -ENODEV;

	if (open_mode)
		t598_send_open(t598);

	return t598->open(t598->input_dev);
}

static int t598_send_close(struct t300rs_device_entry *t598)
{
	int r1, r2;

	t598->send_buffer[0] = 0x01;
	t598->send_buffer[1] = 0x05;
	if ((r1 = t300rs_send_int(t598)))
		return r1;

	t598->send_buffer[0] = 0x01;
	t598->send_buffer[1] = 0x00;
	if ((r2 = t300rs_send_int(t598)))
		return r2;

	return 0;
}

static int t598_close(void *data, int open_mode)
{
	struct t300rs_device_entry *t598 = data;

	if (!t598)
		return -ENODEV;

	if (open_mode)
		t598_send_close(t598);

	t598->close(t598->input_dev);
	return 0;
}

static int t598_wheel_init(struct tmff2_device_entry *tmff2, int open_mode)
{
	struct t300rs_device_entry *t598 = kzalloc(sizeof(struct t300rs_device_entry),
						    GFP_KERNEL);
	struct list_head *report_list;
	int ret;

	if (!t598) {
		ret = -ENOMEM;
		goto t598_err;
	}

	t598->hdev = tmff2->hdev;
	t598->input_dev = tmff2->input_dev;
	t598->usbdev = to_usb_device(tmff2->hdev->dev.parent->parent);
	t598->buffer_length = T598_BUFFER_LENGTH;

	t598->send_buffer = kzalloc(t598->buffer_length, GFP_KERNEL);
	if (!t598->send_buffer) {
		ret = -ENOMEM;
		goto send_err;
	}

	report_list = &t598->hdev->report_enum[HID_OUTPUT_REPORT].report_list;
	t598->report = list_entry(report_list->next, struct hid_report, list);
	t598->ff_field = t598->report->field[0];

	t598->open = t598->input_dev->open;
	t598->close = t598->input_dev->close;

	/* No USB interrupt initialisation needed for T598 */

	tmff2->data = t598;
	tmff2->params = t598_params;
	tmff2->max_effects = T598_MAX_EFFECTS;
	memcpy(tmff2->supported_effects, t598_effects, sizeof(t598_effects));

	if (!open_mode)
		t598_send_open(t598);

	hid_info(t598->hdev, "force feedback for T598\n");
	return 0;

send_err:
	kfree(t598);
t598_err:
	hid_err(tmff2->hdev, "failed initializing T598\n");
	return ret;
}

static __u8 *t598_wheel_fixup(struct hid_device *hdev, __u8 *rdesc,
		unsigned int *rsize)
{
	rdesc = t598_pc_rdesc_fixed;
	*rsize = sizeof(t598_pc_rdesc_fixed);
	return rdesc;
}

int t598_populate_api(struct tmff2_device_entry *tmff2)
{
	tmff2->play_effect = t300rs_play_effect;
	tmff2->upload_effect = t300rs_upload_effect;
	tmff2->update_effect = t300rs_update_effect;
	tmff2->stop_effect = t300rs_stop_effect;

	tmff2->set_gain = t300rs_set_gain;
	tmff2->set_autocenter = t300rs_set_autocenter;
	tmff2->set_range = t598_set_range;
	tmff2->wheel_fixup = t598_wheel_fixup;

	tmff2->open = t598_open;
	tmff2->close = t598_close;

	tmff2->wheel_init = t598_wheel_init;
	tmff2->wheel_destroy = t598_wheel_destroy;

	return 0;
}
