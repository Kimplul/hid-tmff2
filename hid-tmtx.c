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

static const unsigned long tx_params =
	PARAM_SPRING_LEVEL
	| PARAM_DAMPER_LEVEL
	| PARAM_FRICTION_LEVEL
	| PARAM_RANGE
	| PARAM_GAIN
	;

static const signed short tx_effects[] = {
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
static u8 tx_pc_rdesc_fixed[] = {
	0x05, 0x01, /* Usage page (Generic Desktop) */
	0x09, 0x04, /* Usage (Joystick) */
	0xa1, 0x01, /* Collection (Application) */
	0x09, 0x01, /* Usage (Pointer) */
	0xa1, 0x00, /* Collection (Physical) */
	0x85, 0x07, /* Report ID (7) */
	0x09, 0x30, /* Usage (X) */
	0x15, 0x00, /* Logical minimum (0) */
	0x27, 0xff, 0xff, 0x00, 0x00, /* Logical maximum (65535) */
	0x35, 0x00, /* Physical minimum (0) */
	0x47, 0xff, 0xff, 0x00, 0x00, /* Physical maximum (65535) */
	0x75, 0x10, /* Report size (16) */
	0x95, 0x01, /* Report count (1) */
	0x81, 0x02, /* Input (Variable, Absolute) */
	0x09, 0x35, /* Usage (Rz) (Brake) */
	0x26, 0xff, 0x03, /* Logical maximum (1023) */
	0x46, 0xff, 0x03, /* Physical maximum (1023) */
	0x81, 0x02, /* Input (Variable, Absolute) */
	0x09, 0x32, /* Usage (Z) (Gas) */
	0x81, 0x02, /* Input (Variable, Absolute) */
	0x09, 0x31, /* Usage (Y) (Clutch) */
	0x81, 0x02, /* Input (Variable, Absolute) */
	0x81, 0x03, /* Input (Variable, Absolute, Constant) */
	0x05, 0x09, /* Usage page (Button) */
	0x19, 0x01, /* Usage minimum (1) */
	0x29, 0x0d, /* Usage maximum (13) */
	0x25, 0x01, /* Logical maximum (1) */
	0x45, 0x01, /* Physical maximum (1) */
	0x75, 0x01, /* Report size (1) */
	0x95, 0x0d, /* Report count (13) */
	0x81, 0x02, /* Input (Variable, Absolute) */
	0x75, 0x0b, /* Report size (13) */
	0x95, 0x01, /* Report count (1) */
	0x81, 0x03, /* Usage (Variable, Absolute, Constant) */
	0x05, 0x01, /* Usage page (Generic Desktop) */
	0x09, 0x39, /* Usage (Hat Switch) */
	0x25, 0x07, /* Logical maximum (7) */
	0x46, 0x3b, 0x01, /* Physical maximum (315) */
	0x55, 0x00, /* Unit exponent (0) */
	0x65, 0x14, /* Unit (Eng Rot, Angular Pos) */
	0x75, 0x04, /* Report size (4) */
	0x81, 0x42, /* Input (Variable, Absolute, NullState) */
	0x65, 0x00, /* Unit (None) */
	0x81, 0x03, /* Input (Variable, Absolute, Constant) */
	0x85, 0x60, /* Report ID (96), prev 10 */
	0x06, 0x00, 0xff, /* Usage page (Vendor 1) */
	0x09, 0x60, /* Usage (96), prev 10 */
	0x75, 0x08, /* Report size (8) */
	0x95, 0x3f, /* Report count (63) */
	0x26, 0xff, 0x7f, /* Logical maximum (32767) */
	0x15, 0x00, /* Logical minimum (0) */
	0x46, 0xff, 0x7f, /* Physical maximum (32767) */
	0x36, 0x00, 0x80, /* Physical minimum (-32768) */
	0x91, 0x02, /* Output (Variable, Absolute) */
	0x85, 0x02, /* Report ID (2) */
	0x09, 0x02, /* Usage (2) */
	0x81, 0x02, /* Input (Variable, Absolute) */
	0x09, 0x14, /* Usage (20) */
	0x85, 0x14, /* Report ID (20) */
	0x81, 0x02, /* Input (Variable, Absolute) */
	0xc0, /* End collection */
	0xc0, /* End collection */
};

static int tx_interrupts(struct t300rs_device_entry *tx)
{
	u8 *send_buf = kmalloc(256, GFP_KERNEL);
	struct usb_interface *usbif = to_usb_interface(tx->hdev->dev.parent);
	struct usb_host_endpoint *ep;
	int ret, trans, b_ep, i;

	if (!send_buf) {
		hid_err(tx->hdev, "failed allocating send buffer\n");
		return -ENOMEM;
	}

	ep = &usbif->cur_altsetting->endpoint[1];
	b_ep = ep->desc.bEndpointAddress;

	for (i = 0; i < ARRAY_SIZE(setup_arr); ++i) {
		memcpy(send_buf, setup_arr[i], setup_arr_sizes[i]);

		ret = usb_interrupt_msg(tx->usbdev,
				usb_sndintpipe(tx->usbdev, b_ep),
				send_buf, setup_arr_sizes[i],
				&trans,
				USB_CTRL_SET_TIMEOUT);

		if (ret) {
			hid_err(tx->hdev, "setup data couldn't be sent\n");
			goto err;
		}
	}

err:
	kfree(send_buf);
	return ret;
}

int tx_wheel_destroy(void *data)
{
	struct t300rs_device_entry *t300rs = data;

	if (!t300rs)
		return -ENODEV;

	kfree(t300rs->send_buffer);
	kfree(t300rs);
	return 0;
}

int tx_set_range(void *data, uint16_t value)
{
	struct t300rs_device_entry *tx = data;

	if (value < 140) {
		hid_info(tx->hdev, "value %i too small, clamping to 140\n", value);
		value = 140;
	}

	if (value > 900) {
		hid_info(tx->hdev, "value %i too large, clamping to 900\n", value);
		value = 900;
	}

	return t300rs_set_range(data, value);
}

static int tx_send_open(struct t300rs_device_entry *tx)
{
	int r1, r2;
	tx->send_buffer[0] = 0x01;
	tx->send_buffer[1] = 0x04;
	if ((r1 = t300rs_send_int(tx)))
		return r1;

	tx->send_buffer[0] = 0x01;
	tx->send_buffer[1] = 0x05;
	if ((r2 = t300rs_send_int(tx)))
		return r2;

	return 0;
}

static int tx_open(void *data, int open_mode)
{
	struct t300rs_device_entry *tx = data;

	if (!tx)
		return -ENODEV;

	if (open_mode)
		tx_send_open(tx);

	return tx->open(tx->input_dev);
}

static int tx_send_close(struct t300rs_device_entry *tx)
{
	int r1, r2;
	tx->send_buffer[0] = 0x01;
	tx->send_buffer[1] = 0x05;
	if ((r1 = t300rs_send_int(tx)))
		return r1;

	tx->send_buffer[0] = 0x01;
	tx->send_buffer[1] = 0x00;
	if ((r2 = t300rs_send_int(tx)))
		return r2;

	return 0;
}

static int tx_close(void *data, int open_mode)
{
	struct t300rs_device_entry *tx = data;

	if (!tx)
		return -ENODEV;

	if (open_mode)
		tx_send_close(tx);

	tx->close(tx->input_dev);
	return 0;
}

int tx_wheel_init(struct tmff2_device_entry *tmff2, int open_mode)
{
	struct t300rs_device_entry *tx = kzalloc(sizeof(struct t300rs_device_entry), GFP_KERNEL);
	struct list_head *report_list;
	int ret;


	if (!tx) {
		ret = -ENOMEM;
		goto tx_err;
	}

	tx->hdev = tmff2->hdev;
	tx->input_dev = tmff2->input_dev;
	tx->usbdev = to_usb_device(tmff2->hdev->dev.parent->parent);
	tx->buffer_length = T248_BUFFER_LENGTH;

	tx->send_buffer = kzalloc(tx->buffer_length, GFP_KERNEL);
	if (!tx->send_buffer) {
		ret = -ENOMEM;
		goto send_err;
	}

	report_list = &tx->hdev->report_enum[HID_OUTPUT_REPORT].report_list;
	tx->report = list_entry(report_list->next, struct hid_report, list);
	tx->ff_field = tx->report->field[0];

	tx->open = tx->input_dev->open;
	tx->close = tx->input_dev->close;

	if ((ret = tx_interrupts(tx)))
		goto interrupt_err;

	/* everything went OK */
	tmff2->data = tx;
	tmff2->params = tx_params;
	tmff2->max_effects = T248_MAX_EFFECTS;
	memcpy(tmff2->supported_effects, tx_effects, sizeof(tx_effects));

	if (!open_mode)
		tx_send_open(tx);

	hid_info(tx->hdev, "force feedback for TX\n");
	return 0;

interrupt_err:
send_err:
	kfree(tx);
tx_err:
	hid_err(tmff2->hdev, "failed initializing TX\n");
	return ret;
}

static __u8 *tx_wheel_fixup(struct hid_device *hdev, __u8 *rdesc,
		unsigned int *rsize)
{
	rdesc = tx_pc_rdesc_fixed;
	*rsize = sizeof(tx_pc_rdesc_fixed);
	return rdesc;
}

int tx_populate_api(struct tmff2_device_entry *tmff2)
{
	tmff2->play_effect = t300rs_play_effect;
	tmff2->upload_effect = t300rs_upload_effect;
	tmff2->update_effect = t300rs_update_effect;
	tmff2->stop_effect = t300rs_stop_effect;

	tmff2->set_gain = t300rs_set_gain;
	tmff2->set_autocenter = t300rs_set_autocenter;
	/* TX only has 900 degree range, instead of T300RS 1080 */
	tmff2->set_range = tx_set_range;
	tmff2->wheel_fixup = tx_wheel_fixup;

	tmff2->open = tx_open;
	tmff2->close = tx_close;

	tmff2->wheel_init = tx_wheel_init;
	tmff2->wheel_destroy = tx_wheel_destroy;

	return 0;
}
