// SPDX-License-Identifier: GPL-2.0
#include <linux/usb.h>
#include <linux/hid.h>
#include "hid-tmff2.h"

#define T248_MAX_EFFECTS 16
#define T248_BUFFER_LENGTH 63

static const u8 setup_0[64] = { 0x42, 0x01 };
static const u8 setup_1[64] = { 0x0a, 0x04, 0x90, 0x03 };
static const u8 setup_2[64] = { 0x0a, 0x04, 0x00, 0x0c };
static const u8 setup_3[64] = { 0x0a, 0x04, 0x12, 0x10 };
static const u8 setup_4[64] = { 0x0a, 0x04, 0x00, 0x06 };
static const u8 setup_5[64] = { 0x0a, 0x04, 0x00, 0x0e };
static const u8 setup_6[64] = { 0x0a, 0x04, 0x00, 0x0e, 0x01 };
static const u8 *const setup_arr[] = { setup_0, setup_1, setup_2, setup_3, setup_4, setup_5, setup_6 };
static const unsigned int setup_arr_sizes[] = {
	ARRAY_SIZE(setup_0),
	ARRAY_SIZE(setup_1),
	ARRAY_SIZE(setup_2),
	ARRAY_SIZE(setup_3),
	ARRAY_SIZE(setup_4),
	ARRAY_SIZE(setup_5),
	ARRAY_SIZE(setup_6)
};

static const unsigned long t248_params =
	PARAM_SPRING_LEVEL
	| PARAM_DAMPER_LEVEL
	| PARAM_FRICTION_LEVEL
	| PARAM_RANGE
	;

