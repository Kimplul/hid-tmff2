/* SPDX-License-Identifier: GLP-2.0 */
#include <linux/hid.h>
#include <linux/usb.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/ktime.h>
#include <linux/fixp-arith.h>

//#include "hid-ids.h"

#define USB_VENDOR_ID_THRUSTMASTER 0x044f

#define T300RS_MAX_EFFECTS 16
#define T300RS_NORM_BUFFER_LENGTH 63
#define T300RS_PS4_BUFFER_LENGTH 31

/* the wheel seems to only be capable of processing a certain number of
 * interrupts per second, and if this value is too low the kernel urb buffer(or
 * some buffer at least) fills up. Optimally I would figure out some way to
 * space out the interrupts so that they all leave at regular intervals, but
 * for now this is good enough, go slow enough that everything works.
 */
#define DEFAULT_TIMER_PERIOD 8

#define FF_EFFECT_QUEUE_UPLOAD 0
#define FF_EFFECT_QUEUE_START 1
#define FF_EFFECT_QUEUE_STOP 2
#define FF_EFFECT_PLAYING 3
#define FF_EFFECT_QUEUE_UPDATE 4

#undef fixp_sin16
#define fixp_sin16(v) (((v % 360) > 180)? -(fixp_sin32((v % 360) - 180) >> 16) : fixp_sin32(v) >> 16)
#define JIFFIES2MS(jiffies) ((jiffies) * 1000 / HZ)

spinlock_t lock;
unsigned long lock_flags;

spinlock_t data_lock;
unsigned long data_flags;

