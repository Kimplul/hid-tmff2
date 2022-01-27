// SPDX-License-Identifier: GPL-2.0
#include "hid-tmt300rs.h"

static int timer_msecs = DEFAULT_TIMER_PERIOD;
module_param(timer_msecs, int, 0660);
MODULE_PARM_DESC(timer_msecs,
		"Timer resolution in msecs");

static int spring_level = 30;
module_param(spring_level, int, 0);
MODULE_PARM_DESC(spring_level,
		"Level of spring force (0-100), as per Oversteer standards");

static int damper_level = 30;
module_param(damper_level, int, 0);
MODULE_PARM_DESC(damper_level,
		"Level of damper force (0-100), as per Oversteer standards");

static int friction_level = 30;
module_param(friction_level, int, 0);
MODULE_PARM_DESC(friction_level,
		"Level of friction force (0-100), as per Oversteer standards");

static struct t300rs_device_entry *t300rs_get_device(struct hid_device *hdev)
{
	struct t300rs_data *drv_data;
	struct t300rs_device_entry *t300rs;

	spin_lock_irqsave(&lock, lock_flags);
	drv_data = hid_get_drvdata(hdev);
	if (!drv_data) {
		hid_err(hdev, "private data not found\n");
		return NULL;
	}

	t300rs = drv_data->device_props;
	if (!t300rs) {
		hid_err(hdev, "device properties not found\n");
		return NULL;
	}
	spin_unlock_irqrestore(&lock, lock_flags);
	return t300rs;
}

static int t300rs_send_int(struct t300rs_device_entry *t300rs)
{
	int i;
	for (i = 0; i < t300rs->buffer_length; ++i)
		t300rs->ff_field->value[i] = t300rs->send_buffer[i];

	hid_hw_request(t300rs->hdev, t300rs->report, HID_REQ_SET_REPORT);

	memset(t300rs->send_buffer, 0, t300rs->buffer_length);

	return 0;
}

static void t300rs_fill_header(struct t300rs_packet_header *packet_header,
		uint8_t id, uint8_t code)
{
	packet_header->id = id + 1;
	packet_header->code = code;
}

static int t300rs_play_effect(struct t300rs_device_entry *t300rs,
		struct t300rs_effect_state *state)
{
	struct __packed t300rs_packet_play {
		struct t300rs_packet_header header;
		uint8_t value;
	} *play_packet = (struct t300rs_packet_play *)t300rs->send_buffer;

	int ret;


	t300rs_fill_header(&play_packet->header, state->effect.id, 0x89);
	play_packet->value = 0x01;

	ret = t300rs_send_int(t300rs);
	if (ret)
		hid_err(t300rs->hdev, "failed starting effect play\n");

	return ret;
}

