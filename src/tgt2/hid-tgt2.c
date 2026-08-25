// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for the Thrustmaster T-GT II. Shares the PID and FFB protocol with
 * the T300RS, so only the report descriptor fixup and rotary encoder parsing
 * are specific to this wheel. Enabled with the tgt2_force module parameter.
 */

#include <linux/hid.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/usb.h>

#include "../hid-tmff2.h"

#define TGT2_INPUT_REPORT_ID	0x01
#define TGT2_INPUT_REPORT_SIZE	64
#define TGT2_DIAL_BYTE		54
#define TGT2_DIAL_COUNT		4
#define TGT2_DIAL_HOLD_JIFFIES	msecs_to_jiffies(50)

/* rotary encoder buttons in wire order (TL, TR, BL, BR):
 * push, clockwise turn, counter-clockwise turn */
static const int tgt2_dial_push_codes[TGT2_DIAL_COUNT] = {
	BTN_TRIGGER_HAPPY1, BTN_TRIGGER_HAPPY2,
	BTN_TRIGGER_HAPPY3, BTN_TRIGGER_HAPPY4,
};
static const int tgt2_dial_cw_codes[TGT2_DIAL_COUNT] = {
	BTN_TRIGGER_HAPPY5, BTN_TRIGGER_HAPPY6,
	BTN_TRIGGER_HAPPY7, BTN_TRIGGER_HAPPY8,
};
static const int tgt2_dial_ccw_codes[TGT2_DIAL_COUNT] = {
	BTN_TRIGGER_HAPPY9, BTN_TRIGGER_HAPPY10,
	BTN_TRIGGER_HAPPY11, BTN_TRIGGER_HAPPY12,
};

struct tgt2_dial_state {
	u8 last_pos[TGT2_DIAL_COUNT];
	bool last_push[TGT2_DIAL_COUNT];
	/* detent holds, released from the ~4 ms report stream */
	unsigned long cw_deadline[TGT2_DIAL_COUNT];
	unsigned long ccw_deadline[TGT2_DIAL_COUNT];
	bool initialized;
};

/*
 * Layout follows the wheel's actual report: bytes 1-4 ministicks (Rx/Ry and
 * simulation Rudder/Throttle, since X/Y/Z/Rz are taken by the axes below),
 * byte 5 D-pad and face buttons, bytes 6-7 buttons, bytes 8-9 analog L2/R2
 * (simulation Brake/Accelerator), bytes 10-42 vendor, bytes 43-50 the 16-bit
 * wheel and pedal axes, bytes 51-63 vendor. Note the pedal order: the T-GT II
 * sends clutch at 45-46 and throttle at 49-50, opposite of the T300RS.
 * The FFB output and feature reports are identical to the T300RS ones.
 */
