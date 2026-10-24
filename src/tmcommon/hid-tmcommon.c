// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Shared backend for the wheels that speak the T300RS protocol but need the
 * T248 style setup sequence. The individual wheels only provide a struct
 * tm_wheel_desc with their report descriptor and limits.
 */
#include <linux/usb.h>
#include <linux/hid.h>
#include "../hid-tmff2.h"

#define TM_COMMON_MAX_EFFECTS 16
#define TM_COMMON_BUFFER_LENGTH 63

/* every setup packet is sent as a full 64 byte packet, the remaining bytes
 * are zero */
#define TM_COMMON_SETUP_PACKET_LENGTH 64

static const u8 setup_arr[][TM_COMMON_SETUP_PACKET_LENGTH] = {
	{ 0x42, 0x01 },
	{ 0x0a, 0x04, 0x90, 0x03 },
	{ 0x0a, 0x04, 0x00, 0x0c },
	{ 0x0a, 0x04, 0x12, 0x10 },
	{ 0x0a, 0x04, 0x00, 0x06 },
	{ 0x0a, 0x04, 0x00, 0x0e },
	{ 0x0a, 0x04, 0x00, 0x0e, 0x01 },
};

static const signed short tm_common_effects[] = {
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

static int tm_common_interrupts(struct t300rs_device_entry *wheel)
{
	struct usb_interface *usbif = to_usb_interface(wheel->hdev->dev.parent);
	struct usb_host_interface *alt = usbif->cur_altsetting;
	struct usb_endpoint_descriptor *ep;
	u8 *send_buf;
	int ret = 0, trans, b_ep, i;

	/* the setup data goes to the second endpoint of the interface, don't
	 * trust the device to actually have one */
	if (alt->desc.bNumEndpoints < 2) {
		hid_err(wheel->hdev, "unexpected number of endpoints: %u\n",
				alt->desc.bNumEndpoints);
		return -ENODEV;
	}

	ep = &alt->endpoint[1].desc;
	if (!usb_endpoint_dir_out(ep)) {
		hid_err(wheel->hdev, "setup endpoint is not an OUT endpoint\n");
		return -ENODEV;
	}
	b_ep = ep->bEndpointAddress;

	/* usb transfers need a heap buffer */
	send_buf = kmalloc(TM_COMMON_SETUP_PACKET_LENGTH, GFP_KERNEL);
	if (!send_buf)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(setup_arr); ++i) {
		memcpy(send_buf, setup_arr[i], TM_COMMON_SETUP_PACKET_LENGTH);

		ret = usb_interrupt_msg(wheel->usbdev,
				usb_sndintpipe(wheel->usbdev, b_ep),
				send_buf, TM_COMMON_SETUP_PACKET_LENGTH,
				&trans,
				USB_CTRL_SET_TIMEOUT);

		if (ret) {
			hid_err(wheel->hdev, "setup data couldn't be sent: %i\n", ret);
			break;
		}
	}

	kfree(send_buf);
	return ret;
}

static int tm_common_wheel_destroy(void *data)
{
	struct t300rs_device_entry *wheel = data;

	if (!wheel)
		return -ENODEV;

	kfree(wheel->send_buffer);
	kfree(wheel);
	return 0;
}

static int tm_common_set_range(void *data, uint16_t value)
{
	struct t300rs_device_entry *wheel = data;
	const struct tm_wheel_desc *desc = wheel->desc;

	if (value < desc->range_min) {
		hid_info(wheel->hdev, "value %i too small, clamping to %i\n",
				value, desc->range_min);
		value = desc->range_min;
	}

	if (value > desc->range_max) {
		hid_info(wheel->hdev, "value %i too large, clamping to %i\n",
				value, desc->range_max);
		value = desc->range_max;
	}

	return t300rs_set_range(data, value);
}

