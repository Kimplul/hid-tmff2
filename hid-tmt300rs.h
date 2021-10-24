/* SPDX-License-Identifier: GLP-2.0 */
#include <linux/hid.h>
#include <linux/usb.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/hrtimer.h>
#include <linux/slab.h>
#include <linux/ktime.h>
#include <linux/fixp-arith.h>

//#include "hid-ids.h"

#define USB_VENDOR_ID_THRUSTMASTER 0x044f

#define T300RS_MAX_EFFECTS 16
#define T300RS_BUFFER_LENGTH 63

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
	struct hrtimer hrtimer;

	int (*open)(struct input_dev *dev);
	void (*close)(struct input_dev *dev);

	spinlock_t lock;
	unsigned long lock_flags;

	u8 *send_buffer;

	u16 range;
	u8 effects_used;

	u8 adv_mode;
};


struct t300rs_data {
	unsigned long quirks;
	void *device_props;
};

static __u8 t300rs_rdesc_fixed[] = {
	0x05, 0x01, 0x09, 0x04, 0xa1,
	0x01, 0x09, 0x01, 0xa1, 0x00,
	0x85, 0x07, 0x09, 0x30, 0x15,
	0x00, 0x27, 0xff, 0xff, 0x00,
	0x00, 0x35, 0x00, 0x47, 0xff,
	0xff, 0x00, 0x00, 0x75, 0x10,
	0x95, 0x01, 0x81, 0x02, 0x09,
	0x35, 0x26, 0xff, 0x03, 0x46,
	0xff, 0x03, 0x81, 0x02, 0x09,
	0x32, 0x81, 0x02, 0x09, 0x31,
	0x81, 0x02, 0x81, 0x03, 0x05,
	0x09, 0x19, 0x01, 0x29, 0x0d,
	0x25, 0x01, 0x45, 0x01, 0x75,
	0x01, 0x95, 0x0d, 0x81, 0x02,
	0x75, 0x0b, 0x95, 0x01, 0x81,
	0x03, 0x05, 0x01, 0x09, 0x39,
	0x25, 0x07, 0x46, 0x3b, 0x01,
	0x55, 0x00, 0x65, 0x14, 0x75,
	0x04, 0x81, 0x42, 0x65, 0x00,
	0x81, 0x03, 0x85, 0x60, // here 0x0a
	0x06, 0x00, 0xff, 0x09, 0x60, // here 0x0a
	0x75, 0x08, 0x95, 0x3f, 0x26,
	0xff, 0x7f, 0x15, 0x00, 0x46,
	0xff, 0x7f, 0x36, 0x00, 0x80,
	0x91, 0x02, 0x85, 0x02, 0x09,
	0x02, 0x81, 0x02, 0x09, 0x14,
	0x85, 0x14, 0x81, 0x02, 0xc0,
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