static int t300rs_stop_effect(struct t300rs_device_entry *t300rs,
		struct t300rs_effect_state *state)
{
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

static int t300rs_modify_envelope(struct t300rs_device_entry *t300rs,
		struct t300rs_effect_state *state,
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

static int t300rs_modify_duration(struct t300rs_device_entry *t300rs,
		struct t300rs_effect_state *state)
{
	struct ff_effect effect = state->effect;
	struct ff_effect old = state->old;
	struct t300rs_packet_mod_duration {
		struct t300rs_packet_header header;
		uint16_t marker;
		uint16_t duration;
	} *packet_mod_duration = (struct t300rs_packet_mod_duration *)t300rs->send_buffer;

	uint16_t duration;
	int ret = 0;

	duration = effect.replay.length - 1;

	if (effect.replay.length != old.replay.length) {

		t300rs_fill_header(&packet_mod_duration->header, effect.id, 0x49);
		packet_mod_duration->marker = cpu_to_le16(0x4100);
		packet_mod_duration->duration = cpu_to_le16(duration);

		ret = t300rs_send_int(t300rs);
		if (ret) {
			hid_err(t300rs->hdev, "failed modifying duration\n");
			goto error;
		}
	}

error:
	return ret;
}

static int t300rs_modify_constant(struct t300rs_device_entry *t300rs,
		struct t300rs_effect_state *state)
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

	if (constant.level != constant_old.level) {

		t300rs_fill_header(&packet_mod_constant->header, effect.id, 0x0a);
		packet_mod_constant->level = cpu_to_le16(level);

		ret = t300rs_send_int(t300rs);
		if (ret) {
			hid_err(t300rs->hdev, "failed modifying constant effect\n");
			goto error;
		}

	}

	ret = t300rs_modify_envelope(t300rs,
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

	ret = t300rs_modify_duration(t300rs, state);
	if (ret) {
		hid_err(t300rs->hdev, "failed modifying constant duration\n");
		goto error;
	}

error:
	return ret;
}

static int t300rs_modify_ramp(struct t300rs_device_entry *t300rs,
		struct t300rs_effect_state *state)
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

	if (ramp.start_level != ramp_old.start_level || ramp.end_level != ramp_old.end_level) {

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

	ret = t300rs_modify_envelope(t300rs,
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

	ret = t300rs_modify_duration(t300rs, state);
	if (ret) {
		hid_err(t300rs->hdev, "failed modifying ramp duration\n");
		goto error;
	}

error:
	return ret;
}
static int t300rs_modify_damper(struct t300rs_device_entry *t300rs,
		struct t300rs_effect_state *state)
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

	ret = t300rs_modify_duration(t300rs, state);
	if (ret) {
		hid_err(t300rs->hdev, "failed modifying damper duration\n");
		goto error;
	}

error:
	return ret;
}


static int t300rs_modify_periodic(struct t300rs_device_entry *t300rs,
		struct t300rs_effect_state *state)
{
	struct ff_effect effect = state->effect;
	struct ff_effect old = state->old;
	struct ff_periodic_effect periodic = effect.u.periodic;
	struct ff_periodic_effect periodic_old = old.u.periodic;
	struct __packed t300rs_packet_mod_periodic {
		struct t300rs_packet_header header;
		uint8_t attribute;
		uint16_t value;
	} *packet_mod_periodic = (struct t300rs_packet_mod_periodic *)t300rs->send_buffer;

	int ret;
	int16_t magnitude;
	uint16_t phase;
	bool update_phase = false;

	magnitude = (periodic.magnitude * fixp_sin16(effect.direction * 360 / 0x10000)) / 0x7fff;
	phase = periodic.phase;
	if(magnitude < 0){
		phase += 0x4000;
		phase = phase < 0 ? -phase : phase;
		update_phase = true;
	}

	magnitude = magnitude < 0 ? -magnitude : magnitude;

	if (periodic.magnitude != periodic_old.magnitude) {

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
		int16_t offset = periodic.offset;

		t300rs_fill_header(&packet_mod_periodic->header, effect.id, 0x0e);
		packet_mod_periodic->attribute = 0x02;
		packet_mod_periodic->value = cpu_to_le16(offset);

		ret = t300rs_send_int(t300rs);
		if (ret) {
			hid_err(t300rs->hdev, "failed modifying periodic offset\n");
			goto error;
		}

	}

	if (periodic.phase != periodic_old.phase || update_phase) {

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

	ret = t300rs_modify_envelope(t300rs,
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

	ret = t300rs_modify_duration(t300rs, state);
	if (ret) {
		hid_err(t300rs->hdev, "failed modifying periodic duration\n");
		goto error;
	}

error:
	return ret;
}

static int t300rs_upload_constant(struct t300rs_device_entry *t300rs,
		struct t300rs_effect_state *state)
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

	/* some games, such as DiRT Rally 2 have a weird feeling to them, sort of
	 * like the wheel pulls just a bit to the right or left and then it just
	 * stops. I wouldn't be surprised if it's got something to do with the
	 * constant envelope, but right now I don't know.
	 */

	if (test_bit(FF_EFFECT_PLAYING, &state->flags)
			&& test_bit(FF_EFFECT_QUEUE_UPDATE, &state->flags)) {

		__clear_bit(FF_EFFECT_QUEUE_UPLOAD, &state->flags);

		return t300rs_modify_constant(t300rs, state);
	}

	level = (constant.level * fixp_sin16(effect.direction * 360 / 0x10000)) / 0x7fff;
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
		struct t300rs_effect_state *state)
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

	if (test_bit(FF_EFFECT_PLAYING, &state->flags)
			&& test_bit(FF_EFFECT_QUEUE_UPDATE, &state->flags)) {

		__clear_bit(FF_EFFECT_QUEUE_UPLOAD, &state->flags);

		return t300rs_modify_ramp(t300rs, state);
	}

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
		struct t300rs_effect_state *state)
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

	if (test_bit(FF_EFFECT_PLAYING, &state->flags)
			&& test_bit(FF_EFFECT_QUEUE_UPDATE, &state->flags)) {

		__clear_bit(FF_EFFECT_QUEUE_UPLOAD, &state->flags);

		return t300rs_modify_damper(t300rs, state);
	}

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
		struct t300rs_effect_state *state)
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