static const signed short t248_effects[] = {
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

/* TODO: sort through this stuff */
static u8 t248_pc_rdesc_fixed[] = {
0x05, 0x01, 0x09, 0x04, 0xa1, 0x01, 0x09, 0x01, 0xa1, 0x00, 0x85, 0x07, 0x09, 0x30, 0x15, 0x00, 0x27, 0xff, 0xff, 0x00, 0x00, 0x35, 0x00, 0x47, 0xff, 0xff, 0x00, 0x00, 0x75, 0x10, 0x95, 0x01, 0x81, 0x02, 0x09, 0x31, 0x26, 0xff, 0x03, 0x46, 0xff, 0x03, 0x81, 0x02, 0x09, 0x35, 0x81, 0x02, 0x09, 0x36, 0x81, 0x02, 0x75, 0x08, 0x26, 0xff, 0x00, 0x46, 0xff, 0x00, 0x09, 0x40, 0x81, 0x02, 0x09, 0x41, 0x81, 0x02, 0x09, 0x33, 0x81, 0x02, 0x09, 0x34, 0x81, 0x02, 0x09, 0x32, 0x81, 0x02, 0x09, 0x37, 0x81, 0x02, 0x05, 0x09, 0x19, 0x01, 0x29, 0x1a, 0x25, 0x01, 0x45, 0x01, 0x75, 0x01, 0x95, 0x1a, 0x81, 0x02, 0x75, 0x06, 0x95, 0x01, 0x81, 0x03, 0x05, 0x01, 0x09, 0x39, 0x25, 0x07, 0x46, 0x3b, 0x01, 0x55, 0x00, 0x65, 0x14, 0x75, 0x04, 0x81, 0x42, 0x65, 0x00, 0x81, 0x03, 0x85, 0x0a, 0x06, 0x00, 0xff, 0x09, 0x0a, 0x75, 0x08, 0x95, 0x3f, 0x26, 0xff, 0x00, 0x46, 0xff, 0x00, 0x91, 0x02, 0x85, 0x02, 0x09, 0x02, 0x81, 0x02, 0x09, 0x14, 0x85, 0x14 0x81, 0x02, 0xc0, 0xc0
};

static int t248_interrupts(struct t300rs_device_entry *t248)
{
	u8 *send_buf = kmalloc(256, GFP_KERNEL);
	struct usb_interface *usbif = to_usb_interface(t248->hdev->dev.parent);
	struct usb_host_endpoint *ep;
	int ret, trans, b_ep, i;
	if (!send_buf) {
		hid_err(t248->hdev, "failed allocating send buffer\n");
		return -ENOMEM;
	}

	ep = &usbif->cur_altsetting->endpoint[1];
	b_ep = ep->desc.bEndpointAddress;

	for (i = 0; i < ARRAY_SIZE(setup_arr); ++i) {
		memcpy(send_buf, setup_arr[i], setup_arr_sizes[i]);

		ret = usb_interrupt_msg(t248->usbdev,
				usb_sndintpipe(t248->usbdev, b_ep),
				send_buf, setup_arr_sizes[i],
				&trans,
				USB_CTRL_SET_TIMEOUT);

		if (ret) {
			hid_err(t248->hdev, "setup data couldn't be sent\n");
			goto err;
		}
	}

err:
	kfree(send_buf);
	return ret;
}

int t248_wheel_init(struct tmff2_device_entry *tmff2)
{
	struct t300rs_device_entry *t248 = kzalloc(sizeof(struct t300rs_device_entry), GFP_KERNEL);
	struct list_head *report_list;
	int ret;

	if (!t248) {
		ret = -ENOMEM;
		goto t248_err;
	}

	t248->hdev = tmff2->hdev;
	t248->input_dev = tmff2->input_dev;
	t248->usbdev = to_usb_device(tmff2->hdev->dev.parent->parent);
	t248->buffer_length = T248_BUFFER_LENGTH;

	t248->send_buffer = kzalloc(t248->buffer_length, GFP_KERNEL);
	if (!t248->send_buffer) {
		ret = -ENOMEM;
		goto send_err;
	}

	report_list = &t248->hdev->report_enum[HID_OUTPUT_REPORT].report_list;
	t248->report = list_entry(report_list->next, struct hid_report, list);
	t248->ff_field = t248->report->field[0];

	t248->open = t248->input_dev->open;
	t248->close = t248->input_dev->close;

	if ((ret = t248_interrupts(t248)))
		goto interrupt_err;

	/* everything went OK */
	tmff2->data = t248;
	tmff2->params = t248_params;
	tmff2->max_effects = T248_MAX_EFFECTS;
	memcpy(tmff2->supported_effects, t248_effects, sizeof(t248_effects));

	hid_info(t248->hdev, "force feedback for T248\n");
	return 0;

interrupt_err:
send_err:
	kfree(t248);
t248_err:
	hid_err(tmff2->hdev, "failed initializing T248\n");
	return ret;
}

int t248_wheel_destroy(void *data)
{
	struct t300rs_device_entry *t300rs = data;
	if (!t300rs)
		return -ENODEV;

	kfree(t300rs->send_buffer);
	kfree(t300rs);
	return 0;
}

int t248_set_range(void *data, uint16_t value)
{
	struct t300rs_device_entry *t248 = data;
	if (value < 140) {
		hid_info(t248->hdev, "value %i too small, clamping to 140\n", value);
		value = 140;
	}

	if (value > 900) {
		hid_info(t248->hdev, "value %i too large, clamping to 900\n", value);
		value = 900;
	}

	return t300rs_set_range(data, value);
}

static int t248_open(void *data)
{
	struct t300rs_device_entry *t248 = data;
	if (!t248)
		return -ENODEV;

	/* TODO: send usb commands to actually open device */
	return t248->open(t248->input_dev);
}

static int t248_close(void *data)
{
	struct t300rs_device_entry *t248 = data;
	if (!t248)
		return -ENODEV;

	/* TODO: send usb commands to actually close device */
	t248->close(t248->input_dev);
	return 0;
}

int t248_populate_api(struct tmff2_device_entry *tmff2)
{
	tmff2->play_effect = t300rs_play_effect;
	tmff2->upload_effect = t300rs_upload_effect;
	tmff2->update_effect = t300rs_update_effect;
	tmff2->stop_effect = t300rs_stop_effect;

	tmff2->wheel_init = t248_wheel_init;
	tmff2->wheel_destroy = t248_wheel_destroy;

	tmff2->open = t248_open;
	tmff2->close = t248_close;
	tmff2->set_gain = t300rs_set_gain;
	/* T248 only has 900 degree range, instead of T300RS 1080 */
	tmff2->set_range = t248_set_range;
	tmff2->set_autocenter = t300rs_set_autocenter;

	return 0;
}