static const signed short t300rs_ff_effects[] = {
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

struct t300rs_effect_state {
	struct ff_effect effect;
	struct ff_effect old;
	bool old_set;
	unsigned long flags;
	unsigned long start_time;
	unsigned long count;
};

struct __packed t300rs_firmware_response {
	uint8_t unused1[2];
	uint8_t firmware_version;
	uint8_t unused2;
};


struct __packed t300rs_packet_header {
	uint8_t zero1;
	uint8_t id;
	uint8_t code;
};

struct __packed t300rs_setup_header {
	uint8_t cmd;
	uint8_t code;
};

struct __packed t300rs_packet_envelope {
	uint16_t attack_length;
	uint16_t attack_level;
	uint16_t fade_length;
	uint16_t fade_level;
};

struct __packed t300rs_packet_timing {
	uint8_t start_marker;
	uint16_t duration;
	uint8_t zero1[2];
	uint16_t offset;
	uint8_t zero2;
	uint16_t end_marker;
};

struct usb_ctrlrequest t300rs_firmware_request = {
	.bRequestType = 0xc1,
	.bRequest = 86,
	.wValue = 0,
	.wIndex = 0,
	.wLength = 8
};


struct t300rs_device_entry {
	struct hid_device *hdev;
	struct input_dev *input_dev;
	struct hid_report *report;
	struct hid_field *ff_field;
	struct usb_device *usbdev;
	struct usb_interface *usbif;
	struct t300rs_effect_state *states;
	struct t300rs_firmware_response *firmware_response;

	struct delayed_work work;

	int (*open)(struct input_dev *dev);
	void (*close)(struct input_dev *dev);

	spinlock_t lock;
	unsigned long lock_flags;

	u8 buffer_length;
	u8 *send_buffer;

	u16 range;
	u8 effects_used;

	u8 adv_mode;
};


struct t300rs_data {
	unsigned long quirks;
	void *device_props;
};

static u8 t300rs_rdesc_nrm_fixed[] = {
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

static u8 t300rs_rdesc_adv_fixed[] = {
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
	0x09, 0x31, /* Usage (Y) */
	0x26, 0xff, 0x03, /* Logical maximum (1023) */
	0x46, 0xff, 0x03, /* Physical maximum (1023) */
	0x81, 0x02, /* Input (Variable, Absolute) */
	0x09, 0x35, /* Usage (Rz) */
	0x81, 0x02, /* Input (Variable, Absolute) */
	0x09, 0x36, /* Usage (Slider) */
	0x81, 0x02, /* Input (Variable, Absolute) */
	0x81, 0x03, /* Input (Variable, Absolute, Constant) ? */
	0x05, 0x09, /* Usage page (Button) */
	0x19, 0x01, /* Usage minimum (1) */
	0x29, 0x19, /* Usage maximum (25) */
	0x25, 0x01, /* Logical maximum (1) */
	0x45, 0x01, /* Physical maximum (1) */
	0x75, 0x01, /* Report size (1) */
	0x95, 0x19, /* Report count (25) */
	0x81, 0x02, /* Input (Variable, Absolute) */
	0x75, 0x03, /* Report size (3) */
	0x95, 0x01, /* Report count (1) */
	0x81, 0x03, /* Input (Variable, Absolute, Constant) */
	0x05, 0x01, /* Usage page (Generic Desktop) */
	0x09, 0x39, /* Usage (Hat Switch) */
	0x25, 0x07, /* Logical maximum (7) */
	0x46, 0x3b, 0x01, /* Physical maximum (315) */
	0x55, 0x00, /* Unit exponent (0) */
	0x65, 0x14, /* Unit (Eng Rot, Angular Pos) */
	0x75, 0x04, /* Report size (4) */
	0x81, 0x42, /* Input (Variable, Absolute, NullState) */
	0x65, 0x00, /* Unit (None) */
	0x85, 0x60, /* Report ID (96), prev 10 */
	0x06, 0x00, 0xff, /* Usage page (Vendor 1) */
	0x09, 0x60, /* Usage (96), prev 10 (why?) */
	0x75, 0x08, /* Report size (8) */
	0x95, 0x3f, /* Report count (63) */
	0x26, 0xff, 0x00, /* Logical maximum (255) */
	0x46, 0xff, 0x00, /* Physical maximum (255) */
	0x91, 0x02, /* Output (Variable, Absolute) */
	0x85, 0x02, /* Report ID (2) */
	0x09, 0x02, /* Usage (Mouse) */
	0x81, 0x02, /* Input (Variable, Absolute) */
	0x09, 0x14, /* Usage (20) */
	0x85, 0x14, /* Report ID (20) */
	0x81, 0x02, /* Input (Variable, Absolute) */
	0xc0, /* End collection */
	0xc0, /* End collection */
};

static u8 t300rs_rdesc_ps4_fixed[] = {
	0x05, 0x01, /* Usage page (Generic Desktop) */
	0x09, 0x05, /* Usage (GamePad) */
	0xa1, 0x01, /* Collection (Application) */
	0x85, 0x01, /* Report ID (1) */
	0x09, 0x00, /* Usage (U) (was X) */
	0x09, 0x00, /* Usage (U) (was Y) */
	0x09, 0x00, /* Usage (U) (was Z) */
	0x09, 0x00, /* Usage (U) (was Rz)*/
	0x15, 0x00, /* Logical minimum (0) */
	0x26, 0xff, 0x00, /* Logical maximum (255) */
	0x75, 0x08, /* Report size (8) */
	0x95, 0x04, /* Report count (4) */
	0x81, 0x02, /* Input (Variable, Absolute) */
	0x09, 0x39, /* Usage (Hat Switch) */
	0x15, 0x00, /* Logical minimum (0) */
	0x25, 0x07, /* Logical maximum (7)*/
	0x35, 0x00, /* Physical minimum (0) */
	0x46, 0x3b, 0x01, /* Physical maximum (315) */
	0x65, 0x14, /* Unit (Eng Rot, Angular Pos) */
	0x75, 0x04, /* Report size (4) */
	0x95, 0x01, /* Report count (1) */
	0x81, 0x42, /* Input (Variable, Absolute, NullState) */
	0x65, 0x00, /* Input (None) */
	0x05, 0x09, /* Usage page (Button) */
	0x19, 0x01, /* Usage minimum (1) */
	0x29, 0x0e, /* Usage maximum (14) */
	0x15, 0x00, /* Logical minimum (0) */
	0x25, 0x01, /* Logical maximum (1) */
	0x75, 0x01, /* Report size (1) */
	0x95, 0x0e, /* Report size (14) */
	0x81, 0x02, /* Input (Variable, Absolute) */
	0x06, 0x00, 0xff, /* Usage page (Vendor 1) */
	0x09, 0x20, /* Usage (32) */
	0x75, 0x06, /* Report size (6) */
	0x95, 0x01, /* Report count (1) */
	0x81, 0x02, /* Input (Variable, Absolute) */
	0x05, 0x01, /* Usage page (Generic Desktop) */
	0x09, 0x00, /* Usage (U) (was Rx)*/
	0x09, 0x00, /* Usage (U) (was Ry) */
	0x15, 0x00, /* Logical minimum (0) */
	0x26, 0xff, 0x00, /* Logical maximum (255) */
	0x75, 0x08, /* Report size (8) */
	0x95, 0x02, /* Report count (2) */
	0x81, 0x02, /* Input (Variable, Absolute) */
	0x05, 0x01, /* Usage page (Vendor 1) */
	/* constant zero? */
	0x09, 0x00, /* Usage (33) */
	0x95, 0x21, /* Report count (33) */
	0x81, 0x02, /* Input (Variable, Absolute) */
	/* wheel */
	0x09, 0x30, /* Usage (X) */
	0x15, 0x00, /* Logical minimum (0) */
	0x27, 0xff, 0xff, 0x00, 0x00, /* Logical maximum (65535) */
	0x35, 0x00, /* Physical minimum (0) */
	0x47, 0xff, 0xff, 0x00, 0x00, /* Physical maximum (65535) */
	0x75, 0x10, /* Report size (16) */
	0x95, 0x01, /* Report count (1) */
	0x81, 0x02, /* Input (Variable, Absolute) */
	/* gas */
	0x09, 0x31, /* Usage (Y) */
	0x81, 0x02, /* Input (Variable, Absolute) */
	/* brake */
	0x09, 0x32, /* Usage (Z) */
	0x81, 0x02, /* Input (Variable, Absolute) */
	/* clutch */
	0x09, 0x35, /* Usage (Rz) */
	0x81, 0x02, /* Input (Variable, Absolute) */
	/* stick shifter (check model no) */
	0x05, 0x09, /* Usage page (Button) */
	0x19, 0x0f, /* Usage minimum (15) */
	0x29, 0x17, /* Usage maximum (23) */
	0x15, 0x00, /* Logical minimum (1) */
	0x25, 0x01, /* Logical maximum (1) */
	0x75, 0x01, /* Report size (1) */
	0x95, 0x08, /* Report count (8) */
	0x81, 0x02, /* Input (Variable, Absolute) */
	/* no clue */
	0x05, 0x01, /* Usage page (Generic Desktop) */
	0x09, 0x00, /* Usage (U) */
	0x75, 0x08, /* Report size (8) */
	0x95, 0x0c, /* Report count (12) */
	0x81, 0x02, /* Input (Variable, Absolute) */
	/* continue unmodified */
	0x06, 0x00, 0xff, /* Usage page (Vendor defined 1) */
	0x85, 0x60, /* Report ID (5) (change to 0x60?) */
	0x09, 0x60, /* Usage (34) (change to 0x60?) */
	0x95, 0x1f, /* Report count (31) () */
	0x91, 0x02, /* Output (Variable, Absolute) */
	0x85, 0x03, /* Report ID (3) */
	0x0a, 0x21, 0x27, /* ??? */
	0x95, 0x2f, /* Report count (47) */
	0xb1, 0x02, /* Feature (Data, Var, Abs) */
	0xc0, /* End collection */

	/* From here on out no clue */
	0x06, 0xf0,
	0xff, 0x09,
	0x40, 0xa1,
	0x01, 0x85,
	0xf0, 0x09,
	0x47, 0x95,
	0x3f, 0xb1,
	0x02, 0x85,
	0xf1, 0x09,
	0x48, 0x95,
	0x3f, 0xb1,
	0x02, 0x85,
	0xf2, 0x09,
	0x49, 0x95,
	0x0f, 0xb1,
	0x02, 0x85,
	0xf3, 0x0a,
	0x01, 0x47,
	0x95, 0x07,
	0xb1, 0x02,
	0xc0,
};

static u8 spring_values[] = {
	0xa6, 0x6a, 0xa6, 0x6a, 0xfe,
	0xff, 0xfe, 0xff, 0xfe, 0xff,
	0xfe, 0xff, 0xdf, 0x58, 0xa6,
	0x6a, 0x06
};

static u8 damper_values[] = {
	0xfc, 0x7f, 0xfc, 0x7f, 0xfe,
	0xff, 0xfe, 0xff, 0xfe, 0xff,
	0xfe, 0xff, 0xfc, 0x7f, 0xfc,
	0x7f, 0x07
};
