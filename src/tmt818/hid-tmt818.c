// SPDX-License-Identifier: GPL-2.0-or-later
#include <linux/usb.h>
#include <linux/hid.h>
#include "../hid-tmff2.h"
#include <linux/delay.h>

#define t818_MAX_EFFECTS 16
#define t818_BUFFER_LENGTH 63

static const u8 setup_0[64] = {0x0a, 0x04, 0x00, 0x20};
static const u8 setup_1[64] = {0x0a, 0x04, 0x00, 0x0c};
static const u8 setup_2[64] = {0x0a, 0x04, 0x12, 0x10};
static const u8 setup_3[64] = {0x0a, 0x04, 0x00, 0x16};
static const u8 setup_4[64] = {0x0a, 0x04, 0x00, 0x17, 0x01};
static const u8 setup_5[64] = {0x0a, 0x04, 0x00, 0x2a, 0x01};
static const u8 setup_6[64] = {0x0a, 0x04, 0x00, 0x2f, 0x01};
static const u8 setup_7[64] = {0x0a, 0x04, 0x03};
static const u8 setup_8[64] = {0x0a, 0x04, 0x00, 0x2f, 0x00, 0xcd, 0xcc, 0x4c, 0x3d}; // Set Linearity to 5%
static const u8 setup_9[64] = {0x0a, 0x04, 0x00, 0x17, 0x00, 0x0a, 0xd7, 0xa3, 0x3c}; // Set Linearity to 5%
static const u8 *const setup_arr[] = {setup_0, setup_1, setup_2, setup_3, setup_4, setup_5, setup_6, setup_7};
static const unsigned int setup_arr_sizes[] = {
	ARRAY_SIZE(setup_0),
	ARRAY_SIZE(setup_1),
	ARRAY_SIZE(setup_2),
	ARRAY_SIZE(setup_3),
	ARRAY_SIZE(setup_4),
	ARRAY_SIZE(setup_5),
	ARRAY_SIZE(setup_6),
	ARRAY_SIZE(setup_7),
	ARRAY_SIZE(setup_8),
	ARRAY_SIZE(setup_9)
	};

static const u8 init_0[64] = {0x49, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00};

static const unsigned long t818_params =
	PARAM_SPRING_LEVEL | PARAM_DAMPER_LEVEL | PARAM_FRICTION_LEVEL | PARAM_RANGE | PARAM_GAIN | PARAM_MODE | PARAM_COLOR;

static const signed short t818_effects[] = {
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
	-1};

