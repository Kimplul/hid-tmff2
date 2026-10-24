// SPDX-License-Identifier: GPL-2.0-or-later
#include <linux/usb.h>
#include <linux/hid.h>
#include "../hid-tmff2.h"

/* TODO: sort through this stuff */
static const u8 t248_pc_rdesc_fixed[] = {
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
	0x09, 0x31, /* Usage (Y) TODO: clutch? */
	0x26, 0xff, 0x03, /* Logical maximum (1023) */
	0x46, 0xff, 0x03, /* Physical maximum (1023) */
	0x81, 0x02, /* Input (Variable, Absolute) */
	0x09, 0x35, /* Usage (Rz) TODO: brake? */
	0x81, 0x02, /* Input (Variable, Absolute) */
	0x09, 0x36, /* Usage (Slider) */
	0x81, 0x02, /* Input (Variable, Absolute) */
	0x75, 0x08, /* Report size (8) */
	0x26, 0xff, 0x00, /* Logical maximum (255) */
	0x46, 0xff, 0x00, /* Physical maximum (255) */
	0x09, 0x40, /* Usage (Vx) TODO: what is this? */
	0x81, 0x02, /* Input (Variable, Absolute) */
	0x09, 0x41, /* Usage (Vy) TODO: --||-- */
	0x81, 0x02, /* Input (Variable, Absolute) */
	0x09, 0x33, /* Usage (Rx) TODO: --||-- */
	0x81, 0x02, /* Input (Variable, Absolute) */
	0x09, 0x34, /* Usage (Ry) TODO: --||-- */
	0x81, 0x02, /* Input (Variable, Absolute) */
	0x09, 0x32, /* Usage (Z) TODO: --||-- (gas?) */
	0x81, 0x02, /* Input (Variable, Absolute) */
	0x09, 0x37, /* Usage (Dial) */
	0x81, 0x02, /* Input (Variable, Absolute) */
	0x05, 0x09, /* Usage page (Button) */
	0x19, 0x01, /* Usage minimum (1) */
	0x29, 0x1a, /* Usage maximum (13) */
	0x25, 0x01, /* Logical maximum (1) */
	0x45, 0x01, /* Physical maximum (1) */
	0x75, 0x01, /* Report size (1) */
	0x95, 0x1a, /* Report count (26) */
	0x81, 0x02, /* Input (Variable, Absolute) */
	0x75, 0x06, /* Report size (6) */
	0x95, 0x01, /* Report count (1) */
	0x81, 0x03, /* Usage (Variable, Absolute, Constant) */
	0x05, 0x01, /* Usage page (Generic Desktop) */
	0x09, 0x39, /* Usage (Hat Switch) */
	0x25, 0x07, /* Logical maximum (7) */
	0x46, 0x3b, 0x01, /* Physical maximum (315) */
	0x55, 0x00, /* Unit exponent (0) */
	0x65, 0x14, /* Unit (Eng rot, Angular Pos) */
	0x75, 0x04, /* Report size (4) */
	0x81, 0x42, /* Input (Variable, Absolute, NullState) */
	0x65, 0x00, /* Input (None) */
	0x81, 0x03, /* Input (Variable, Absolute, Constant) */
	0x85, 0x60, /* Report ID (96), prev 10 */
	0x06, 0x00, 0xff, /* Usage page (Vendor 1) */
	0x09, 0x60, /* Usage (96), prev 10 */
	0x75, 0x08, /* Report size (8) */
	0x95, 0x3f, /* Report count (63) */
	0x26, 0xff, 0x00, /* Logical maximum (256) */
	0x46, 0xff, 0x00, /* Physical maximum (256) */
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

static const struct tm_wheel_desc t248_desc = {
	.name = "T248",
	.rdesc = t248_pc_rdesc_fixed,
	.rsize = sizeof(t248_pc_rdesc_fixed),
	/* T248 only has 900 degree range, instead of T300RS 1080 */
	.range_min = 140,
	.range_max = 900,
	.params = PARAM_SPRING_LEVEL
		| PARAM_DAMPER_LEVEL
		| PARAM_FRICTION_LEVEL
		| PARAM_RANGE
		| PARAM_GAIN,
};

int t248_populate_api(struct tmff2_device_entry *tmff2)
{
	return tm_common_populate_api(tmff2, &t248_desc);
}