	if (test_bit(FF_EFFECT_PLAYING, &state->flags)	&&
			test_bit(FF_EFFECT_QUEUE_UPDATE, &state->flags)) {

		__clear_bit(FF_EFFECT_QUEUE_UPLOAD, &state->flags);

		return t300rs_modify_damper(t300rs, state);
	}

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
		struct t300rs_effect_state *state)
{
	struct ff_effect effect = state->effect;
	struct ff_periodic_effect periodic = state->effect.u.periodic;
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

	int ret;
	uint16_t duration, magnitude, period, offset;
	int16_t periodic_offset, phase;

	if (test_bit(FF_EFFECT_PLAYING, &state->flags)	&&
			test_bit(FF_EFFECT_QUEUE_UPDATE, &state->flags)) {

		__clear_bit(FF_EFFECT_QUEUE_UPLOAD, &state->flags);

		return t300rs_modify_periodic(t300rs, state);
	}

	duration = effect.replay.length - 1;

	magnitude = (periodic.magnitude * fixp_sin16(effect.direction * 360 / 0x10000)) / 0x7fff;

	phase = periodic.phase;
	if(magnitude < 0){
		phase += 0x4000;
		phase = phase < 0 ? -phase : phase;
	}
	
	magnitude = magnitude < 0 ? -magnitude : magnitude;
	periodic_offset = periodic.offset;
	period = periodic.period;
	offset = effect.replay.delay;

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

static int t300rs_upload_effect(struct t300rs_device_entry *t300rs,
		struct t300rs_effect_state *state)
{
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
			hid_err(t300rs->hdev, "invalid effect type: %x", state->effect.type);
			return -1;
	}
}

static int t300rs_timer_helper(struct t300rs_device_entry *t300rs)
{
	struct t300rs_effect_state *state;
	unsigned long jiffies_now = JIFFIES2MS(jiffies);
	int max_count = 0, effect_id, ret;

	for (effect_id = 0; effect_id < T300RS_MAX_EFFECTS; ++effect_id) {

		state = &t300rs->states[effect_id];

		if (test_bit(FF_EFFECT_PLAYING, &state->flags) && state->effect.replay.length) {
			if ((jiffies_now - state->start_time) >= state->effect.replay.length) {
				__clear_bit(FF_EFFECT_PLAYING, &state->flags);

				/* lazy bum fix? */
				__clear_bit(FF_EFFECT_QUEUE_UPDATE, &state->flags);

				if (state->count)
					state->count--;

				if (state->count)
					__set_bit(FF_EFFECT_QUEUE_START, &state->flags);
			}
		}

		if (test_bit(FF_EFFECT_QUEUE_UPLOAD, &state->flags)) {
			__clear_bit(FF_EFFECT_QUEUE_UPLOAD, &state->flags);

			ret = t300rs_upload_effect(t300rs, state);
			if (ret) {
				hid_err(t300rs->hdev, "failed uploading effects");
				return ret;
			}
		}

		if (test_bit(FF_EFFECT_QUEUE_START, &state->flags)) {
			__clear_bit(FF_EFFECT_QUEUE_START, &state->flags);
			__set_bit(FF_EFFECT_PLAYING, &state->flags);

			ret = t300rs_play_effect(t300rs, state);
			if (ret) {
				hid_err(t300rs->hdev, "failed starting effects\n");
				return ret;
			}

		}

		if (test_bit(FF_EFFECT_QUEUE_STOP, &state->flags)) {
			__clear_bit(FF_EFFECT_QUEUE_STOP, &state->flags);
			__clear_bit(FF_EFFECT_PLAYING, &state->flags);

			ret = t300rs_stop_effect(t300rs, state);
			if (ret) {
				hid_err(t300rs->hdev, "failed stopping effect\n");
				return ret;
			}
		}

		if (state->count > max_count)
			max_count = state->count;
	}

	return max_count;
}