/* TODO: sort through this stuff */
static u8 t818_pc_rdesc_fixed[] = {
	0x05, 0x01,					  /* Usage page (Generic Desktop) */
	0x09, 0x04,					  /* Usage (Joystick) */
	0xa1, 0x01,					  /* Collection (Application) */
	0x09, 0x01,					  /* Usage (Pointer) */
	0xa1, 0x00,					  /* Collection (Physical) */
	0x85, 0x07,					  /* Report ID (7) */
	0x09, 0x30,					  /* Usage (X) */
	0x15, 0x00,					  /* Logical minimum (0) */
	0x27, 0xff, 0xff, 0x00, 0x00, /* Logical maximum (65535) */
	0x35, 0x00,					  /* Physical minimum (0) */
	0x47, 0xff, 0xff, 0x00, 0x00, /* Physical maximum (65535) */
	0x75, 0x10,					  /* Report size (16) */
	0x95, 0x01,					  /* Report count (1) */
	0x81, 0x02,					  /* Input (Variable, Absolute) */
	0x09, 0x31,					  /* Usage (Y) TODO: clutch? */
	0x26, 0xff, 0x03,			  /* Logical maximum (1023) */
	0x46, 0xff, 0x03,			  /* Physical maximum (1023) */
	0x81, 0x02,					  /* Input (Variable, Absolute) */
	0x09, 0x35,					  /* Usage (Rz) TODO: brake? */
	0x81, 0x02,					  /* Input (Variable, Absolute) */
	0x09, 0x36,					  /* Usage (Slider) */
	0x81, 0x02,					  /* Input (Variable, Absolute) */
	0x75, 0x08,					  /* Report size (8) */
	0x26, 0xff, 0x00,			  /* Logical maximum (255) */
	0x46, 0xff, 0x00,			  /* Physical maximum (255) */
	0x09, 0x40,					  /* Usage (Vx) TODO: what is this? */
	0x81, 0x02,					  /* Input (Variable, Absolute) */
	0x09, 0x41,					  /* Usage (Vy) TODO: --||-- */
	0x81, 0x02,					  /* Input (Variable, Absolute) */
	0x09, 0x33,					  /* Usage (Rx) TODO: --||-- */
	0x81, 0x02,					  /* Input (Variable, Absolute) */
	0x09, 0x34,					  /* Usage (Ry) TODO: --||-- */
	0x81, 0x02,					  /* Input (Variable, Absolute) */
	0x09, 0x32,					  /* Usage (Z) TODO: --||-- (gas?) */
	0x81, 0x02,					  /* Input (Variable, Absolute) */
	0x09, 0x37,					  /* Usage (Dial) */
	0x81, 0x02,					  /* Input (Variable, Absolute) */
	0x05, 0x09,					  /* Usage page (Button) */
	0x19, 0x01,					  /* Usage minimum (1) */
	0x29, 0x1a,					  /* Usage maximum (13) */
	0x25, 0x01,					  /* Logical maximum (1) */
	0x45, 0x01,					  /* Physical maximum (1) */
	0x75, 0x01,					  /* Report size (1) */
	0x95, 0x1a,					  /* Report count (26) */
	0x81, 0x02,					  /* Input (Variable, Absolute) */
	0x75, 0x06,					  /* Report size (6) */
	0x95, 0x01,					  /* Report count (1) */
	0x81, 0x03,					  /* Usage (Variable, Absolute, Constant) */
	0x05, 0x01,					  /* Usage page (Generic Desktop) */
	0x09, 0x39,					  /* Usage (Hat Switch) */
	0x25, 0x07,					  /* Logical maximum (7) */
	0x46, 0x3b, 0x01,			  /* Physical maximum (315) */
	0x55, 0x00,					  /* Unit exponent (0) */
	0x65, 0x14,					  /* Unit (Eng rot, Angular Pos) */
	0x75, 0x04,					  /* Report size (4) */
	0x81, 0x42,					  /* Input (Variable, Absolute, NullState) */
	0x65, 0x00,					  /* Input (None) */
	0x81, 0x03,					  /* Input (Variable, Absolute, Constant) */
	0x85, 0x60,					  /* Report ID (96), prev 10 */
	0x06, 0x00, 0xff,			  /* Usage page (Vendor 1) */
	0x09, 0x60,					  /* Usage (96), prev 10 */
	0x75, 0x08,					  /* Report size (8) */
	0x95, 0x3f,					  /* Report count (63) */
	0x26, 0xff, 0x00,			  /* Logical maximum (256) */
	0x46, 0xff, 0x00,			  /* Physical maximum (256) */
	0x91, 0x02,					  /* Output (Variable, Absolute) */
	0x85, 0x02,					  /* Report ID (2) */
	0x09, 0x02,					  /* Usage (2) */
	0x81, 0x02,					  /* Input (Variable, Absolute) */
	0x09, 0x14,					  /* Usage (20) */
	0x85, 0x14,					  /* Report ID (20) */
	0x81, 0x02,					  /* Input (Variable, Absolute) */
	0xc0,						  /* End collection */
	0xc0,						  /* End collection */
};

static int t818_interrupts(struct t300rs_device_entry *t818)
{
	u8 *send_buf = kmalloc(256, GFP_KERNEL);
	struct usb_interface *usbif = to_usb_interface(t818->hdev->dev.parent);
	struct usb_host_endpoint *ep;
	int ret, trans, b_ep, i;
	if (!send_buf)
	{
		hid_err(t818->hdev, "failed allocating send buffer\n");
		return -ENOMEM;
	}

	ep = &usbif->cur_altsetting->endpoint[1];
	b_ep = ep->desc.bEndpointAddress;

	for (i = 0; i < ARRAY_SIZE(setup_arr); ++i)
	{
		memcpy(send_buf, setup_arr[i], setup_arr_sizes[i]);

		ret = usb_interrupt_msg(t818->usbdev,
								usb_sndintpipe(t818->usbdev, b_ep),
								send_buf, setup_arr_sizes[i],
								&trans,
								USB_CTRL_SET_TIMEOUT);

		if (ret)
		{
			hid_err(t818->hdev, "setup data couldn't be sent\n");
			goto err;
		}
	}

err:
	kfree(send_buf);
	return ret;
}

struct __packed t818_fw_response
{
	uint8_t unused1[16];
};

// Request 0x56 points to Microsoft Extended Properties Feature Descriptor
struct __packed t818_ext_props_response
{
	uint8_t unused1[4];
};

