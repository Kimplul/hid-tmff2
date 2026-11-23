// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for the Thrustmaster T-GT II. The wheel is booted into its native
 * mode (b684) by hid-tminit, where its own report descriptor declares all
 * inputs, including the rotary encoders as buttons. The force feedback
 * protocol is the T300RS one, but the wheel additionally expects an
 * initialization handshake, a calibration run and an explicit open command
 * before it accepts any effect commands (all captured from the Windows
 * driver).
 */

#include <linux/hid.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/workqueue.h>

#include "../hid-tmff2.h"

#define TGT2_MAX_EFFECTS 16
#define TGT2_BUFFER_LENGTH 63

static const signed short tgt2_effects[] = {
	FF_CONSTANT,
	FF_SPRING,
	FF_DAMPER,
	FF_FRICTION,
	FF_INERTIA,
	FF_SINE,
	FF_SQUARE,
	FF_TRIANGLE,
	FF_SAW_UP,
	FF_SAW_DOWN,
	FF_RAMP,
	FF_PERIODIC,
	FF_RUMBLE,
	FF_GAIN,
	FF_AUTOCENTER,
	-1
};

static const unsigned long tgt2_params =
	PARAM_SPRING_LEVEL
	| PARAM_DAMPER_LEVEL
	| PARAM_FRICTION_LEVEL
	| PARAM_RANGE
	| PARAM_GAIN;

/*
 * The wheel's own b684 descriptor with one change: the force feedback
 * output report is renumbered from 0x0a to 0x60, because the firmware
 * expects the T300-family report ids on the wire (observed on Windows:
 * initialization and open commands on reports 0x0a and 0x42/0x60, all
 * force feedback effects on report 0x60). The input side is kept untouched.
 */
static u8 tgt2_rdesc_fixed[] = {
	0x05, 0x01,       /* Usage Page (Generic Desktop) */
	0x09, 0x04,       /* Usage (Joystick) */
	0xa1, 0x01,       /* Collection (Application) */
	0x09, 0x01,       /* Usage (Pointer) */
	0xa1, 0x00,       /* Collection (Physical) */
	0x85, 0x07,       /* Report ID (7) - inputs */
	0x09, 0x30,       /* Usage (X) - steering */
	0x15, 0x00,       /* Logical Minimum (0) */
	0x27, 0xff, 0xff, 0x00, 0x00, /* Logical Maximum (65535) */
	0x35, 0x00,       /* Physical Minimum (0) */
	0x47, 0xff, 0xff, 0x00, 0x00, /* Physical Maximum (65535) */
	0x75, 0x10,       /* Report Size (16) */
	0x95, 0x01,       /* Report Count (1) */
	0x81, 0x02,       /* Input (Data,Var,Abs) */
	0x09, 0x31,       /* Usage (Y) - throttle */
	0x26, 0xff, 0x03, /* Logical Maximum (1023) */
	0x46, 0xff, 0x03, /* Physical Maximum (1023) */
	0x81, 0x02,       /* Input (Data,Var,Abs) */
	0x09, 0x35,       /* Usage (Rz) - brake */
	0x81, 0x02,       /* Input (Data,Var,Abs) */
	0x09, 0x36,       /* Usage (Slider) - clutch */
	0x81, 0x02,       /* Input (Data,Var,Abs) */
	0x81, 0x03,       /* Input (Constant) */
	0x75, 0x08,       /* Report Size (8) */
	0x26, 0xff, 0x00, /* Logical Maximum (255) */
	0x46, 0xff, 0x00, /* Physical Maximum (255) */
	0x09, 0x33,       /* Usage (Rx) - left stick X */
	0x81, 0x02,       /* Input (Data,Var,Abs) */
	0x09, 0x34,       /* Usage (Ry) - left stick Y */
	0x81, 0x02,       /* Input (Data,Var,Abs) */
	0x09, 0x32,       /* Usage (Z) - right stick X */
	0x81, 0x02,       /* Input (Data,Var,Abs) */
	0x09, 0x37,       /* Usage (Wheel) - right stick Y */
	0x81, 0x02,       /* Input (Data,Var,Abs) */
	0x05, 0x09,       /* Usage Page (Button) */
	0x19, 0x01,       /* Usage Minimum (1) */
	0x29, 0x19,       /* Usage Maximum (25) */
	0x25, 0x01,       /* Logical Maximum (1) */
	0x45, 0x01,       /* Physical Maximum (1) */
	0x75, 0x01,       /* Report Size (1) */
	0x95, 0x19,       /* Report Count (25) */
	0x81, 0x02,       /* Input (Data,Var,Abs) */
	0x75, 0x07,       /* Report Size (7) */
	0x95, 0x01,       /* Report Count (1) */
	0x81, 0x03,       /* Input (Constant) */
	0x05, 0x01,       /* Usage Page (Generic Desktop) */
	0x09, 0x39,       /* Usage (Hat Switch) */
	0x25, 0x07,       /* Logical Maximum (7) */
	0x46, 0x3b, 0x01, /* Physical Maximum (315) */
	0x55, 0x00,       /* Physical Minimum (0) */
	0x65, 0x14,       /* Unit (Eng Rot) */
	0x75, 0x04,       /* Report Size (4) */
	0x81, 0x42,       /* Input (Data,Var,Abs,Null) */
	0x65, 0x00,       /* Unit (None) */
	0x81, 0x03,       /* Input (Constant) */
	0x85, 0x60,       /* Report ID (96) - force feedback */
	0x06, 0x00, 0xff, /* Usage Page (Vendor 0xff00) */
	0x09, 0x60,       /* Usage (0x60) */
	0x75, 0x08,       /* Report Size (8) */
	0x95, 0x3f,       /* Report Count (63) */
	0x26, 0xff, 0x00, /* Logical Maximum (255) */
	0x46, 0xff, 0x00, /* Physical Maximum (255) */
	0x91, 0x02,       /* Output (Data,Var,Abs) */
	0x85, 0x02,       /* Report ID (2) - vendor input */
	0x09, 0x02,       /* Usage (0x02) */
	0x81, 0x02,       /* Input (Data,Var,Abs) */
	0x09, 0x14,       /* Usage (0x14) */
	0x85, 0x14,       /* Report ID (20) - vendor input */
	0x81, 0x02,       /* Input (Data,Var,Abs) */
	0xc0,             /* End Collection */
	0xc0,             /* End Collection */
};

