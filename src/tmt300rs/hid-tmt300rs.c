// SPDX-License-Identifier: GPL-2.0-or-later
#include <linux/usb.h>
#include <linux/hid.h>
#include "../hid-tmff2.h"

#define T300RS_MAX_EFFECTS 16
#define T300RS_NORM_BUFFER_LENGTH 63
#define T300RS_PS4_BUFFER_LENGTH 31

#define T300RS_DEFAULT_ATTACHMENT 0x06
#define T300RS_F1_ATTACHMENT 0x03

static const unsigned long t300rs_params =
	PARAM_SPRING_LEVEL
	| PARAM_DAMPER_LEVEL
	| PARAM_FRICTION_LEVEL
	| PARAM_GAIN
	| PARAM_RANGE
	| PARAM_ALT_MODE
	;

static const signed short t300rs_effects[] = {
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

struct __packed t300rs_fw_response {
	uint8_t unused1[2];
	uint8_t fw_version;
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

struct usb_ctrlrequest t300rs_fw_request = {
	.bRequestType = 0xc1,
	.bRequest = 86,
	.wValue = 0,
	.wIndex = 0,
	.wLength = 8
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

static void t300rs_calculate_periodic_values(struct ff_effect *effect)
{
	struct ff_periodic_effect *periodic = &effect->u.periodic;
	int16_t headroom;

	effect->replay.length -= 1;

	periodic->magnitude = (periodic->magnitude * fixp_sin16(effect->direction * 360 / 0x10000)) / 0x7fff;

	if (periodic->magnitude < 0){
		/* the wheel handles positive magnitudes only */
		periodic->magnitude = -periodic->magnitude;

		/* to give the expected result 180 deg is added to the phase */
		periodic->phase = (periodic->phase + (0x10000 / 2)) % 0x10000;
	}

	/* the interval [0; 32677[ is used by the wheel for the [0; 360[ degree phase shift */
	periodic->phase = periodic->phase * 32677 / 0x10000;

	headroom = 0x7FFF - periodic->magnitude;
	/* magnitude + offset cannot be outside the valid magnitude range, */
	/* otherwise the wheel behaves incorrectly */
	periodic->offset = clamp(periodic->offset, -headroom, headroom);
}

int t300rs_send_buf(struct t300rs_device_entry *t300rs, u8 *send_buffer, size_t len)
{
	int i;
	/* check that send_buffer fits into our report */
	if (len > t300rs->buffer_length)
		return -EINVAL;

	/* fill with actual data */
	for (i = 0; i < len; ++i)
		t300rs->ff_field->value[i] = send_buffer[i];

	/* fill the rest with zeroes */
	for (i = len; i < t300rs->buffer_length; ++i)
		t300rs->ff_field->value[i] = 0;

	hid_hw_request(t300rs->hdev, t300rs->report, HID_REQ_SET_REPORT);
	return 0;
}

int t300rs_send_int(struct t300rs_device_entry *t300rs)
{
	t300rs_send_buf(t300rs, t300rs->send_buffer, t300rs->buffer_length);
	memset(t300rs->send_buffer, 0, t300rs->buffer_length);

	return 0;
}

static void t300rs_fill_header(struct t300rs_packet_header *packet_header,
		uint8_t id, uint8_t code)
{
	packet_header->id = id + 1;
	packet_header->code = code;
}

int t300rs_play_effect(void *data, struct tmff2_effect_state *state)
{
	struct t300rs_device_entry *t300rs = data;
	struct __packed t300rs_packet_play {
		struct t300rs_packet_header header;
		uint8_t code;
		uint16_t count;
	} *play_packet = (struct t300rs_packet_play *)t300rs->send_buffer;

	int ret;


	t300rs_fill_header(&play_packet->header, state->effect.id, 0x89);
	play_packet->code = 0x41;

	if (state->count == 0 || state->count >= 65535)
		play_packet->count = 0;
	else
		play_packet->count = cpu_to_le16(state->count);

	ret = t300rs_send_int(t300rs);
	if (ret)
		hid_err(t300rs->hdev, "failed starting effect play\n");

	return ret;
}

int t300rs_stop_effect(void *data, struct tmff2_effect_state *state)
{
	struct t300rs_device_entry *t300rs = data;
	struct __packed t300rs_packet_stop {
		struct t300rs_packet_header header;
		uint8_t value;
	} *stop_packet = (struct t300rs_packet_stop *)t300rs->send_buffer;

	int ret;


	t300rs_fill_header(&stop_packet->header, state->effect.id, 0x89);

	ret = t300rs_send_int(t300rs);
	if (ret)
		hid_err(t300rs->hdev, "failed stopping effect play\n");

	return ret;
}

static void t300rs_fill_envelope(struct t300rs_packet_envelope *packet_envelope,
		int16_t level, uint16_t duration, struct ff_envelope *envelope)
{
	uint16_t attack_length = (duration * envelope->attack_length) / 0x7fff;
	uint16_t attack_level = (level * envelope->attack_level) / 0x7fff;
	uint16_t fade_length = (duration * envelope->fade_length) / 0x7fff;
	uint16_t fade_level = (level * envelope->fade_level) / 0x7fff;

	packet_envelope->attack_length = cpu_to_le16(attack_length);
	packet_envelope->attack_level = cpu_to_le16(attack_level);
	packet_envelope->fade_length = cpu_to_le16(fade_length);
	packet_envelope->fade_level = cpu_to_le16(fade_level);
}

static void t300rs_fill_timing(struct t300rs_packet_timing *packet_timing,
		uint16_t duration, uint16_t offset){
	packet_timing->start_marker = 0x4f;

	packet_timing->duration = cpu_to_le16(duration);
	packet_timing->offset = cpu_to_le16(offset);

	packet_timing->end_marker = 0xffff;
}

static int t300rs_update_envelope(struct t300rs_device_entry *t300rs,
		struct tmff2_effect_state *state,
		int16_t level,
		uint16_t duration,
		uint8_t id,
		struct ff_envelope envelope,
		struct ff_envelope envelope_old
		)
{
	struct __packed t300rs_packet_mod_envelope {
		struct t300rs_packet_header header;
		uint8_t attribute;
		uint16_t value;
	} *packet_mod_envelope = (struct t300rs_packet_mod_envelope *)t300rs->send_buffer;

	uint16_t attack_length, attack_level, fade_length, fade_level;
	int ret = 0;

	duration = duration - 1;

	attack_length = (duration * envelope.attack_length) / 0x7fff;
	attack_level = (level * envelope.attack_level) / 0x7fff;
	fade_length = (duration * envelope.fade_length) / 0x7fff;
	fade_level = (level * envelope.fade_level) / 0x7fff;

	if (envelope.attack_length != envelope_old.attack_length) {
		t300rs_fill_header(&packet_mod_envelope->header, id, 0x31);
		packet_mod_envelope->attribute = 0x81;
		packet_mod_envelope->value = cpu_to_le16(attack_length);

		ret = t300rs_send_int(t300rs);
		if (ret) {
			hid_err(t300rs->hdev, "failed modifying effect envelope\n");
			goto error;
		}
	}

	if (envelope.attack_level != envelope_old.attack_level) {
		t300rs_fill_header(&packet_mod_envelope->header, id, 0x31);
		packet_mod_envelope->attribute = 0x82;
		packet_mod_envelope->value = cpu_to_le16(attack_level);

		ret = t300rs_send_int(t300rs);
		if (ret) {
			hid_err(t300rs->hdev, "failed modifying effect envelope\n");
			goto error;
		}
	}

	if (envelope.fade_length != envelope_old.fade_length) {
		t300rs_fill_header(&packet_mod_envelope->header, id, 0x31);
		packet_mod_envelope->attribute = 0x84;
		packet_mod_envelope->value = cpu_to_le16(fade_length);

		ret = t300rs_send_int(t300rs);
		if (ret) {
			hid_err(t300rs->hdev, "failed modifying effect envelope\n");
			goto error;
		}
	}

	if (envelope.fade_level != envelope_old.fade_level) {
		t300rs_fill_header(&packet_mod_envelope->header, id, 0x31);
		packet_mod_envelope->attribute = 0x88;
		packet_mod_envelope->value = cpu_to_le16(fade_level);

		ret = t300rs_send_int(t300rs);
		if (ret) {
			hid_err(t300rs->hdev, "failed modifying effect envelope\n");
			goto error;
		}
	}

error:
	return ret;
}

static int t300rs_update_simple_duration(struct t300rs_device_entry *t300rs,
		struct tmff2_effect_state *state, unsigned type)
{
	struct ff_effect effect = state->effect;
	struct __packed t300rs_packet_mod_duration {
		struct t300rs_packet_header header;
		uint16_t marker;
		uint16_t duration;
	} *packet_mod_duration = (struct t300rs_packet_mod_duration *)t300rs->send_buffer;

	uint16_t duration;
	int ret = 0;

	duration = effect.replay.length - 1;

	t300rs_fill_header(&packet_mod_duration->header, effect.id, 0x49);
	packet_mod_duration->marker = cpu_to_le16(0x4100 + type);
	packet_mod_duration->duration = cpu_to_le16(duration);

	ret = t300rs_send_int(t300rs);
	if (ret) {
		hid_err(t300rs->hdev, "failed modifying duration\n");
		goto error;
	}

error:
	return ret;
}

static int t300rs_update_periodic_duration(struct t300rs_device_entry *t300rs,
		struct tmff2_effect_state *state, unsigned type)
{
	struct ff_effect effect = state->effect;
	struct __packed t300rs_packet_mod_duration {
		struct t300rs_packet_header header;
		uint8_t type;
		uint8_t marker;
		uint16_t duration;
	} *packet_mod_duration = (struct t300rs_packet_mod_duration *)t300rs->send_buffer;

	uint16_t duration;
	int ret = 0;

	duration = effect.replay.length - 1;

	t300rs_fill_header(&packet_mod_duration->header, effect.id, 0x49);
	packet_mod_duration->type = type;
	packet_mod_duration->marker = 0x41;
	packet_mod_duration->duration = cpu_to_le16(duration);

	ret = t300rs_send_int(t300rs);
	if (ret) {
		hid_err(t300rs->hdev, "failed modifying duration\n");
		goto error;
	}

error:
	return ret;
}

static int t300rs_update_ramp_duration(struct t300rs_device_entry *t300rs,
		struct tmff2_effect_state *state)
{
	struct ff_effect effect = state->effect;
	struct __packed t300rs_packet_mod_duration {
		struct t300rs_packet_header header;
		uint8_t marker0;
		uint16_t duration0;
		uint8_t marker1;
		uint8_t marker2;
		uint16_t duration1;
	} *packet_mod_duration = (struct t300rs_packet_mod_duration *)t300rs->send_buffer;

	uint16_t duration;
	int ret = 0;

	duration = effect.replay.length - 1;

	t300rs_fill_header(&packet_mod_duration->header, effect.id, 0x4e);
	packet_mod_duration->marker0 = 0x08;
	packet_mod_duration->duration0 = cpu_to_le16(duration);
	packet_mod_duration->marker1 = 0x05;
	packet_mod_duration->marker2 = 0x41;
	packet_mod_duration->duration1 = cpu_to_le16(duration);

	ret = t300rs_send_int(t300rs);
	if (ret) {
		hid_err(t300rs->hdev, "failed modifying duration\n");
		goto error;
	}

error:
	return ret;
}

static int t300rs_update_constant(struct t300rs_device_entry *t300rs,
		struct tmff2_effect_state *state)
{
	struct ff_effect effect = state->effect;
	struct ff_effect old = state->old;
	struct ff_constant_effect constant = effect.u.constant;
	struct ff_constant_effect constant_old = old.u.constant;
	struct __packed t300rs_packet_mod_constant {
		struct t300rs_packet_header header;
		uint16_t level;
	} *packet_mod_constant = (struct t300rs_packet_mod_constant *)t300rs->send_buffer;
	int ret;
	int16_t level;

	level = (constant.level * fixp_sin16(effect.direction * 360 / 0x10000)) / 0x7fff;
	/* the Windows driver uses the range [-16385;16381] */
	level = level / 2;

	if ((constant.level != constant_old.level) || (effect.direction != old.direction)) {

		t300rs_fill_header(&packet_mod_constant->header, effect.id, 0x0a);
		packet_mod_constant->level = cpu_to_le16(level);

		ret = t300rs_send_int(t300rs);
		if (ret) {
			hid_err(t300rs->hdev, "failed modifying constant effect\n");
			goto error;
		}

	}

	ret = t300rs_update_envelope(t300rs,
			state,
			level,
			effect.replay.length,
			effect.id,
			constant.envelope,
			constant_old.envelope
			);

	if (ret) {
		hid_err(t300rs->hdev, "failed modifying constant envelope\n");
		goto error;
	}

	ret = t300rs_update_simple_duration(t300rs, state, 0x00);
	if (ret) {
		hid_err(t300rs->hdev, "failed modifying constant duration\n");
		goto error;
	}

error:
	return ret;
}

static int t300rs_update_ramp(struct t300rs_device_entry *t300rs,
		struct tmff2_effect_state *state)
{
	struct ff_effect effect = state->effect;
	struct ff_effect old = state->old;
	struct ff_ramp_effect ramp = effect.u.ramp;
	struct ff_ramp_effect ramp_old = old.u.ramp;
	struct __packed t300rs_packet_mod_ramp {
		struct t300rs_packet_header header;
		uint8_t attribute;
		uint16_t difference;
		uint16_t level;
	} *packet_mod_ramp = (struct t300rs_packet_mod_ramp *)t300rs->send_buffer;

	int ret;

	uint16_t difference, top, bottom;
	int16_t level;

	top = ramp.end_level > ramp.start_level ? ramp.end_level : ramp.start_level;
	bottom = ramp.end_level > ramp.start_level ? ramp.start_level : ramp.end_level;


	difference = ((top - bottom) * fixp_sin16(effect.direction * 360 / 0x10000)) / 0x7fff;


	level = (top * fixp_sin16(effect.direction * 360 / 0x10000)) / 0x7fff;

	if ((ramp.start_level != ramp_old.start_level)
			|| (ramp.end_level != ramp_old.end_level)
			|| (effect.direction != old.direction)) {

		t300rs_fill_header(&packet_mod_ramp->header, effect.id, 0x0e);
		packet_mod_ramp->attribute = 0x03;
		packet_mod_ramp->difference = cpu_to_le16(difference);
		packet_mod_ramp->level = cpu_to_le16(level);

		ret = t300rs_send_int(t300rs);
		if (ret) {
			hid_err(t300rs->hdev, "failed modifying ramp effect\n");
			goto error;
		}

	}

	ret = t300rs_update_envelope(t300rs,
			state,
			level,
			effect.replay.length,
			effect.id,
			ramp.envelope,
			ramp_old.envelope
			);


	if (ret) {
		hid_err(t300rs->hdev, "failed modifying ramp envelope\n");
		goto error;
	}

	ret = t300rs_update_ramp_duration(t300rs, state);
	if (ret) {
		hid_err(t300rs->hdev, "failed modifying ramp duration\n");
		goto error;
	}

error:
	return ret;
}

static int t300rs_update_damper(struct t300rs_device_entry *t300rs,
		struct tmff2_effect_state *state)
{
	struct ff_effect effect = state->effect;
	struct ff_effect old = state->old;
	struct ff_condition_effect damper = effect.u.condition[0];
	struct ff_condition_effect damper_old = old.u.condition[0];
	struct __packed t300rs_packet_mod_damper {
		struct t300rs_packet_header header;
		uint8_t attribute;
		uint16_t value0;
		uint16_t value1;
	} *packet_mod_damper = (struct t300rs_packet_mod_damper *)t300rs->send_buffer;

	int ret, input_level;

	input_level = damper_level;
	if (state->effect.type == FF_FRICTION)
		input_level = friction_level;

	if (state->effect.type == FF_SPRING)
		input_level = spring_level;

	if (damper.right_coeff != damper_old.right_coeff) {
		int16_t coeff = damper.right_coeff * input_level / 100;

		t300rs_fill_header(&packet_mod_damper->header, effect.id, 0x0e);
		packet_mod_damper->attribute = 0x41;
		packet_mod_damper->value0 = cpu_to_le16(coeff);

		ret = t300rs_send_int(t300rs);
		if (ret) {
			hid_err(t300rs->hdev, "failed modifying damper rc\n");
			goto error;
		}

	}

	if (damper.left_coeff != damper_old.left_coeff) {
		int16_t coeff = damper.left_coeff * input_level / 100;

		t300rs_fill_header(&packet_mod_damper->header, effect.id, 0x0e);
		packet_mod_damper->attribute = 0x42;
		packet_mod_damper->value0 = cpu_to_le16(coeff);

		ret = t300rs_send_int(t300rs);
		if (ret) {
			hid_err(t300rs->hdev, "failed modifying damper lc\n");
			goto error;
		}

	}

	if ((damper.deadband != damper_old.deadband)
			|| (damper.center != damper_old.center)) {

		uint16_t right_deadband = 0xfffe - damper.deadband - damper.center;
		uint16_t left_deadband = 0xfffe - damper.deadband + damper.center;

		t300rs_fill_header(&packet_mod_damper->header, effect.id, 0x0e);
		packet_mod_damper->attribute = 0x4c;
		packet_mod_damper->value0 = cpu_to_le16(right_deadband);
		packet_mod_damper->value1 = cpu_to_le16(left_deadband);

		ret = t300rs_send_int(t300rs);
		if (ret) {
			hid_err(t300rs->hdev, "failed modifying damper deadband\n");
			goto error;
		}

	}

	ret = t300rs_update_simple_duration(t300rs, state, 0x06);
	if (ret) {
		hid_err(t300rs->hdev, "failed modifying damper duration\n");
		goto error;
	}

error:
	return ret;
}

static int t300rs_update_spring(struct t300rs_device_entry *t300rs,
		struct tmff2_effect_state *state)
{
	return t300rs_update_damper(t300rs, state);
}

static int t300rs_update_periodic(struct t300rs_device_entry *t300rs,
		struct tmff2_effect_state *state)
{
	struct ff_effect effect = state->effect;
	struct ff_effect old = state->old;
	struct __packed t300rs_packet_mod_periodic {
		struct t300rs_packet_header header;
		uint8_t attribute;
		uint16_t value;
	} *packet_mod_periodic = (struct t300rs_packet_mod_periodic *)t300rs->send_buffer;

	struct ff_periodic_effect periodic, periodic_old;
	int ret;
	uint16_t phase;
	int16_t magnitude;

	t300rs_calculate_periodic_values(&effect);
	periodic = effect.u.periodic;
	magnitude = periodic.magnitude;
	phase = periodic.phase;

	t300rs_calculate_periodic_values(&old);
	periodic_old = old.u.periodic;

	if ((periodic.magnitude != periodic_old.magnitude)
			|| (effect.direction != old.direction)) {

		t300rs_fill_header(&packet_mod_periodic->header, effect.id, 0x0e);
		packet_mod_periodic->attribute = 0x01;
		packet_mod_periodic->value = cpu_to_le16(magnitude);

		ret = t300rs_send_int(t300rs);
		if (ret) {
			hid_err(t300rs->hdev, "failed modifying periodic magnitude\n");
			goto error;
		}

	}

	if (periodic.offset != periodic_old.offset) {
		uint16_t offset = periodic.offset;

		t300rs_fill_header(&packet_mod_periodic->header, effect.id, 0x0e);
		packet_mod_periodic->attribute = 0x02;
		packet_mod_periodic->value = cpu_to_le16(offset);

		ret = t300rs_send_int(t300rs);
		if (ret) {
			hid_err(t300rs->hdev, "failed modifying periodic offset\n");
			goto error;
		}

	}

	if (periodic.phase != periodic_old.phase) {

		t300rs_fill_header(&packet_mod_periodic->header, effect.id, 0x0e);
		packet_mod_periodic->attribute = 0x04;
		packet_mod_periodic->value = cpu_to_le16(phase);

		ret = t300rs_send_int(t300rs);
		if (ret) {
			hid_err(t300rs->hdev, "failed modifying periodic phase\n");
			goto error;
		}

	}

	if (periodic.period != periodic_old.period) {
		int16_t period = periodic.period;

		t300rs_fill_header(&packet_mod_periodic->header, effect.id, 0x0e);
		packet_mod_periodic->attribute = 0x08;
		packet_mod_periodic->value = cpu_to_le16(period);

		ret = t300rs_send_int(t300rs);
		if (ret) {
			hid_err(t300rs->hdev, "failed modifying periodic period\n");
			goto error;
		}

	}

	ret = t300rs_update_envelope(t300rs,
			state,
			magnitude,
			effect.replay.length,
			effect.id,
			periodic.envelope,
			periodic_old.envelope
			);

	if (ret) {
		hid_err(t300rs->hdev, "failed modifying periodic envelope\n");
		goto error;
	}

	ret = t300rs_update_periodic_duration(t300rs, state, periodic.waveform - 0x57);
	if (ret) {
		hid_err(t300rs->hdev, "failed modifying periodic duration\n");
		goto error;
	}

error:
	return ret;
}

static int t300rs_upload_constant(struct t300rs_device_entry *t300rs,
		struct tmff2_effect_state *state)
{
	struct ff_effect effect = state->effect;
	struct ff_constant_effect constant = state->effect.u.constant;
	struct __packed t300rs_packet_constant {
		struct t300rs_packet_header header;
		uint16_t level;
		struct t300rs_packet_envelope envelope;
		uint8_t zero;
		struct t300rs_packet_timing timing;
	} *packet_constant = (struct t300rs_packet_constant *)t300rs->send_buffer;

	int16_t level;
	uint16_t duration, offset;

	int ret;

	level = (constant.level * fixp_sin16(effect.direction * 360 / 0x10000)) / 0x7fff;
	/* the Windows driver uses the range [-16385;16381] */
	level = level / 2;
	duration = effect.replay.length - 1;

	offset = effect.replay.delay;

	t300rs_fill_header(&packet_constant->header, effect.id, 0x6a);

	packet_constant->level = cpu_to_le16(level);

	t300rs_fill_envelope(&packet_constant->envelope, level, duration,
			&constant.envelope);
	t300rs_fill_timing(&packet_constant->timing, duration, offset);

	ret = t300rs_send_int(t300rs);
	if (ret)
		hid_err(t300rs->hdev, "failed uploading constant effect\n");

	return ret;
}

static int t300rs_upload_ramp(struct t300rs_device_entry *t300rs,
		struct tmff2_effect_state *state)
{
	struct ff_effect effect = state->effect;
	struct ff_ramp_effect ramp = state->effect.u.ramp;
	struct __packed t300rs_packet_ramp {
		struct t300rs_packet_header header;
		uint16_t difference;
		uint16_t level;
		uint8_t zero1[2];
		uint16_t duration;
		uint16_t marker;
		struct t300rs_packet_envelope envelope;
		uint8_t direction;
		struct t300rs_packet_timing timing;
	} *packet_ramp = (struct t300rs_packet_ramp *)t300rs->send_buffer;

	int ret;
	uint16_t difference, offset, top, bottom, duration;
	int16_t level;

	duration = effect.replay.length - 1;

	top = ramp.end_level > ramp.start_level ? ramp.end_level : ramp.start_level;
	bottom = ramp.end_level > ramp.start_level ? ramp.start_level : ramp.end_level;


	difference = ((top - bottom) * fixp_sin16(effect.direction * 360 / 0x10000)) / 0x7fff;
	level = (top * fixp_sin16(effect.direction * 360 / 0x10000)) / 0x7fff;
	offset = effect.replay.delay;


	t300rs_fill_header(&packet_ramp->header, effect.id, 0x6b);

	packet_ramp->difference = cpu_to_le16(difference);
	packet_ramp->level = cpu_to_le16(level);
	packet_ramp->duration = cpu_to_le16(duration);

	packet_ramp->marker = cpu_to_le16(0x8000);

	t300rs_fill_envelope(&packet_ramp->envelope, level, duration,
			&ramp.envelope);

	packet_ramp->direction = ramp.end_level > ramp.start_level ? 0x04 : 0x05;
	t300rs_fill_timing(&packet_ramp->timing, duration, offset);

	ret = t300rs_send_int(t300rs);
	if (ret)
		hid_err(t300rs->hdev, "failed uploading ramp");

	return ret;
}

static int t300rs_upload_spring(struct t300rs_device_entry *t300rs,
		struct tmff2_effect_state *state)
{
	struct ff_effect effect = state->effect;
	/* we only care about the first axis */
	struct ff_condition_effect spring = state->effect.u.condition[0];
	struct __packed t300rs_packet_spring {
		struct t300rs_packet_header header;
		uint16_t right_coeff;
		uint16_t left_coeff;
		uint16_t right_deadband;
		uint16_t left_deadband;
		uint8_t spring_start[17];
		struct t300rs_packet_timing timing;
	} *packet_spring = (struct t300rs_packet_spring *)t300rs->send_buffer;

	int ret;
	uint16_t duration, right_coeff, left_coeff, right_deadband, left_deadband, offset;

	duration = effect.replay.length - 1;

	right_coeff = spring.right_coeff * spring_level / 100;
	left_coeff = spring.left_coeff * spring_level / 100;

	right_deadband = 0xfffe - spring.deadband - spring.center;
	left_deadband = 0xfffe - spring.deadband + spring.center;

	offset = effect.replay.delay;

	t300rs_fill_header(&packet_spring->header, effect.id, 0x64);

	packet_spring->right_coeff = cpu_to_le16(right_coeff);
	packet_spring->left_coeff = cpu_to_le16(left_coeff);
	packet_spring->right_deadband = cpu_to_le16(right_deadband);
	packet_spring->left_deadband = cpu_to_le16(left_deadband);

	memcpy(&packet_spring->spring_start, spring_values, ARRAY_SIZE(spring_values));
	t300rs_fill_timing(&packet_spring->timing, duration, offset);

	ret = t300rs_send_int(t300rs);
	if (ret)
		hid_err(t300rs->hdev, "failed uploading spring\n");

	return ret;
}

static int t300rs_upload_damper(struct t300rs_device_entry *t300rs,
		struct tmff2_effect_state *state)
{

	struct ff_effect effect = state->effect;
	/* we only care about the first axis */
	struct ff_condition_effect spring = state->effect.u.condition[0];
	struct __packed t300rs_packet_damper {
		struct t300rs_packet_header header;
		uint16_t right_coeff;
		uint16_t left_coeff;
		uint16_t right_deadband;
		uint16_t left_deadband;
		uint8_t damper_start[17];
		struct t300rs_packet_timing timing;
	} *packet_damper = (struct t300rs_packet_damper *)t300rs->send_buffer;

	int ret, input_level;
	uint16_t duration, right_coeff, left_coeff, right_deadband, left_deadband, offset;

	duration = effect.replay.length - 1;

	input_level = damper_level;
	if (state->effect.type == FF_FRICTION)
		input_level = friction_level;

	right_coeff = spring.right_coeff * input_level / 100;
	left_coeff = spring.left_coeff * input_level / 100;

	right_deadband = 0xfffe - spring.deadband - spring.center;
	left_deadband = 0xfffe - spring.deadband + spring.center;

	offset = effect.replay.delay;

	t300rs_fill_header(&packet_damper->header, effect.id, 0x64);

	packet_damper->right_coeff = cpu_to_le16(right_coeff);
	packet_damper->left_coeff = cpu_to_le16(left_coeff);
	packet_damper->right_deadband = cpu_to_le16(right_deadband);
	packet_damper->left_deadband = cpu_to_le16(left_deadband);

	memcpy(&packet_damper->damper_start, damper_values, ARRAY_SIZE(damper_values));
	t300rs_fill_timing(&packet_damper->timing, duration, offset);

	ret = t300rs_send_int(t300rs);
	if (ret)
		hid_err(t300rs->hdev, "failed uploading spring\n");

	return ret;
}

static int t300rs_upload_periodic(struct t300rs_device_entry *t300rs,
		struct tmff2_effect_state *state)
{
	struct ff_effect effect = state->effect;
	struct __packed t300rs_packet_periodic {
		struct t300rs_packet_header header;
		uint16_t magnitude;
		uint16_t periodic_offset;
		uint16_t phase;
		uint16_t period;
		uint16_t marker;
		struct t300rs_packet_envelope envelope;
		uint8_t waveform;
		struct t300rs_packet_timing timing;
	} *packet_periodic = (struct t300rs_packet_periodic *)t300rs->send_buffer;

	struct ff_periodic_effect periodic;
	int ret;
	uint16_t duration, period, phase, offset, periodic_offset;
	int16_t magnitude;

	t300rs_calculate_periodic_values(&effect);
	periodic = effect.u.periodic;
	duration = effect.replay.length;
	offset = effect.replay.delay;
	magnitude = periodic.magnitude;
	period = periodic.period;
	phase = periodic.phase;
	periodic_offset = periodic.offset;

	t300rs_fill_header(&packet_periodic->header, effect.id, 0x6b);

	packet_periodic->magnitude = cpu_to_le16(magnitude);
	packet_periodic->periodic_offset = cpu_to_le16(periodic_offset);
	packet_periodic->phase = cpu_to_le16(phase);
	packet_periodic->period = cpu_to_le16(period);

	packet_periodic->marker = cpu_to_le16(0x8000);

	t300rs_fill_envelope(&packet_periodic->envelope, magnitude, duration,
			&periodic.envelope);

	packet_periodic->waveform = periodic.waveform - 0x57;

	t300rs_fill_timing(&packet_periodic->timing, duration, offset);

	ret = t300rs_send_int(t300rs);
	if (ret)
		hid_err(t300rs->hdev, "failed uploading periodic effect");

	return ret;
}

int t300rs_update_effect(void *data, struct tmff2_effect_state *state)
{
	struct t300rs_device_entry *t300rs = data;
	switch (state->effect.type) {
		case FF_CONSTANT:
			return t300rs_update_constant(t300rs, state);
		case FF_RAMP:
			return t300rs_update_ramp(t300rs, state);
		case FF_SPRING:
			return t300rs_update_spring(t300rs, state);
		case FF_DAMPER:
		case FF_FRICTION:
		case FF_INERTIA:
			return t300rs_update_damper(t300rs, state);
		case FF_PERIODIC:
			return t300rs_update_periodic(t300rs, state);
		default:
			hid_err(t300rs->hdev, "invalid effect type: %x",
					state->effect.type);
			return -1;
	}
}

int t300rs_upload_effect(void *data, struct tmff2_effect_state *state)
{
	struct t300rs_device_entry *t300rs = data;
	switch (state->effect.type) {
		case FF_CONSTANT:
			return t300rs_upload_constant(t300rs, state);
		case FF_RAMP:
			return t300rs_upload_ramp(t300rs, state);
		case FF_SPRING:
			return t300rs_upload_spring(t300rs, state);
		case FF_DAMPER:
		case FF_FRICTION:
		case FF_INERTIA:
			return t300rs_upload_damper(t300rs, state);
		case FF_PERIODIC:
			return t300rs_upload_periodic(t300rs, state);
		default:
			hid_err(t300rs->hdev, "invalid effect type: %x",
					state->effect.type);
			return -1;
	}
}

static int t300rs_switch_mode(void *data, uint16_t mode)
{
	struct t300rs_device_entry *t300rs = data;
	if (!t300rs)
		return -ENODEV;

	if(t300rs->mode == mode) /* already in specified mode */
		return 0;

	if (mode == 0)
		/* go to normal mode */
		usb_control_msg(t300rs->usbdev,
				usb_sndctrlpipe(t300rs->usbdev, 0),
				83, 0x41, 5, 0, 0, 0,
				USB_CTRL_SET_TIMEOUT
				);
	else if (mode == 1)
		/* go to advanced mode */
		usb_control_msg(t300rs->usbdev,
				usb_sndctrlpipe(t300rs->usbdev, 0),
				83, 0x41, 3, 0, 0, 0,
				USB_CTRL_SET_TIMEOUT
				);
	else
		hid_warn(t300rs->hdev, "mode %i not supported\n", mode);


	return 0;
}

static struct t300rs_alt_modes {
	char *id;
	char *label;
	uint16_t mode;
} t300rs_modes[] = {
	{"base", "T300RS base", 0},
	{"F1", "T300RS with F1 wheel attachment", 1}
};

static ssize_t t300rs_alt_mode_show(void *data, char *buf)
{
	struct t300rs_device_entry *t300rs = data;
	ssize_t count = 0;
	int i;
	if (!t300rs)
		return -ENODEV;

	if (t300rs->attachment != T300RS_F1_ATTACHMENT)
		/* we only support one base mode */
		return scnprintf(buf, PAGE_SIZE, "%s: %s *\n",
				t300rs_modes[0].id, t300rs_modes[0].label);

	for (i = 0; i < ARRAY_SIZE(t300rs_modes); ++i) {
		count += scnprintf(buf + count, PAGE_SIZE - count, "%s: %s",
				t300rs_modes[i].id, t300rs_modes[i].label);

		if (count >= PAGE_SIZE - 1)
			return count;

		if (t300rs_modes[i].mode == t300rs->mode)
			count += scnprintf(buf + count, PAGE_SIZE - count, " *\n");
		else
			count += scnprintf(buf + count, PAGE_SIZE - count, "\n");

		if (count >= PAGE_SIZE - 1)
			return count;
	}

	return count;
}

static ssize_t t300rs_alt_mode_store(void *data, const char *buf, size_t count)
{
	struct t300rs_device_entry *t300rs = data;
	int i, len, mode_len;
	char *lbuf;
	if (!t300rs)
		return -ENODEV;

	if (t300rs->attachment != T300RS_F1_ATTACHMENT)
		return count; /* don't do anything */

	lbuf = kasprintf(GFP_KERNEL, "%s", buf);
	if (!lbuf)
		return -ENOMEM;

	len = strlen(buf);
	for (i = 0; i < ARRAY_SIZE(t300rs_modes); ++i) {
		mode_len = strlen(t300rs_modes[i].id);
		if (mode_len > len)
			continue;

		if (strncmp(lbuf, t300rs_modes[i].id, mode_len) == 0) {
			t300rs_switch_mode(data, t300rs_modes[i].mode);
			break;
		}
	}

	kfree(lbuf);
	return count;
}

int t300rs_set_autocenter(void *data, uint16_t value)
{
	struct t300rs_device_entry *t300rs = data;
	struct __packed t300rs_packet_autocenter {
		struct t300rs_setup_header header;
		uint16_t value;
	} *autocenter_packet;
	int ret;

	if (!t300rs)
		return -ENODEV;

	/* TODO: this should probably also use a separately allocated buffer?
	 * someone might change autocentering while we're updating the buffer
	 * which would cause corruption */
	autocenter_packet = (struct t300rs_packet_autocenter *)t300rs->send_buffer;

	autocenter_packet->header.cmd = 0x08;
	autocenter_packet->header.code = 0x04;
	autocenter_packet->value = cpu_to_le16(0x01);

	if ((ret = t300rs_send_int(t300rs))) {
		hid_err(t300rs->hdev, "failed setting autocenter");
		return ret;
	}

	autocenter_packet->header.cmd = 0x08;
	autocenter_packet->header.code = 0x03;

	autocenter_packet->value = cpu_to_le16(value);

	if ((ret = t300rs_send_int(t300rs)))
		hid_err(t300rs->hdev, "failed setting autocenter");

	return ret;
}

int t300rs_set_gain(void *data, uint16_t gain)
{
	struct t300rs_device_entry *t300rs = data;
	struct __packed t300rs_packet_gain {
		struct t300rs_setup_header header;
	} *gain_packet;
	int ret;

	if (!t300rs)
		return -ENODEV;

	gain_packet = (struct t300rs_packet_gain *)t300rs->send_buffer;
	gain_packet->header.cmd = 0x02;
	gain_packet->header.code = (gain >> 8) & 0xff;

	if ((ret = t300rs_send_int(t300rs)))
		hid_err(t300rs->hdev, "failed setting gain: %i\n", ret);

	return ret;
}

int t300rs_set_range(void *data, uint16_t value)
{
	struct t300rs_device_entry *t300rs = data;
	/* it's important that we don't use t300rs->send_buffer, as range can be
	 * set from outside of the FFB environment, and we don't want to
	 * accidentally overwrite any data. */
	u8 *send_buffer = kzalloc(t300rs->buffer_length, GFP_KERNEL);
	uint16_t scaled_value;
	int ret;

	if (value < 40) {
		hid_info(t300rs->hdev, "value %i too small, clamping to 40\n", value);
		value = 40;
	}

	if (value > 1080) {
		hid_info(t300rs->hdev, "value %i too large, clamping to 1080\n", value);
		value = 1080;
	}

	if (!send_buffer) {
		ret = -EINVAL;
		hid_err(t300rs->hdev, "could not allocate send_buffer\n");
		goto err;
	}

	scaled_value = value * 0x3c;
	send_buffer[0] = 0x08;
	send_buffer[1] = 0x11;
	send_buffer[2] = scaled_value & 0xff;
	send_buffer[3] = scaled_value >> 8;

	if ((ret = t300rs_send_buf(t300rs, send_buffer, t300rs->buffer_length)))
		hid_warn(t300rs->hdev, "failed setting range\n");

	/* since everythin went OK, update the current range */
	range = value;
err:
	kfree(send_buffer);
	return ret;
}

static int t300rs_send_open(struct t300rs_device_entry *t300rs)
{
	struct __packed t300rs_packet_open {
		struct t300rs_setup_header header;
	} *open_packet;

	open_packet = (struct t300rs_packet_open *)t300rs->send_buffer;
	open_packet->header.cmd = 0x01;
	open_packet->header.code = 0x05;

	return t300rs_send_int(t300rs);
}

static int t300rs_send_close(struct t300rs_device_entry *t300rs)
{
	struct __packed t300rs_packet_open {
		struct t300rs_setup_header header;
	} *open_packet;

	open_packet = (struct t300rs_packet_open *)t300rs->send_buffer;
	open_packet->header.cmd = 0x01;

	return t300rs_send_int(t300rs);
}

int t300rs_open(void *data, int open_mode)
{
	struct t300rs_device_entry *t300rs = data;
	if (!t300rs)
		return -ENODEV;

	if (open_mode && t300rs_send_open(t300rs))
		hid_warn(t300rs->hdev, "failed sending open command\n");

	return t300rs->open(t300rs->input_dev);
}

int t300rs_close(void *data, int open_mode)
{
	struct t300rs_device_entry *t300rs = data;
	int ret;

	if (!t300rs)
		return -ENODEV;

	if (open_mode && (ret = t300rs_send_close(t300rs)))
		hid_warn(t300rs->hdev, "failed sending close command\n");

	t300rs->close(t300rs->input_dev);
	return ret;
}

static int t300rs_check_firmware(struct t300rs_device_entry *t300rs)
{
	int ret;
	struct t300rs_fw_response *fw_response =
		kzalloc(sizeof(struct t300rs_fw_response), GFP_KERNEL);

	if (!fw_response) {
		hid_err(t300rs->hdev, "could not allocate fw_response\n");
		return -ENOMEM;
	}

	/* fetch firmware version */
	ret = usb_control_msg(t300rs->usbdev,
			usb_rcvctrlpipe(t300rs->usbdev, 0),
			t300rs_fw_request.bRequest,
			t300rs_fw_request.bRequestType,
			t300rs_fw_request.wValue,
			t300rs_fw_request.wIndex,
			fw_response,
			t300rs_fw_request.wLength,
			USB_CTRL_SET_TIMEOUT
			);

	if (ret < 0) {
		hid_err(t300rs->hdev, "could not fetch firmware version: %i\n", ret);
		goto out;
	}

	/* educated guess */
	if (fw_response->fw_version < 31 && ret >= 0) {
		hid_err(t300rs->hdev,
				"firmware version %i is too old, please update.\n",
				fw_response->fw_version
		       );

		hid_info(t300rs->hdev, "note: this has to be done through Windows.\n");

		ret = -EINVAL;
		goto out;
	}

	/* everything OK */
	ret = 0;

out:
	kfree(fw_response);
	return ret;
}

static int t300rs_get_attachment(struct t300rs_device_entry *t300rs)
{
	/* taken directly from hid_tminit */
	struct __packed t300rs_attachment_response
	{
		uint16_t type;

		union {
			struct __packed {
				uint16_t field0;
				uint16_t field1;
				uint8_t attachment;
				uint8_t model;
				uint16_t field2;
				uint16_t field3;
				uint16_t field4;
				uint16_t field5;
			} a;

			struct __packed {
				uint16_t field0;
				uint16_t field1;
				uint8_t attachment;
				uint8_t model;
			} b;
		};
	} *response = kzalloc(GFP_KERNEL, sizeof(struct t300rs_attachment_response));
	struct usb_ctrlrequest t300rs_attachment_rq = {
		.bRequestType = 0xc1,
		.bRequest = 73,
		.wValue = 0,
		.wIndex = 0,
		.wLength = sizeof(struct t300rs_attachment_response)
	};
	int ret, attachment;
	if (!response)
		return -ENODEV;

	ret = usb_control_msg(t300rs->usbdev,
			usb_rcvctrlpipe(t300rs->usbdev, 0),
			t300rs_attachment_rq.bRequest,
			t300rs_attachment_rq.bRequestType,
			t300rs_attachment_rq.wValue,
			t300rs_attachment_rq.wIndex,
			response,
			sizeof(struct t300rs_attachment_response),
			USB_CTRL_SET_TIMEOUT
			);

	if (ret < 0) {
		hid_err(t300rs->hdev, "could not fetch attachment: %i\n", ret);
		goto out;
	}

	if (response->type == cpu_to_le16(0x49)) {
		attachment = response->a.attachment;
	} else if (response->type == cpu_to_le16(0x47)) {
		attachment = response->b.attachment;
	} else {
		hid_err(t300rs->hdev,
				"unknown packet type %hx\n, please contact a maintainer",
				response->type);
		ret = -EINVAL;
		goto out;
	}

	kfree(response);
	return attachment;

out:
	kfree(response);
	return ret;
}

static int t300rs_wheel_init(struct tmff2_device_entry *tmff2, int open_mode)
{
	struct t300rs_device_entry *t300rs = kzalloc(sizeof(struct t300rs_device_entry), GFP_KERNEL);
	struct list_head *report_list;
	int ret;

	if (!t300rs) {
		ret = -ENOMEM;
		goto t300rs_err;
	}

	t300rs->hdev = tmff2->hdev;
	t300rs->input_dev = tmff2->input_dev;
	t300rs->usbdev = to_usb_device(tmff2->hdev->dev.parent->parent);

	if(t300rs->hdev->product == TMT300RS_PS4_NORM_ID)
		t300rs->buffer_length = T300RS_PS4_BUFFER_LENGTH;
	else
		t300rs->buffer_length = T300RS_NORM_BUFFER_LENGTH;

	t300rs->send_buffer = kzalloc(t300rs->buffer_length, GFP_KERNEL);
	if (!t300rs->send_buffer) {
		ret = -ENOMEM;
		goto send_err;
	}

	if ((ret = t300rs_check_firmware(t300rs)))
		goto firmware_err;

	report_list = &t300rs->hdev->report_enum[HID_OUTPUT_REPORT].report_list;

	/* because we set the rdesc, we know exactly which report and field to use */
	t300rs->report = list_entry(report_list->next, struct hid_report, list);
	t300rs->ff_field = t300rs->report->field[0];

	t300rs->open = t300rs->input_dev->open;
	t300rs->close = t300rs->input_dev->close;

	/* TODO: PS4 advanced mode? */
	alt_mode = (t300rs->mode = (t300rs->hdev->product == TMT300RS_PS3_ADV_ID));
	if ((t300rs->attachment = t300rs_get_attachment(t300rs)) < 0)
		t300rs->attachment = T300RS_DEFAULT_ATTACHMENT;

	/* everythin went OK */
	tmff2->data = t300rs;
	tmff2->params = t300rs_params;
	tmff2->max_effects = T300RS_MAX_EFFECTS;
	memcpy(tmff2->supported_effects, t300rs_effects, sizeof(t300rs_effects));

	if (!open_mode)
		t300rs_send_open(t300rs);

	hid_info(t300rs->hdev, "force feedback for T300RS\n");
	return 0;

firmware_err:
	kfree(t300rs->send_buffer);
send_err:
	kfree(t300rs);
t300rs_err:
	hid_err(tmff2->hdev, "failed initializing T300RS\n");
	return ret;
}

static int t300rs_wheel_destroy(void *data)
{
	struct t300rs_device_entry *t300rs = data;
	if (!t300rs)
		return -ENODEV;

	kfree(t300rs->send_buffer);
	kfree(t300rs);
	return 0;
}

static __u8 *t300rs_wheel_fixup(struct hid_device *hdev, __u8 *rdesc,
		unsigned int *rsize)
{
	switch (hdev->product) {
		case TMT300RS_PS3_NORM_ID:
		/* normal PS3 mode */
		rdesc = t300rs_rdesc_nrm_fixed;
		*rsize = sizeof(t300rs_rdesc_nrm_fixed);
		break;

		case TMT300RS_PS4_NORM_ID:
		/* PS4 normal mode */
		rdesc = t300rs_rdesc_ps4_fixed;
		*rsize = sizeof(t300rs_rdesc_ps4_fixed);
		break;

		case TMT300RS_PS3_ADV_ID:
		/* PS3 advanced mode */
		rdesc = t300rs_rdesc_adv_fixed;
		*rsize = sizeof(t300rs_rdesc_adv_fixed);
		break;
	}

	return rdesc;
}

int t300rs_populate_api(struct tmff2_device_entry *tmff2)
{
	/* set callbacks */
	tmff2->play_effect = t300rs_play_effect;
	tmff2->upload_effect = t300rs_upload_effect;
	tmff2->update_effect = t300rs_update_effect;
	tmff2->stop_effect = t300rs_stop_effect;

	tmff2->wheel_init = t300rs_wheel_init;
	tmff2->wheel_destroy = t300rs_wheel_destroy;

	tmff2->open = t300rs_open;
	tmff2->close = t300rs_close;
	tmff2->set_gain = t300rs_set_gain;
	tmff2->set_range = t300rs_set_range;
	tmff2->switch_mode = t300rs_switch_mode;
	tmff2->alt_mode_show = t300rs_alt_mode_show;
	tmff2->alt_mode_store = t300rs_alt_mode_store;
	tmff2->set_autocenter = t300rs_set_autocenter;
	tmff2->wheel_fixup = t300rs_wheel_fixup;

	return 0;
}