struct __packed t818_short_response
{
	uint8_t unused1[3];
};

struct __packed t818_twobyte_response
{
	uint8_t unused1[2];
};


struct __packed t818_long_response
{
	uint8_t unused1[0x40];
};


static int t818_controls(struct t300rs_device_entry *t818)
{
	int ret, trans, b_ep, i;
	struct t818_fw_response *first_response = kzalloc(sizeof(struct t818_fw_response), GFP_KERNEL);
	if (!first_response)
	{
		hid_err(t818->hdev, "could not allocate fw_response\n");
		return -ENOMEM;
	}

	// Wait for wheel to calibrate
	do
	{
		msleep(500);
		ret = usb_control_msg_recv(t818->usbdev, 0, 0x49,
								   0xc1, 0, 0,
								   first_response, 16, USB_CTRL_SET_TIMEOUT,
								   GFP_KERNEL);
	} while (first_response->unused1[2] == 0xFF);

	// Response somehow identifies the wheel
	// Indices 3,6,14 differentiate between attached wheel
	// Round Wheel + Adapter: 0x01, 0x06, 0x00
	// GT3 Wheel			  0x07, 0x0a, 0x53

	// Since I have no T248 to compare, I suspect indices 2, 4, 7, 8, 9, 12, 13 somehow show it's a T818
	// Values [0x03, 0x01, 0x0c, 0xb5, 0x0c, 0x0c, 0x0b]

	struct t818_ext_props_response *ext_props_response = kzalloc(sizeof(struct t818_ext_props_response), GFP_KERNEL);
	if (!ext_props_response)
	{
		hid_err(t818->hdev, "could not allocate fw_response\n");
		return -ENOMEM;
	}

	ret = usb_control_msg_recv(t818->usbdev, 0, 0x56,
							   0xc1, 0, 0,
							   ext_props_response, 4, USB_CTRL_SET_TIMEOUT,
							   GFP_KERNEL);

	ret = usb_control_msg_recv(t818->usbdev, 0, 0x55,
							   0xc1, 0, 0,
							   first_response, 16, USB_CTRL_SET_TIMEOUT,
							   GFP_KERNEL);

	struct t818_long_response *long_response = kzalloc(sizeof(struct t818_long_response), GFP_KERNEL);
	if (!long_response)
	{
		hid_err(t818->hdev, "could not allocate fw_response\n");
		return -ENOMEM;
	}

	ret = usb_control_msg_recv(t818->usbdev, 0, 0x48,
							   0xc1, 0, 0,
							   long_response, 0x40, USB_CTRL_SET_TIMEOUT,
							   GFP_KERNEL);

	struct t818_short_response *short_response = kzalloc(sizeof(struct t818_short_response), GFP_KERNEL);
	if (!short_response)
	{
		hid_err(t818->hdev, "could not allocate fw_response\n");
		return -ENOMEM;
	}
	ret = usb_control_msg_recv(t818->usbdev, 0, 0x42,
							   0xc1, 0, 0,
							   short_response, 3, USB_CTRL_SET_TIMEOUT,
							   GFP_KERNEL);

	struct t818_twobyte_response *twobyte_response = kzalloc(sizeof(struct t818_twobyte_response), GFP_KERNEL);
	if (!twobyte_response)
	{
		hid_err(t818->hdev, "could not allocate fw_response\n");
		return -ENOMEM;
	}
	ret = usb_control_msg_recv(t818->usbdev, 0, 0x4e,
							   0xc1, 0, 0,
							   twobyte_response, 2, USB_CTRL_SET_TIMEOUT,
							   GFP_KERNEL);

	ret = usb_control_msg_recv(t818->usbdev, 0, 0x56,
							   0xc1, 0, 0,
							   ext_props_response, 4, USB_CTRL_SET_TIMEOUT,
							   GFP_KERNEL);

	// for (i = 0; i < ARRAY_SIZE(ext_props_response->unused1); i++)
	// {
	// 	hid_info(t818->hdev, "0x%02X\n", ext_props_response->unused1[i]);
	// }

	if (ret < 0)
	{
		hid_err(t818->hdev, "Initialization data couldn't be sent\n");
		goto err;
	}

err:
	return ret;
}

int t818_wheel_destroy(void *data)
{
	struct t300rs_device_entry *t300rs = data;

	if (!t300rs)
		return -ENODEV;

	kfree(t300rs->send_buffer);
	kfree(t300rs);
	return 0;
}