static __u8 *tgt2_wheel_fixup(struct hid_device *hdev, __u8 *rdesc,
		unsigned int *rsize)
{
	*rsize = sizeof(tgt2_rdesc_fixed);
	return tgt2_rdesc_fixed;
}

static int tgt2_send(struct usb_device *usbdev, u8 ep, u8 report,
		const u8 *data, size_t len)
{
	u8 *buf = kmalloc(64, GFP_KERNEL);
	int transferred, ret;

	if (!buf)
		return -ENOMEM;

	buf[0] = report;
	memcpy(buf + 1, data, min(len, 63));
	memset(buf + 1 + len, 0, 63 - min(len, 63));

	ret = usb_interrupt_msg(usbdev, usb_sndintpipe(usbdev, ep),
		buf, 64, &transferred, USB_CTRL_SET_TIMEOUT);

	kfree(buf);
	return ret;
}

/*
 * The wheel expects to be calibrated shortly after booting into its
 * native mode; the sequence was captured from the Windows driver. It
 * ends with the T300-style open command, which the wheel requires
 * before it accepts any force feedback effects.
 */
static struct usb_device *tgt2_calib_usbdev;
static u8 tgt2_calib_ep;

static void tgt2_calibrate(struct work_struct *work)
{
	int i;

	for (i = 0; i < 7; i++)
		tgt2_send(tgt2_calib_usbdev, tgt2_calib_ep,
			0x0a, (u8[]){0x04, 0x03, 0x00}, 3);
	/* sweep the wheel lock to lock */
	for (i = 0; i < 38; i++) {
		tgt2_send(tgt2_calib_usbdev, tgt2_calib_ep,
			0x0a, (u8[]){0x35, 0xf8, 0x3f}, 3);
		msleep(98);
	}
	tgt2_send(tgt2_calib_usbdev, tgt2_calib_ep, 0x0a, (u8[]){0x35, 0xfa, 0x3f}, 3);
	tgt2_send(tgt2_calib_usbdev, tgt2_calib_ep, 0x0a, (u8[]){0x35, 0xfa, 0x3f}, 3);
	tgt2_send(tgt2_calib_usbdev, tgt2_calib_ep, 0x0a, (u8[]){0x04, 0x12, 0x10}, 3);
	tgt2_send(tgt2_calib_usbdev, tgt2_calib_ep, 0x0a, (u8[]){0x04, 0x00, 0x0c}, 3);
	tgt2_send(tgt2_calib_usbdev, tgt2_calib_ep, 0x0a, (u8[]){0x35, 0xfc, 0x0f}, 3);
	tgt2_send(tgt2_calib_usbdev, tgt2_calib_ep, 0x0a, (u8[]){0x35, 0xfe, 0x0f}, 3);
	tgt2_send(tgt2_calib_usbdev, tgt2_calib_ep, 0x0a, (u8[]){0x35, 0xfe, 0x0f}, 3);
	tgt2_send(tgt2_calib_usbdev, tgt2_calib_ep, 0x0a, (u8[]){0x04, 0x90, 0x03}, 3);
	/* open the wheel for force feedback, T300-style */
	tgt2_send(tgt2_calib_usbdev, tgt2_calib_ep, 0x60, (u8[]){0x01, 0x05}, 2);

	usb_put_dev(tgt2_calib_usbdev);
}
static DECLARE_DELAYED_WORK(tgt2_calib_work, tgt2_calibrate);