static enum hrtimer_restart t300rs_timer(struct hrtimer *t)
{
	struct t300rs_device_entry *t300rs = container_of(t, struct t300rs_device_entry, hrtimer);
	int max_count;

	max_count = t300rs_timer_helper(t300rs);

	if (max_count > 0) {
		hrtimer_forward_now(&t300rs->hrtimer, ms_to_ktime(timer_msecs));
		return HRTIMER_RESTART;
	} else {
		return HRTIMER_NORESTART;
	}
}

static int t300rs_upload(struct input_dev *dev,
		struct ff_effect *effect, struct ff_effect *old)
{
	struct hid_device *hdev = input_get_drvdata(dev);
	struct t300rs_device_entry *t300rs;
	struct t300rs_effect_state *state;

	t300rs = t300rs_get_device(hdev);
	if (!t300rs) {
		hid_err(hdev, "could not get device\n");
		return -1;
	}

	if (effect->type == FF_PERIODIC && effect->u.periodic.period == 0)
		return -EINVAL;

	state = &t300rs->states[effect->id];

	spin_lock_irqsave(&t300rs->lock, t300rs->lock_flags);

	state->effect = *effect;

	if (old) {
		state->old = *old;
		__set_bit(FF_EFFECT_QUEUE_UPDATE, &state->flags);
	} else {
		__clear_bit(FF_EFFECT_QUEUE_UPDATE, &state->flags);
	}
	__set_bit(FF_EFFECT_QUEUE_UPLOAD, &state->flags);

	spin_unlock_irqrestore(&t300rs->lock, t300rs->lock_flags);

	return 0;
}

static int t300rs_play(struct input_dev *dev, int effect_id, int value)
{
	struct hid_device *hdev = input_get_drvdata(dev);
	struct t300rs_device_entry *t300rs;
	struct t300rs_effect_state *state;

	t300rs = t300rs_get_device(hdev);
	if (!t300rs) {
		hid_err(hdev, "could not get device\n");
		return -1;
	}

	state = &t300rs->states[effect_id];

	if (&state->effect == 0)
		return 0;

	spin_lock_irqsave(&t300rs->lock, t300rs->lock_flags);

	if (value > 0) {
		state->count = value;
		state->start_time = JIFFIES2MS(jiffies);
		__set_bit(FF_EFFECT_QUEUE_START, &state->flags);

		if (test_bit(FF_EFFECT_QUEUE_STOP, &state->flags))
			__clear_bit(FF_EFFECT_QUEUE_STOP, &state->flags);

	} else {
		__set_bit(FF_EFFECT_QUEUE_STOP, &state->flags);
	}

	if (!hrtimer_active(&t300rs->hrtimer))
		hrtimer_start(&t300rs->hrtimer, ms_to_ktime(timer_msecs), HRTIMER_MODE_REL);

	spin_unlock_irqrestore(&t300rs->lock, t300rs->lock_flags);
	return 0;
}

static ssize_t spring_level_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct hid_device *hdev = to_hid_device(dev);
	unsigned int value;
	int ret;

	ret = kstrtouint(buf, 0, &value);
	if (ret) {
		hid_err(hdev, "kstrtouint failed at spring_level_store: %i", ret);
		return ret;
	}

	if (value > 100)
		value = 100;

	spring_level = value;

	return count;
}
static ssize_t spring_level_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	size_t count;

	count = scnprintf(buf, PAGE_SIZE, "%u\n", spring_level);

	return count;
}

static DEVICE_ATTR_RW(spring_level);

static ssize_t damper_level_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct hid_device *hdev = to_hid_device(dev);
	unsigned int value;
	int ret;

	ret = kstrtouint(buf, 0, &value);
	if (ret) {
		hid_err(hdev, "kstrtouint failed at damper_level_store: %i", ret);
		return ret;
	}

	if (value > 100)
		value = 100;

	damper_level = value;

	return count;
}

static ssize_t damper_level_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	size_t count;

	count = scnprintf(buf, PAGE_SIZE, "%u\n", damper_level);

	return count;
}

static DEVICE_ATTR_RW(damper_level);