int t818_set_range(void *data, uint16_t value)
{
	struct t300rs_device_entry *t818 = data;

	if (value < 140)
	{
		hid_info(t818->hdev, "value %i too small, clamping to 140\n", value);
		value = 140;
	}

	if (value > 1080)
	{
		hid_info(t818->hdev, "value %i too large, clamping to 1080\n", value);
		value = 1080;
	}

	return t300rs_set_range(data, value);
}

// 0: Comfort, 1: Sport, 2: Performance, 3: Extreme
static struct t818_ffb_modes
{
	int id;
	char *label;
} t818_ffbmodes[] = {
	{0, "Comfort"},
	{1, "Sport"},
	{2, "Performance"},
	{3, "Extreme"}};

int t818_set_mode(void *data, uint value)
{
	struct t300rs_device_entry *t818 = data;
	int ret;

	if (value > 3)
	{
		hid_info(t818->hdev, "value %i too large, clamping to 3\n", value);
		value = 3;
	}

	u8 *send_buf = kmalloc(256, GFP_KERNEL);
	struct usb_interface *usbif = to_usb_interface(t818->hdev->dev.parent);
	struct usb_host_endpoint *ep;
	int ret2, trans, b_ep;

	if (!send_buf)
	{
		hid_err(t818->hdev, "failed allocating send buffer\n");
		return -ENOMEM;
	}

	ep = &usbif->cur_altsetting->endpoint[1];
	b_ep = ep->desc.bEndpointAddress;

	if (!t818)
		return -ENODEV;

	u8 setMode[64] = {0x0a, 0x04, 0x00, 0x2a, 0x00, 0x01, 0x01};
	setMode[5] = value;

	memcpy(send_buf, setMode, ARRAY_SIZE(setMode));

	ret2 = usb_interrupt_msg(t818->usbdev,
							 usb_sndintpipe(t818->usbdev, b_ep),
							 send_buf, ARRAY_SIZE(setMode),
							 &trans,
							 USB_CTRL_SET_TIMEOUT);

	if (ret2)
	{
		hid_err(t818->hdev, "mode could not be set\n");
		goto err;
	}

	// Store new mode, as everything worked out fine
	t818->mode = value;
	return ret;

err:
	kfree(send_buf);
	return ret2;
}

ssize_t t818_ffb_mode_show(void *data, char *buf)
{
	struct t300rs_device_entry *t818 = data;
	ssize_t count = 0;
	int i;
	if (!t818)
		return -ENODEV;

	for (i = 0; i < ARRAY_SIZE(t818_ffbmodes); i++)
	{
		count += scnprintf(buf + count, PAGE_SIZE - count, "%i: %s",
						   t818_ffbmodes[i].id, t818_ffbmodes[i].label);

		if (count >= PAGE_SIZE - 1)
			return count;

		if (t818_ffbmodes[i].id == t818->mode)
			count += scnprintf(buf + count, PAGE_SIZE - count, " *\n");
		else
			count += scnprintf(buf + count, PAGE_SIZE - count, "\n");

		if (count >= PAGE_SIZE - 1)
			return count;
	}
	return count;
}

int t818_set_color(void *data, uint value)
{
	struct t300rs_device_entry *t818 = data;
	int ret;

	u8 *send_buf = kmalloc(256, GFP_KERNEL);
	struct usb_interface *usbif = to_usb_interface(t818->hdev->dev.parent);
	struct usb_host_endpoint *ep;
	int ret2, trans, b_ep;

	if (!send_buf)
	{
		hid_err(t818->hdev, "failed allocating send buffer\n");
		return -ENOMEM;
	}

	ep = &usbif->cur_altsetting->endpoint[1];
	b_ep = ep->desc.bEndpointAddress;

	if (!t818)
		return -ENODEV;

	uint8_t rgba[4];
	memcpy(rgba, &value, sizeof(value));
	u8 setColor[64] = {0x0a, 0x04, 0x00, 0x24, 0xfe, 0x00, 0x00, 0x00, 0x00};
	setColor[5] = rgba[3];
	setColor[6] = rgba[2];
	setColor[7] = rgba[1];
	setColor[8] = rgba[0];

	memcpy(send_buf, setColor, ARRAY_SIZE(setColor));

	ret2 = usb_interrupt_msg(t818->usbdev,
							 usb_sndintpipe(t818->usbdev, b_ep),
							 send_buf, ARRAY_SIZE(setColor),
							 &trans,
							 USB_CTRL_SET_TIMEOUT);

	if (ret2)
	{
		hid_err(t818->hdev, "color could not be set\n");
		goto err;
	}

	// Store new mode, as everything worked out fine
	mode = value;
	return ret;

err:
	kfree(send_buf);
	return ret2;
}