/* like t300rs_wheel_init, but without the T300RS open packet (which is
 * sent as part of the calibration above), the attachment query and the
 * alternate-mode handling: the attachment query is a bootloader command
 * and knocks the wheel out of its native mode again, and the alternate
 * modes do not apply to this wheel */
static int tgt2_wheel_init(struct tmff2_device_entry *tmff2, int open_mode)
{
	struct t300rs_device_entry *t300rs =
		kzalloc(sizeof(struct t300rs_device_entry), GFP_KERNEL);
	struct usb_interface *interface = to_usb_interface(tmff2->hdev->dev.parent);
	struct usb_host_endpoint *ep_out = NULL;
	struct list_head *report_list;
	int i, ret;

	if (!t300rs) {
		ret = -ENOMEM;
		goto t300rs_err;
	}

	t300rs->hdev = tmff2->hdev;
	t300rs->input_dev = tmff2->input_dev;
	t300rs->usbdev = to_usb_device(tmff2->hdev->dev.parent->parent);

	t300rs->buffer_length = TGT2_BUFFER_LENGTH;

	t300rs->send_buffer = kzalloc(t300rs->buffer_length, GFP_KERNEL);
	if (!t300rs->send_buffer) {
		ret = -ENOMEM;
		goto send_err;
	}

	report_list = &t300rs->hdev->report_enum[HID_OUTPUT_REPORT].report_list;

	/* because we set the rdesc, we know exactly which report and field
	 * to use */
	t300rs->report = list_entry(report_list->next, struct hid_report, list);
	t300rs->ff_field = t300rs->report->field[0];

	for (i = 0; i < interface->cur_altsetting->desc.bNumEndpoints; i++) {
		struct usb_host_endpoint *ep = &interface->cur_altsetting->endpoint[i];

		if (usb_endpoint_is_int_out(&ep->desc))
			ep_out = ep;
	}

	/* initialization handshake the wheel expects right after booting
	 * into its native mode (captured from the Windows driver) */
	if (ep_out) {
		u8 ep = ep_out->desc.bEndpointAddress;

		ret = tgt2_send(t300rs->usbdev, ep, 0x0a, (u8[]){0x04, 0x90, 0x03}, 3);
		if (!ret)
			ret = tgt2_send(t300rs->usbdev, ep, 0x0a, (u8[]){0x04, 0x00, 0x0c}, 3);
		if (!ret)
			ret = tgt2_send(t300rs->usbdev, ep, 0x0a, (u8[]){0x04, 0x12, 0x10}, 3);
		if (!ret)
			ret = tgt2_send(t300rs->usbdev, ep, 0x0a, (u8[]){0x04, 0x03, 0x00}, 3);
		if (!ret)
			ret = tgt2_send(t300rs->usbdev, ep, 0x42, (u8[]){0x01}, 1);

		if (ret) {
			hid_warn(t300rs->hdev, "initialization failed: %i\n", ret);
			goto init_err;
		}

		cancel_delayed_work_sync(&tgt2_calib_work);
		tgt2_calib_usbdev = usb_get_dev(t300rs->usbdev);
		tgt2_calib_ep = ep_out->desc.bEndpointAddress;
		schedule_delayed_work(&tgt2_calib_work, msecs_to_jiffies(1000));
	} else {
		hid_warn(t300rs->hdev, "no interrupt out endpoint found\n");
		ret = -ENODEV;
		goto init_err;
	}

	tmff2->data = t300rs;
	tmff2->params = tgt2_params;
	tmff2->max_effects = TGT2_MAX_EFFECTS;
	memcpy(tmff2->supported_effects, tgt2_effects, sizeof(tgt2_effects));

	hid_info(t300rs->hdev, "force feedback for T-GT II\n");
	return 0;

init_err:
send_err:
	kfree(t300rs->send_buffer);
	t300rs->send_buffer = NULL;
	kfree(t300rs);
t300rs_err:
	return ret;
}

int tgt2_populate_api(struct tmff2_device_entry *tmff2)
{
	/* the wheel reports no serial number, which makes userspace fall
	 * back to unstable identifiers (eg. device node paths) when telling
	 * replugged wheels apart - breaking per-device settings such as game
	 * control bindings. Use a stable fake serial instead */
	strscpy(tmff2->hdev->uniq, "Thrustmaster T-GT II",
		sizeof(tmff2->hdev->uniq));

	/* the T-GT II shares the T300RS force feedback protocol */
	t300rs_populate_api(tmff2);

	/* everything that would switch the wheel mode has to go: the open
	 * and close packets are sent as part of the initialization and
	 * calibration above instead, and the T300RS attachment query and
	 * alternate modes do not apply to this wheel */
	tmff2->wheel_init = tgt2_wheel_init;
	tmff2->wheel_fixup = tgt2_wheel_fixup;
	tmff2->open = NULL;
	tmff2->close = NULL;
	tmff2->switch_mode = NULL;
	tmff2->alt_mode_show = NULL;
	tmff2->alt_mode_store = NULL;

	return 0;
}