static ssize_t friction_level_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct hid_device *hdev = to_hid_device(dev);
	unsigned int value;
	int ret;

	ret = kstrtouint(buf, 0, &value);
	if (ret) {
		hid_err(hdev, "kstrtouint failed at friction_level_store: %i", ret);
		return ret;
	}

	if (value > 100)
		value = 100;

	friction_level = value;

	return count;
}
static ssize_t friction_level_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	size_t count;

	count = scnprintf(buf, PAGE_SIZE, "%u\n", friction_level);

	return count;
}

static DEVICE_ATTR_RW(friction_level);

static ssize_t range_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct hid_device *hdev = to_hid_device(dev);
	struct t300rs_device_entry *t300rs;
	struct __packed t300rs_packet_range {
		struct t300rs_setup_header header;
		uint16_t range;
	} *packet_range;

	unsigned int range;
	int ret;

	ret = kstrtouint(buf, 0, &range);
	if (ret) {
		hid_err(hdev, "kstrtouint failed at range_store: %i", ret);
		return ret;
	}

	t300rs = t300rs_get_device(hdev);
	if (!t300rs) {
		hid_err(hdev, "could not get device\n");
		return -1;
	}

	packet_range = (struct t300rs_packet_range *)t300rs->send_buffer;

	if (range < 40)
		range = 40;

	if (range > 1080)
		range = 1080;

	range *= 0x3c;


	packet_range->header.cmd = 0x08;
	packet_range->header.code = 0x11;

	packet_range->range = cpu_to_le16(range);

	ret = t300rs_send_int(t300rs);
	if (ret) {
		hid_err(hdev, "failed sending interrupts\n");
		return -1;
	}

	t300rs->range = range / 0x3c;

	return count;
}

static ssize_t range_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct hid_device *hdev = to_hid_device(dev);
	struct t300rs_device_entry *t300rs;
	size_t count = 0;

	t300rs = t300rs_get_device(hdev);
	if (!t300rs) {
		hid_err(hdev, "could not get device\n");
		return -1;
	}

	count = scnprintf(buf, PAGE_SIZE, "%u\n", t300rs->range);
	return count;
}

static DEVICE_ATTR_RW(range);

static ssize_t adv_mode_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct hid_device *hdev = to_hid_device(dev);
	struct t300rs_device_entry *t300rs;

	t300rs = t300rs_get_device(hdev);
	if (!t300rs) {
		hid_err(hdev, "could not get device\n");
		return -1;
	}

	if(t300rs->adv_mode)
		usb_control_msg(t300rs->usbdev,
				usb_sndctrlpipe(t300rs->usbdev, 0),
				83, 0x41, 5, 0, 0, 0,
				USB_CTRL_SET_TIMEOUT
				);
	else
		usb_control_msg(t300rs->usbdev,
				usb_sndctrlpipe(t300rs->usbdev, 0),
				83, 0x41, 3, 0, 0, 0,
				USB_CTRL_SET_TIMEOUT
				);

	return count;
}

static ssize_t adv_mode_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct hid_device *hdev = to_hid_device(dev);
	struct t300rs_device_entry *t300rs;

	t300rs = t300rs_get_device(hdev);
	if (!t300rs) {
		hid_err(hdev, "could not get device\n");
		return -1;
	}

	return scnprintf(buf, PAGE_SIZE, "%s\n", t300rs->adv_mode ? "Yes" : "No");
}
static DEVICE_ATTR_RW(adv_mode);

static void t300rs_set_autocenter(struct input_dev *dev, uint16_t value)
{
	struct hid_device *hdev = input_get_drvdata(dev);
	struct t300rs_device_entry *t300rs;
	struct __packed t300rs_packet_autocenter {
		struct t300rs_setup_header header;
		uint16_t value;
	} *autocenter_packet;

	int ret;

	t300rs = t300rs_get_device(hdev);
	if (!t300rs) {
		hid_err(hdev, "could not get device\n");
		return;
	}

	autocenter_packet = (struct t300rs_packet_autocenter *)t300rs->send_buffer;

	autocenter_packet->header.cmd = 0x08;
	autocenter_packet->header.code = 0x04;
	autocenter_packet->value = cpu_to_le16(0x01);

	ret = t300rs_send_int(t300rs);
	if (ret) {
		hid_err(hdev, "failed setting autocenter");
		return;
	}

	autocenter_packet->header.cmd = 0x08;
	autocenter_packet->header.code = 0x03;

	autocenter_packet->value = cpu_to_le16(value);

	ret = t300rs_send_int(t300rs);
	if (ret)
		hid_err(hdev, "failed setting autocenter");
}