static u8 tgt2_rdesc_fixed[] = {
	0x05, 0x01,       /* Usage Page (Generic Desktop) */
	0x09, 0x05,       /* Usage (Gamepad) */
	0xa1, 0x01,       /* Collection (Application) */
	0x85, 0x01,       /* Report ID (1) */
	/* ministicks: bytes 1-2 left stick, bytes 3-4 right stick */
	0x05, 0x01,       /* Usage Page (Generic Desktop) */
	0x09, 0x33,       /* Usage (Rx) */
	0x09, 0x34,       /* Usage (Ry) */
	0x15, 0x00,       /* Logical Minimum (0) */
	0x26, 0xff, 0x00, /* Logical Maximum (255) */
	0x75, 0x08,       /* Report Size (8) */
	0x95, 0x02,       /* Report Count (2) */
	0x81, 0x02,       /* Input (Data,Var,Abs) */
	0x05, 0x02,       /* Usage Page (Simulation Controls) */
	0x09, 0xba,       /* Usage (Rudder) */
	0x09, 0xbb,       /* Usage (Throttle) */
	0x75, 0x08,       /* Report Size (8) */
	0x95, 0x02,       /* Report Count (2) */
	0x81, 0x02,       /* Input (Data,Var,Abs) */
	0x05, 0x01,       /* Usage Page (Generic Desktop) */
	/* byte 5 low nibble: D-pad */
	0x09, 0x39,       /* Usage (Hat Switch) */
	0x15, 0x00,       /* Logical Minimum (0) */
	0x25, 0x07,       /* Logical Maximum (7) */
	0x35, 0x00,       /* Physical Minimum (0) */
	0x46, 0x3b, 0x01, /* Physical Maximum (315) */
	0x65, 0x14,       /* Unit (Eng Rot) */
	0x75, 0x04,       /* Report Size (4) */
	0x95, 0x01,       /* Report Count (1) */
	0x81, 0x42,       /* Input (Data,Var,Abs,Null) */
	0x65, 0x00,       /* Unit (None) */
	/* bytes 5 (high nibble) - 7: buttons 1-14 */
	0x05, 0x09,       /* Usage Page (Button) */
	0x19, 0x01,       /* Usage Minimum (1) */
	0x29, 0x0e,       /* Usage Maximum (14) */
	0x15, 0x00,       /* Logical Minimum (0) */
	0x25, 0x01,       /* Logical Maximum (1) */
	0x75, 0x01,       /* Report Size (1) */
	0x95, 0x0e,       /* Report Count (14) */
	0x81, 0x02,       /* Input (Data,Var,Abs) */
	/* byte 7 bits 2-7: vendor */
	0x06, 0x00, 0xff, /* Usage Page (Vendor 0xff00) */
	0x09, 0x20,       /* Usage (0x20) */
	0x75, 0x06,       /* Report Size (6) */
	0x95, 0x01,       /* Report Count (1) */
	0x81, 0x02,       /* Input (Data,Var,Abs) */
	/* bytes 8-9: analog L2/R2 */
	0x05, 0x02,       /* Usage Page (Simulation Controls) */
	0x09, 0xc5,       /* Usage (Brake) */
	0x09, 0xc4,       /* Usage (Accelerator) */
	0x15, 0x00,       /* Logical Minimum (0) */
	0x26, 0xff, 0x00, /* Logical Maximum (255) */
	0x75, 0x08,       /* Report Size (8) */
	0x95, 0x02,       /* Report Count (2) */
	0x81, 0x02,       /* Input (Data,Var,Abs) */
	/* bytes 10-42: vendor */
	0x06, 0x00, 0xff, /* Usage Page (Vendor 0xff00) */
	0x09, 0x21,       /* Usage (0x21) */
	0x75, 0x08,       /* Report Size (8) */
	0x95, 0x21,       /* Report Count (33) */
	0x81, 0x02,       /* Input (Data,Var,Abs) */
	/* bytes 43-50: wheel and pedals */
	0x05, 0x01,       /* Usage Page (Generic Desktop) */
	0x09, 0x30,       /* Usage (X) - steering */
	0x15, 0x00,       /* Logical Minimum (0) */
	0x27, 0xff, 0xff, 0x00, 0x00, /* Logical Maximum (65535) */
	0x35, 0x00,       /* Physical Minimum (0) */
	0x47, 0xff, 0xff, 0x00, 0x00, /* Physical Maximum (65535) */
	0x75, 0x10,       /* Report Size (16) */
	0x95, 0x01,       /* Report Count (1) */
	0x81, 0x02,       /* Input (Data,Var,Abs) */
	0x09, 0x35,       /* Usage (Rz) - clutch */
	0x81, 0x02,       /* Input (Data,Var,Abs) */
	0x09, 0x32,       /* Usage (Z) - brake */
	0x81, 0x02,       /* Input (Data,Var,Abs) */
	0x09, 0x31,       /* Usage (Y) - throttle */
	0x81, 0x02,       /* Input (Data,Var,Abs) */
	/* bytes 51-63: vendor, includes the rotary encoders */
	0x06, 0x00, 0xff, /* Usage Page (Vendor 0xff00) */
	0x09, 0x21,       /* Usage (0x21) */
	0x75, 0x08,       /* Report Size (8) */
	0x95, 0x0d,       /* Report Count (13) */
	0x81, 0x02,       /* Input (Data,Var,Abs) */
	/* FFB output and feature reports, same as T300RS */
	0x06, 0x00, 0xff, /* Usage Page (Vendor 0xff00) */
	0x85, 0x60,       /* Report ID (96) */
	0x09, 0x60,       /* Usage (0x60) */
	0x95, 0x1f,       /* Report Count (31) */
	0x91, 0x02,       /* Output (Data,Var,Abs) */
	0x85, 0x03,       /* Report ID (3) */
	0x0a, 0x21, 0x27, /* Usage (0xff002721) */
	0x95, 0x2f,       /* Report Count (47) */
	0xb1, 0x02,       /* Feature (Data,Var,Abs) */
	0xc0,             /* End Collection */

	0x06, 0xf0, 0xff, /* Usage Page (Vendor 0xfff0) */
	0x09, 0x40,       /* Usage (0x40) */
	0xa1, 0x01,       /* Collection (Application) */
	0x85, 0xf0,       /* Report ID (240) */
	0x09, 0x47,       /* Usage (0x47) */
	0x95, 0x3f,       /* Report Count (63) */
	0xb1, 0x02,       /* Feature (Data,Var,Abs) */
	0x85, 0xf1,       /* Report ID (241) */
	0x09, 0x48,       /* Usage (0x48) */
	0x95, 0x3f,       /* Report Count (63) */
	0xb1, 0x02,       /* Feature (Data,Var,Abs) */
	0x85, 0xf2,       /* Report ID (242) */
	0x09, 0x49,       /* Usage (0x49) */
	0x95, 0x0f,       /* Report Count (15) */
	0xb1, 0x02,       /* Feature (Data,Var,Abs) */
	0x85, 0xf3,       /* Report ID (243) */
	0x0a, 0x01, 0x47, /* Usage (0xff470001) */
	0x95, 0x07,       /* Report Count (7) */
	0xb1, 0x02,       /* Feature (Data,Var,Abs) */
	0xc0,             /* End Collection */
};