static int tm_common_send_open(struct t300rs_device_entry *wheel)
{
	static const u8 open_0[] = { 0x01, 0x04 };
	static const u8 open_1[] = { 0x01, 0x05 };
	int ret;

	if ((ret = t300rs_send_buf(wheel, open_0, sizeof(open_0))))
		return ret;

	return t300rs_send_buf(wheel, open_1, sizeof(open_1));
}

static int tm_common_open(void *data, int open_mode)
{
	struct t300rs_device_entry *wheel = data;

	if (!wheel)
		return -ENODEV;

	if (open_mode && tm_common_send_open(wheel))
		hid_warn(wheel->hdev, "failed sending open command\n");

	return 0;
}

static int tm_common_send_close(struct t300rs_device_entry *wheel)
{
	static const u8 close_0[] = { 0x01, 0x05 };
	static const u8 close_1[] = { 0x01, 0x00 };
	int ret;

	if ((ret = t300rs_send_buf(wheel, close_0, sizeof(close_0))))
		return ret;

	return t300rs_send_buf(wheel, close_1, sizeof(close_1));
}

static int tm_common_close(void *data, int open_mode)
{
	struct t300rs_device_entry *wheel = data;

	if (!wheel)
		return -ENODEV;

	if (open_mode && tm_common_send_close(wheel))
		hid_warn(wheel->hdev, "failed sending close command\n");

	return 0;
}

static int tm_common_wheel_init(struct tmff2_device_entry *tmff2, int open_mode)
{
	const struct tm_wheel_desc *desc = tmff2->wheel_desc;
	struct t300rs_device_entry *wheel = kzalloc(sizeof(struct t300rs_device_entry),
						   GFP_KERNEL);
	int ret;

	if (!wheel) {
		ret = -ENOMEM;
		goto wheel_err;
	}

	wheel->desc = desc;

	if ((ret = t300rs_init_common(tmff2, wheel, TM_COMMON_BUFFER_LENGTH)))
		goto init_err;

	if ((ret = tm_common_interrupts(wheel)))
		goto init_err;

	/* everything went OK */
	tmff2->data = wheel;
	tmff2->params = desc->params;
	tmff2->max_effects = TM_COMMON_MAX_EFFECTS;
	memcpy(tmff2->supported_effects, tm_common_effects, sizeof(tm_common_effects));

	if (!open_mode)
		tm_common_send_open(wheel);

	hid_info(wheel->hdev, "force feedback for %s\n", desc->name);
	return 0;

init_err:
	kfree(wheel->send_buffer);
	kfree(wheel);
wheel_err:
	hid_err(tmff2->hdev, "failed initializing %s\n", desc->name);
	return ret;
}

static const __u8 *tm_common_wheel_fixup(struct hid_device *hdev, __u8 *rdesc,
		unsigned int *rsize)
{
	/* the driver data is set before hid_parse() calls the fixup */
	struct tmff2_device_entry *tmff2 = hid_get_drvdata(hdev);

	if (!tmff2 || !tmff2->wheel_desc)
		return rdesc;

	*rsize = tmff2->wheel_desc->rsize;
	return tmff2->wheel_desc->rdesc;
}

int tm_common_populate_api(struct tmff2_device_entry *tmff2,
		const struct tm_wheel_desc *desc)
{
	tmff2->wheel_desc = desc;

	tmff2->play_effect = t300rs_play_effect;
	tmff2->upload_effect = t300rs_upload_effect;
	tmff2->update_effect = t300rs_update_effect;
	tmff2->stop_effect = t300rs_stop_effect;

	tmff2->set_gain = t300rs_set_gain;
	tmff2->set_autocenter = t300rs_set_autocenter;
	tmff2->set_range = tm_common_set_range;
	tmff2->wheel_fixup = tm_common_wheel_fixup;

	tmff2->open = tm_common_open;
	tmff2->close = tm_common_close;

	tmff2->wheel_init = tm_common_wheel_init;
	tmff2->wheel_destroy = tm_common_wheel_destroy;

	tmff2->alt_mode_store = desc->alt_mode_store;

	return 0;
}