static void t300rs_set_gain(struct input_dev *dev, uint16_t gain)
{
	struct hid_device *hdev = input_get_drvdata(dev);
	struct t300rs_device_entry *t300rs;
	struct __packed t300rs_packet_gain {
		struct t300rs_setup_header header;
	} *gain_packet;

	int ret;

	t300rs = t300rs_get_device(hdev);
	if (!t300rs) {
		hid_err(hdev, "could not get device\n");
		return;
	}

	gain_packet = (struct t300rs_packet_gain *)t300rs->send_buffer;

	gain_packet->header.cmd = 0x02;
	gain_packet->header.code = (gain >> 8) & 0xff;

	ret = t300rs_send_int(t300rs);
	if (ret)
		hid_err(hdev, "failed setting gain: %i\n", ret);
}

static void t300rs_destroy(struct ff_device *ff)
{
	// maybe not necessary?
}

static int t300rs_open(struct input_dev *dev)
{
	struct t300rs_device_entry *t300rs;
	struct hid_device *hdev = input_get_drvdata(dev);
	struct __packed t300rs_packet_open {
		struct t300rs_setup_header header;
	} *open_packet;

	int ret;

	t300rs = t300rs_get_device(hdev);
	if (!t300rs) {
		hid_err(hdev, "could not get device\n");
		return -1;
	}

	open_packet = (struct t300rs_packet_open *)t300rs->send_buffer;

	open_packet->header.cmd = 0x01;
	open_packet->header.code = 0x05;

	ret = t300rs_send_int(t300rs);
	if (ret) {
		hid_err(hdev, "failed sending interrupts\n");
		goto err;
	}

err:
	return t300rs->open(dev);
}

static void t300rs_close(struct input_dev *dev)
{
	struct t300rs_device_entry *t300rs;
	struct hid_device *hdev = input_get_drvdata(dev);
	struct t300rs_packet_close {
		struct t300rs_setup_header header;
	} *close_packet;

	int ret;

	t300rs = t300rs_get_device(hdev);
	if (!t300rs) {
		hid_err(hdev, "could not get device\n");
		return;
	}

	close_packet = (struct t300rs_packet_close *)t300rs->send_buffer;

	close_packet->header.cmd = 0x01;

	ret = t300rs_send_int(t300rs);
	if (ret) {
		hid_err(hdev, "failed sending interrupts\n");
		goto err;
	}
err:
	t300rs->close(dev);
}

static int t300rs_create_files(struct hid_device *hdev)
{
	int ret;

	ret = device_create_file(&hdev->dev, &dev_attr_adv_mode);
	if (ret) {
		hid_warn(hdev, "unable to create sysfs interface for adv_mode\n");
		goto attr_adv_err;
	}

	ret = device_create_file(&hdev->dev, &dev_attr_range);
	if (ret) {
		hid_warn(hdev, "unable to create sysfs interface for range\n");
		goto attr_range_err;
	}

	ret = device_create_file(&hdev->dev, &dev_attr_spring_level);
	if (ret) {
		hid_warn(hdev, "unable to create sysfs interface for spring_level\n");
		goto attr_spring_err;
	}

	ret = device_create_file(&hdev->dev, &dev_attr_damper_level);
	if (ret) {
		hid_warn(hdev, "unable to create sysfs interface for damper_level\n");
		goto attr_damper_err;
	}

	ret = device_create_file(&hdev->dev, &dev_attr_friction_level);
	if (ret) {
		hid_warn(hdev, "unable to create sysfs interface for friction_level\n");
		goto attr_friction_err;
	}

	return ret;

	// if the creation of dev_attr_friction fails, we don't need to remove it
	// device_remove_file(&hdev->dev, &dev_attr_friction_level);
attr_friction_err:
	device_remove_file(&hdev->dev, &dev_attr_damper_level);
attr_damper_err:
	device_remove_file(&hdev->dev, &dev_attr_spring_level);
attr_spring_err:
	device_remove_file(&hdev->dev, &dev_attr_range);
attr_range_err:
	device_remove_file(&hdev->dev, &dev_attr_adv_mode);
attr_adv_err:
	return ret;
}