static __u8 *tgt2_wheel_fixup(struct hid_device *hdev, __u8 *rdesc,
		unsigned int *rsize)
{
	*rsize = sizeof(tgt2_rdesc_fixed);
	return tgt2_rdesc_fixed;
}

/*
 * The four rotary encoders report absolute 7-bit position counters (mod 128)
 * with the push flag in bit 7. Since the counters wrap, they cannot be
 * exposed as axes; report each detent as a short button hold instead.
 */
static int tgt2_raw_event(struct hid_device *hdev, __u8 *data, int size)
{
	struct tmff2_device_entry *tmff2 = hid_get_drvdata(hdev);
	struct tgt2_dial_state *dials;
	struct input_dev *input;
	unsigned long now = jiffies;
	int i, delta;
	bool push, changed = false;

	if (!tmff2)
		return 0;

	dials = tmff2->raw_event_data;
	input = tmff2->input_dev;

	if (!dials || !input)
		return 0;

	if (size < TGT2_INPUT_REPORT_SIZE || data[0] != TGT2_INPUT_REPORT_ID)
		return 0;

	for (i = 0; i < TGT2_DIAL_COUNT; i++) {
		u8 raw = data[TGT2_DIAL_BYTE + i];
		u8 pos = raw & 0x7f;

		push = raw & 0x80;

		if (!dials->initialized) {
			dials->last_pos[i] = pos;
			dials->last_push[i] = push;
			continue;
		}

		delta = ((pos - dials->last_pos[i] + 64) & 0x7f) - 64;

		if (delta > 0) {
			if (!dials->cw_deadline[i])
				input_report_key(input, tgt2_dial_cw_codes[i], 1);
			dials->cw_deadline[i] = now + TGT2_DIAL_HOLD_JIFFIES;
			changed = true;
		} else if (delta < 0) {
			if (!dials->ccw_deadline[i])
				input_report_key(input, tgt2_dial_ccw_codes[i], 1);
			dials->ccw_deadline[i] = now + TGT2_DIAL_HOLD_JIFFIES;
			changed = true;
		}

		if (dials->cw_deadline[i] &&
		    time_after_eq(now, dials->cw_deadline[i])) {
			input_report_key(input, tgt2_dial_cw_codes[i], 0);
			dials->cw_deadline[i] = 0;
			changed = true;
		}
		if (dials->ccw_deadline[i] &&
		    time_after_eq(now, dials->ccw_deadline[i])) {
			input_report_key(input, tgt2_dial_ccw_codes[i], 0);
			dials->ccw_deadline[i] = 0;
			changed = true;
		}

		if (push != dials->last_push[i]) {
			input_report_key(input, tgt2_dial_push_codes[i], push);
			changed = true;
		}

		dials->last_pos[i] = pos;
		dials->last_push[i] = push;
	}

	if (!dials->initialized)
		dials->initialized = true;
	else if (changed)
		input_sync(input);

	return 0;
}

static int tgt2_wheel_init(struct tmff2_device_entry *tmff2, int open_mode)
{
	struct tgt2_dial_state *dials;
	struct input_dev *input = tmff2->input_dev;
	int ret, i;

	dials = devm_kzalloc(&tmff2->hdev->dev, sizeof(*dials), GFP_KERNEL);
	if (!dials)
		return -ENOMEM;

	if ((ret = t300rs_wheel_init(tmff2, open_mode)))
		return ret;

	for (i = 0; i < TGT2_DIAL_COUNT; i++) {
		__set_bit(tgt2_dial_push_codes[i], input->keybit);
		__set_bit(tgt2_dial_cw_codes[i], input->keybit);
		__set_bit(tgt2_dial_ccw_codes[i], input->keybit);
	}

	tmff2->raw_event_data = dials;

	hid_info(tmff2->hdev, "T-GT II: dials and extended inputs enabled\n");
	return 0;
}

int tgt2_populate_api(struct tmff2_device_entry *tmff2)
{
	/* the T-GT II shares the T300RS FFB protocol */
	t300rs_populate_api(tmff2);

	tmff2->wheel_init = tgt2_wheel_init;
	tmff2->wheel_fixup = tgt2_wheel_fixup;
	tmff2->raw_event = tgt2_raw_event;

	return 0;
}
