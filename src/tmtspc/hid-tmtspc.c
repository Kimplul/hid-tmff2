// SPDX-License-Identifier: GPL-2.0-or-later
#include <linux/usb.h>
#include <linux/hid.h>
#include "../hid-tmff2.h"

/* TODO: sort through this stuff */
static const u8 tspc_pc_rdesc_fixed[] = {
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

static ssize_t tspc_alt_mode_store(void *data, const char *buf, size_t count)
{
	struct t300rs_device_entry *tspc = data;
	int ret;

	if (!tspc)
		return -ENODEV;

	/* blindly trusting that this works for now */
	ret = usb_control_msg(tspc->usbdev,
			usb_sndctrlpipe(tspc->usbdev, 0),
			83, 0x41, 0xb, 0, NULL, 0,
			USB_CTRL_SET_TIMEOUT);
	if (ret < 0) {
		hid_err(tspc->hdev, "failed switching mode: %i\n", ret);
		return ret;
	}

	return count;
}

static const struct tm_wheel_desc tspc_desc = {
	.name = "TS-PC",
	.rdesc = tspc_pc_rdesc_fixed,
	.rsize = sizeof(tspc_pc_rdesc_fixed),
	/* TS-PC has 1080 degree range, like T300RS */
	.range_min = 140,
	.range_max = 1080,
	.params = PARAM_SPRING_LEVEL
		| PARAM_DAMPER_LEVEL
		| PARAM_FRICTION_LEVEL
		| PARAM_ALT_MODE
		| PARAM_RANGE
		| PARAM_GAIN,
	.alt_mode_store = tspc_alt_mode_store,
};

int tspc_populate_api(struct tmff2_device_entry *tmff2)
{
	return tm_common_populate_api(tmff2, &tspc_desc);
}