static int t300rs_init(struct hid_device *hdev, const signed short *ff_bits)
{
	struct t300rs_device_entry *t300rs;
	struct t300rs_data *drv_data;
	struct list_head *report_list;
	struct hid_input *hidinput = list_entry(hdev->inputs.next,
			struct hid_input, list);
	struct input_dev *input_dev = hidinput->input;
	struct device *dev = &hdev->dev;
	struct usb_interface *usbif = to_usb_interface(dev->parent);
	struct usb_device *usbdev = interface_to_usbdev(usbif);
	struct ff_device *ff;
	char range[10] = "900"; // max
	int i, ret;

	drv_data = hid_get_drvdata(hdev);
	if (!drv_data) {
		hid_err(hdev, "private driver data not allocated\n");
		ret = -ENOMEM;
		goto drvdata_err;
	}

	t300rs = kzalloc(sizeof(struct t300rs_device_entry), GFP_KERNEL);
	if (!t300rs) {
		ret = -ENOMEM;
		goto t300rs_err;
	}

	t300rs->input_dev = input_dev;
	t300rs->hdev = hdev;
	t300rs->usbdev = usbdev;
	t300rs->usbif = usbif;

	t300rs->states = kzalloc(
			sizeof(struct t300rs_effect_state) * T300RS_MAX_EFFECTS, GFP_KERNEL);

	if (!t300rs->states) {
		ret = -ENOMEM;
		goto states_err;
	}

	if(hdev->product == 0xb66d)
		t300rs->buffer_length = T300RS_PS4_BUFFER_LENGTH;
	else
		t300rs->buffer_length = T300RS_NORM_BUFFER_LENGTH;

	t300rs->send_buffer = kzalloc(t300rs->buffer_length, GFP_KERNEL);
	if (!t300rs->send_buffer) {
		ret = -ENOMEM;
		goto send_err;
	}

	t300rs->firmware_response = kzalloc(sizeof(struct t300rs_firmware_response), GFP_KERNEL);
	if (!t300rs->firmware_response) {
		ret = -ENOMEM;
		goto firmware_err;
	}

	// Check firmware version
	ret = usb_control_msg(t300rs->usbdev,
			usb_rcvctrlpipe(t300rs->usbdev, 0),
			t300rs_firmware_request.bRequest,
			t300rs_firmware_request.bRequestType,
			t300rs_firmware_request.wValue,
			t300rs_firmware_request.wIndex,
			t300rs->firmware_response,
			t300rs_firmware_request.wLength,
			USB_CTRL_SET_TIMEOUT
			);

	// Educated guess
	if (t300rs->firmware_response->firmware_version < 31 && ret >= 0) {
		hid_err(t300rs->hdev,
				"firmware version %i is too old, please update.",
				t300rs->firmware_response->firmware_version
		       );

		hid_info(t300rs->hdev, "note: this has to be done through Windows.");

		ret = -EINVAL;
		goto version_err;
	}

	spin_lock_init(&t300rs->lock);

	drv_data->device_props = t300rs;

	report_list = &hdev->report_enum[HID_OUTPUT_REPORT].report_list;

	// because we set the rdesc, we know exactly which report and field to use
	t300rs->report = list_entry(report_list->next, struct hid_report, list);
	t300rs->ff_field = t300rs->report->field[0];

	// set ff capabilities
	for (i = 0; ff_bits[i] >= 0; ++i)
		__set_bit(ff_bits[i], input_dev->ffbit);

	ret = input_ff_create(input_dev, T300RS_MAX_EFFECTS);
	if (ret) {
		hid_err(hdev, "could not create input_ff\n");
		goto input_ff_err;
	}

	ff = input_dev->ff;
	ff->upload = t300rs_upload;
	ff->playback = t300rs_play;
	ff->set_gain = t300rs_set_gain;
	ff->set_autocenter = t300rs_set_autocenter;
	ff->destroy = t300rs_destroy;

	t300rs->open = input_dev->open;
	t300rs->close = input_dev->close;

	input_dev->open = t300rs_open;
	input_dev->close = t300rs_close;

	ret = t300rs_create_files(hdev);
	if (ret) {
		// this might not be a catastrophic issue, but it could affect
		// programs such as oversteer, best play it safe
		hid_err(hdev, "could not create sysfs files\n");
		goto sysfs_err;
	}


	hrtimer_init(&t300rs->hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	t300rs->hrtimer.function = t300rs_timer;

	range_store(dev, &dev_attr_range, range, 10);
	t300rs_set_gain(input_dev, 0xffff);

	t300rs->adv_mode = (hdev->product == 0xb66f);

	hid_info(hdev, "force feedback for T300RS\n");
	return 0;

sysfs_err:
	kfree(t300rs->firmware_response);

input_ff_err:
version_err:
firmware_err:
	kfree(t300rs->send_buffer);

send_err:
	kfree(t300rs->states);

states_err:
	kfree(t300rs);

t300rs_err:
	kfree(drv_data);

drvdata_err:
	hid_err(hdev, "failed creating force feedback device\n");

	return ret;

}

static int t300rs_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	int ret;
	struct t300rs_data *drv_data;

	spin_lock_init(&lock);

	drv_data = kzalloc(sizeof(struct t300rs_data), GFP_KERNEL);
	if (!drv_data) {
		ret = -ENOMEM;
		goto err;
	}

	drv_data->quirks = id->driver_data;
	hid_set_drvdata(hdev, (void *)drv_data);

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "parse failed\n");
		goto err;
	}

	ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT & ~HID_CONNECT_FF);
	if (ret) {
		hid_err(hdev, "hw start failed\n");
		goto err;
	}

	ret = t300rs_init(hdev, (void *)id->driver_data);
	if (ret) {
		hid_err(hdev, "t300rs_init failed\n");
		goto err;
	}