static int t818_send_open(struct t300rs_device_entry *t818)
{
	int r1, r2;
	t818->send_buffer[0] = 0x01;
	t818->send_buffer[1] = 0x04;
	if ((r1 = t300rs_send_int(t818)))
		return r1;

	t818->send_buffer[0] = 0x01;
	t818->send_buffer[1] = 0x05;
	if ((r2 = t300rs_send_int(t818)))
		return r2;

	return 0;
}

static int t818_open(void *data, int open_mode)
{
	struct t300rs_device_entry *t818 = data;

	if (!t818)
		return -ENODEV;

	if (open_mode)
		t818_send_open(t818);

	return t818->open(t818->input_dev);
}

static int t818_send_close(struct t300rs_device_entry *t818)
{
	int r1, r2;
	t818->send_buffer[0] = 0x01;
	t818->send_buffer[1] = 0x05;
	if ((r1 = t300rs_send_int(t818)))
		return r1;

	t818->send_buffer[0] = 0x01;
	t818->send_buffer[1] = 0x00;
	if ((r2 = t300rs_send_int(t818)))
		return r2;

	return 0;
}

static int t818_close(void *data, int open_mode)
{
	struct t300rs_device_entry *t818 = data;

	if (!t818)
		return -ENODEV;

	if (open_mode)
		t818_send_close(t818);

	t818->close(t818->input_dev);
	return 0;
}

int t818_wheel_init(struct tmff2_device_entry *tmff2, int open_mode)
{
	struct t300rs_device_entry *t818 = kzalloc(sizeof(struct t300rs_device_entry), GFP_KERNEL);
	struct list_head *report_list;
	int ret;

	if (!t818)
	{
		ret = -ENOMEM;
		goto t818_err;
	}

	t818->hdev = tmff2->hdev;
	t818->input_dev = tmff2->input_dev;
	t818->usbdev = to_usb_device(tmff2->hdev->dev.parent->parent);
	t818->buffer_length = t818_BUFFER_LENGTH;

	t818->send_buffer = kzalloc(t818->buffer_length, GFP_KERNEL);
	if (!t818->send_buffer)
	{
		ret = -ENOMEM;
		goto send_err;
	}

	report_list = &t818->hdev->report_enum[HID_OUTPUT_REPORT].report_list;
	t818->report = list_entry(report_list->next, struct hid_report, list);
	t818->ff_field = t818->report->field[0];

	t818->open = t818->input_dev->open;
	t818->close = t818->input_dev->close;

	if ((ret = t818_controls(t818)))
		goto controls_err;

	if ((ret = t818_interrupts(t818)))
		goto interrupt_err;

	/* everything went OK */
	tmff2->data = t818;
	tmff2->params = t818_params;
	tmff2->max_effects = t818_MAX_EFFECTS;
	memcpy(tmff2->supported_effects, t818_effects, sizeof(t818_effects));

	if (!open_mode)
		t818_send_open(t818);

	hid_info(t818->hdev, "force feedback for T818\n");
	return 0;

interrupt_err:
controls_err:
send_err:
	kfree(t818);
t818_err:
	hid_err(tmff2->hdev, "failed initializing t818\n");
	return ret;
}

static __u8 *t818_wheel_fixup(struct hid_device *hdev, __u8 *rdesc,
							  unsigned int *rsize)
{
	rdesc = t818_pc_rdesc_fixed;
	*rsize = sizeof(t818_pc_rdesc_fixed);
	return rdesc;
}

int t818_populate_api(struct tmff2_device_entry *tmff2)
{
	tmff2->play_effect = t300rs_play_effect;
	tmff2->upload_effect = t300rs_upload_effect;
	tmff2->update_effect = t300rs_update_effect;
	tmff2->stop_effect = t300rs_stop_effect;

	tmff2->set_gain = t300rs_set_gain;
	tmff2->set_autocenter = t300rs_set_autocenter;
	tmff2->set_range = t818_set_range;
	tmff2->wheel_fixup = t818_wheel_fixup;
	tmff2->set_mode = t818_set_mode;
	tmff2->mode_show = t818_ffb_mode_show;
	tmff2->set_color = t818_set_color;

	tmff2->open = t818_open;
	tmff2->close = t818_close;

	tmff2->wheel_init = t818_wheel_init;
	tmff2->wheel_destroy = t818_wheel_destroy;

	return 0;
}