err:
	return ret;
}

static void t300rs_remove(struct hid_device *hdev)
{
	struct t300rs_device_entry *t300rs;
	struct t300rs_data *drv_data;

	drv_data = hid_get_drvdata(hdev);
	t300rs = t300rs_get_device(hdev);
	if (!t300rs) {
		hid_err(hdev, "could not get device\n");
		return;
	}

	hrtimer_cancel(&t300rs->hrtimer);
	drv_data->device_props = NULL;

	device_remove_file(&hdev->dev, &dev_attr_range);
	device_remove_file(&hdev->dev, &dev_attr_adv_mode);
	device_remove_file(&hdev->dev, &dev_attr_spring_level);
	device_remove_file(&hdev->dev, &dev_attr_damper_level);
	device_remove_file(&hdev->dev, &dev_attr_friction_level);

	hid_hw_stop(hdev);
	kfree(t300rs->states);
	kfree(t300rs->send_buffer);
	kfree(t300rs->firmware_response);
	kfree(t300rs);
	kfree(drv_data);
}

static __u8 *t300rs_report_fixup(struct hid_device *hdev, __u8 *rdesc,
		unsigned int *rsize)
{
	if(hdev->product == 0xb66e) {
		/* PS3 normal mode */
		rdesc = t300rs_rdesc_nrm_fixed;
		*rsize = sizeof(t300rs_rdesc_nrm_fixed);
	} else if (hdev->product == 0xb66d){
		/* PS4 normal mode */
		rdesc = t300rs_rdesc_ps4_fixed;
		*rsize = sizeof(t300rs_rdesc_ps4_fixed);
	} else if (hdev->product == 0xb66f){
		/* PS3 advanced mode */
		rdesc = t300rs_rdesc_adv_fixed;
		*rsize = sizeof(t300rs_rdesc_adv_fixed);
	}

	return rdesc;
}

static const struct hid_device_id t300rs_devices[] = {
	{HID_USB_DEVICE(USB_VENDOR_ID_THRUSTMASTER, 0xb66e),
		.driver_data = (unsigned long)t300rs_ff_effects},
	{HID_USB_DEVICE(USB_VENDOR_ID_THRUSTMASTER, 0xb66f),
		.driver_data = (unsigned long)t300rs_ff_effects},
	{HID_USB_DEVICE(USB_VENDOR_ID_THRUSTMASTER, 0xb66d),
		.driver_data = (unsigned long)t300rs_ff_effects},
	{}
};
MODULE_DEVICE_TABLE(hid, t300rs_devices);

static struct hid_driver t300rs_driver = {
	.name = "t300rs",
	.id_table = t300rs_devices,
	.probe = t300rs_probe,
	.remove = t300rs_remove,
	.report_fixup = t300rs_report_fixup,
};
module_hid_driver(t300rs_driver);

MODULE_LICENSE("GPL");
