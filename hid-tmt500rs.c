// SPDX-License-Identifier: GPL-2.0
#include "hid-tmt500rs.h"

static int timer_msecs = DEFAULT_TIMER_PERIOD;
module_param(timer_msecs, int, 0660);
MODULE_PARM_DESC(timer_msecs, "Timer resolution in msecs");

static int spring_level = 30;
module_param(spring_level, int, 0);
MODULE_PARM_DESC(spring_level, "Level of spring force (0-100), as per Oversteer standards");

static int damper_level = 30;
module_param(damper_level, int, 0);
MODULE_PARM_DESC(damper_level, "Level of damper force (0-100), as per Oversteer standards");

static int friction_level = 30;
module_param(friction_level, int, 0);
MODULE_PARM_DESC(friction_level, "Level of friction force (0-100), as per Oversteer standards");

static struct t500rs_device_entry *t500rs_get_device(struct hid_device *hdev)
{
	struct t500rs_data *drv_data;
	struct t500rs_device_entry *t500rs;

	spin_lock_irqsave(&lock, lock_flags);
	drv_data = hid_get_drvdata(hdev);
	if (!drv_data) {
		hid_err(hdev, "private data not found\n");
		return NULL;
	}

	t500rs = drv_data->device_props;
	if (!t500rs) {
		hid_err(hdev, "device properties not found\n");
		return NULL;
	}
	spin_unlock_irqrestore(&lock, lock_flags);
	return t500rs;
}

static int t500rs_send_int(struct input_dev *dev, u8 *send_buffer, int *trans)
{
	struct hid_device *hdev = input_get_drvdata(dev);
	struct t500rs_device_entry *t500rs;
	int i;

	t500rs = t500rs_get_device(hdev);
	if (!t500rs) {
		hid_err(hdev, "could not get device\n");
		return -1;
	}

	for (i = 0; i < T500RS_BUFFER_LENGTH; ++i)
		t500rs->ff_field->value[i] = send_buffer[i];

	hid_hw_request(t500rs->hdev, t500rs->report, HID_REQ_SET_REPORT);

	memset(send_buffer, 0, T500RS_BUFFER_LENGTH);

	return 0;
}

static void t500rs_int_callback(struct urb *urb){
    if(urb->status){
        hid_warn(urb->dev, "urb status %i received\n", urb->status);
    }

    usb_free_urb(urb);
}

static int t500rs_send_custom_int(struct input_dev *dev, u8 *send_buffer, int *trans){
    struct hid_device *hdev = input_get_drvdata(dev);
    struct t500rs_device_entry *t500rs;
    struct usb_device *usbdev;
    struct usb_interface *usbif;
    struct usb_host_endpoint *ep;
    struct urb *urb = usb_alloc_urb(0, GFP_ATOMIC);
    
    t500rs = t500rs_get_device(hdev);
    if(!t500rs){
        hid_err(hdev, "could not get device\n");
    }

    usbdev = t500rs->usbdev;
    usbif = t500rs->usbif;
    ep = &usbif->cur_altsetting->endpoint[1];

    usb_fill_int_urb(
            urb,
            usbdev,
            usb_sndintpipe(usbdev, 1),
            send_buffer,
            T500RS_BUFFER_LENGTH,
            t500rs_int_callback,
            hdev,
            ep->desc.bInterval
            );

    return usb_submit_urb(urb, GFP_ATOMIC);
}

static int t500rs_play_effect(struct t500rs_device_entry *t500rs,
		struct t500rs_effect_state *state)
{
	u8 *send_buffer = t500rs->send_buffer;
	int ret, trans;


	send_buffer[1] = state->effect.id + 1;
	send_buffer[2] = 0x89;
	send_buffer[3] = 0x01;

	ret = t500rs_send_custom_int(t500rs->input_dev, send_buffer, &trans);
	if (ret)
		hid_err(t500rs->hdev, "failed starting effect play\n");

	return ret;
}

static int t500rs_stop_effect(struct t500rs_device_entry *t500rs,
		struct t500rs_effect_state *state)
{
	u8 *send_buffer = t500rs->send_buffer;
	int ret, trans;


	send_buffer[1] = state->effect.id + 1;
	send_buffer[2] = 0x89;

	ret = t500rs_send_int(t500rs->input_dev, send_buffer, &trans);
	if (ret)
		hid_err(t500rs->hdev, "failed stopping effect play\n");

	return ret;
}

static void t500rs_fill_envelope(u8 *send_buffer, int i, s16 level,
		u16 duration, struct ff_envelope *envelope)
{
	u16 attack_length = (duration * envelope->attack_length) / 0x7fff;
	u16 attack_level = (level * envelope->attack_level) / 0x7fff;
	u16 fade_length = (duration * envelope->fade_length) / 0x7fff;
	u16 fade_level = (level * envelope->fade_level) / 0x7fff;

	send_buffer[i + 0] = attack_length & 0xff;
	send_buffer[i + 1] = attack_length >> 8;
	send_buffer[i + 2] = attack_level & 0xff;
	send_buffer[i + 3] = attack_level >> 8;
	send_buffer[i + 4] = fade_length & 0xff;
	send_buffer[i + 5] = fade_length >> 8;
	send_buffer[i + 6] = fade_level & 0xff;
	send_buffer[i + 7] = fade_level >> 8;
}

static int t500rs_modify_envelope(struct t500rs_device_entry *t500rs,
		struct t500rs_effect_state *state,
		u8 *send_buffer,
		s16 level,
		u16 duration,
		u8 id,
		struct ff_envelope envelope,
		struct ff_envelope envelope_old
		)
{
	u16 attack_length, attack_level, fade_length, fade_level;
	int ret = 0, trans;

	if (duration == 0)
		duration = 0xffff;

	attack_length = (duration * envelope.attack_length) / 0x7fff;
	attack_level = (level * envelope.attack_level) / 0x7fff;
	fade_length = (duration * envelope.fade_length) / 0x7fff;
	fade_level = (level * envelope.fade_level) / 0x7fff;


	send_buffer[1] = id + 1;
	send_buffer[2] = 0x31;

	if (envelope.attack_length != envelope_old.attack_length) {
		send_buffer[3] = 0x81;

		send_buffer[4] = attack_length & 0xff;
		send_buffer[5] = attack_length >> 8;

		ret = t500rs_send_int(t500rs->input_dev, send_buffer, &trans);
		if (ret) {
			hid_err(t500rs->hdev, "failed modifying effect envelope\n");
			goto error;
		}
	}

	if (envelope.attack_level != envelope_old.attack_level) {
		send_buffer[3] = 0x82;

		send_buffer[4] = attack_level & 0xff;
		send_buffer[5] = attack_level >> 8;

		ret = t500rs_send_int(t500rs->input_dev, send_buffer, &trans);
		if (ret) {
			hid_err(t500rs->hdev, "failed modifying effect envelope\n");
			goto error;
		}
	}

	if (envelope.fade_length != envelope_old.fade_length) {
		send_buffer[3] = 0x84;

		send_buffer[4] = fade_length & 0xff;
		send_buffer[5] = fade_length >> 8;

		ret = t500rs_send_int(t500rs->input_dev, send_buffer, &trans);
		if (ret) {
			hid_err(t500rs->hdev, "failed modifying effect envelope\n");
			goto error;
		}
	}

	if (envelope.fade_level != envelope_old.fade_level) {
		send_buffer[3] = 0x88;

		send_buffer[4] = fade_level & 0xff;
		send_buffer[5] = fade_level >> 8;

		ret = t500rs_send_int(t500rs->input_dev, send_buffer, &trans);
		if (ret) {
			hid_err(t500rs->hdev, "failed modifying effect envelope\n");
			goto error;
		}
	}

error:
	return ret;
}

static int t500rs_modify_duration(struct t500rs_device_entry *t500rs,
		struct t500rs_effect_state *state, u8 *send_buffer)
{
	struct ff_effect effect = state->effect;
	struct ff_effect old = state->old;
	u16 duration;
	int ret = 0, trans;

	if (effect.replay.length == 0)
		duration = 0xffff;
	else
		duration = effect.replay.length;

	if (effect.replay.length != old.replay.length) {

		send_buffer[1] = effect.id + 1;
		send_buffer[2] = 0x49;
		send_buffer[4] = 0x41;

		send_buffer[5] = duration & 0xff;
		send_buffer[6] = duration >> 8;

		ret = t500rs_send_int(t500rs->input_dev, send_buffer, &trans);
		if (ret) {
			hid_err(t500rs->hdev, "failed modifying duration\n");
			goto error;
		}
	}
error:
	return ret;
}

static int t500rs_modify_constant(struct t500rs_device_entry *t500rs,
		struct t500rs_effect_state *state, u8 *send_buffer)
{
	struct ff_effect effect = state->effect;
	struct ff_effect old = state->old;
	struct ff_constant_effect constant = effect.u.constant;
	struct ff_constant_effect constant_old = old.u.constant;
	int ret, trans;
	s16 level;

	level = (constant.level * fixp_sin16(effect.direction * 360 / 0x10000)) / 0x7fff;

	if (constant.level != constant_old.level) {

		send_buffer[1] = effect.id + 1;
		send_buffer[2] = 0x0a;

		send_buffer[3] = level & 0xff;
		send_buffer[4] = level >> 8;

		ret = t500rs_send_int(t500rs->input_dev, send_buffer, &trans);
		if (ret) {
			hid_err(t500rs->hdev, "failed modifying constant effect\n");
			goto error;
		}

	}

	ret = t500rs_modify_envelope(t500rs,
			state,
			send_buffer,
			level,
			effect.replay.length,
			effect.id,
			constant.envelope,
			constant_old.envelope
			);
	if (ret) {
		hid_err(t500rs->hdev, "failed modifying constant envelope\n");
		goto error;
	}

	ret = t500rs_modify_duration(t500rs, state, send_buffer);
	if (ret) {
		hid_err(t500rs->hdev, "failed modifying constant duration\n");
		goto error;
	}

error:

	return ret;
}

static int t500rs_modify_ramp(struct t500rs_device_entry *t500rs,
		struct t500rs_effect_state *state, u8 *send_buffer)
{
	struct ff_effect effect = state->effect;
	struct ff_effect old = state->old;
	struct ff_ramp_effect ramp = effect.u.ramp;
	struct ff_ramp_effect ramp_old = old.u.ramp;
	int ret, trans;

	u16 difference, top, bottom;
	s16 level;

	top = ramp.end_level > ramp.start_level ? ramp.end_level : ramp.start_level;
	bottom = ramp.end_level > ramp.start_level ? ramp.start_level : ramp.end_level;


	difference = ((top - bottom) * fixp_sin16(effect.direction * 360 / 0x10000)) / 0x7fff;


	level = (top * fixp_sin16(effect.direction * 360 / 0x10000)) / 0x7fff;

	if (ramp.start_level != ramp_old.start_level || ramp.end_level != ramp_old.end_level) {

		send_buffer[1] = effect.id + 1;
		send_buffer[2] = 0x0e;
		send_buffer[3] = 0x03;

		send_buffer[4] = difference & 0xff;
		send_buffer[5] = difference >> 8;

		send_buffer[6] = level & 0xff;
		send_buffer[7] = level >> 8;

		ret = t500rs_send_int(t500rs->input_dev, send_buffer, &trans);
		if (ret) {
			hid_err(t500rs->hdev, "failed modifying ramp effect\n");
			goto error;
		}

	}

	ret = t500rs_modify_envelope(t500rs,
			state,
			send_buffer,
			level,
			effect.replay.length,
			effect.id,
			ramp.envelope,
			ramp_old.envelope
			);

	if (ret) {
		hid_err(t500rs->hdev, "failed modifying ramp envelope\n");
		goto error;
	}

	ret = t500rs_modify_duration(t500rs, state, send_buffer);
	if (ret) {
		hid_err(t500rs->hdev, "failed modifying ramp duration\n");
		goto error;
	}

error:

	return ret;
}
static int t500rs_modify_damper(struct t500rs_device_entry *t500rs,
		struct t500rs_effect_state *state, u8 *send_buffer)
{
	struct ff_effect effect = state->effect;
	struct ff_effect old = state->old;
	struct ff_condition_effect damper = effect.u.condition[0];
	struct ff_condition_effect damper_old = old.u.condition[0];
	int ret, trans, input_level;

	input_level = damper_level;
	if (state->effect.type == FF_FRICTION)
		input_level = friction_level;

	if (state->effect.type == FF_SPRING)
		input_level = spring_level;

	if (damper.right_coeff != damper_old.right_coeff) {
		s16 coeff = damper.right_coeff * input_level / 100;


		send_buffer[1] = effect.id + 1;
		send_buffer[2] = 0x0e;
		send_buffer[3] = 0x41;

		send_buffer[4] = coeff & 0xff;
		send_buffer[5] = coeff >> 8;

		ret = t500rs_send_int(t500rs->input_dev, send_buffer, &trans);
		if (ret) {
			hid_err(t500rs->hdev, "failed modifying damper rc\n");
			goto error;
		}

	}

	if (damper.left_coeff != damper_old.left_coeff) {
		s16 coeff = damper.left_coeff * input_level / 100;


		send_buffer[1] = effect.id + 1;
		send_buffer[2] = 0x0e;
		send_buffer[3] = 0x42;

		send_buffer[4] = coeff & 0xff;
		send_buffer[5] = coeff >> 8;

		ret = t500rs_send_int(t500rs->input_dev, send_buffer, &trans);
		if (ret) {
			hid_err(t500rs->hdev, "failed modifying damper lc\n");
			goto error;
		}

	}

	if ((damper.deadband != damper_old.deadband)  ||
		(damper.center	 != damper_old.center)) {
		u16 deadband_right = 0xfffe - damper.deadband - damper.center;
		u16 deadband_left = 0xfffe - damper.deadband + damper.center;


		send_buffer[1] = effect.id + 1;
		send_buffer[2] = 0x0e;
		send_buffer[3] = 0x4c;

		send_buffer[4] = deadband_right & 0xff;
		send_buffer[5] = deadband_right >> 8;

		send_buffer[6] = deadband_left & 0xff;
		send_buffer[7] = deadband_left >> 8;

		ret = t500rs_send_int(t500rs->input_dev, send_buffer, &trans);
		if (ret) {
			hid_err(t500rs->hdev, "failed modifying damper deadband\n");
			goto error;
		}

	}

	ret = t500rs_modify_duration(t500rs, state, send_buffer);
	if (ret) {
		hid_err(t500rs->hdev, "failed modifying damper duration\n");
		goto error;
	}

error:

	return ret;
}


static int t500rs_modify_periodic(struct t500rs_device_entry *t500rs,
		struct t500rs_effect_state *state, u8 *send_buffer)
{
	struct ff_effect effect = state->effect;
	struct ff_effect old = state->old;
	struct ff_periodic_effect periodic = effect.u.periodic;
	struct ff_periodic_effect periodic_old = old.u.periodic;
	int ret, trans;
	s16 level;

	level = (periodic.magnitude * fixp_sin16(effect.direction * 360 / 0x10000)) / 0x7fff;


	if (periodic.magnitude != periodic_old.magnitude) {

		send_buffer[1] = effect.id + 1;
		send_buffer[2] = 0x0e;
		send_buffer[3] = 0x01;

		send_buffer[4] = level & 0xff;
		send_buffer[5] = level >> 8;

		ret = t500rs_send_int(t500rs->input_dev, send_buffer, &trans);
		if (ret) {
			hid_err(t500rs->hdev, "failed modifying periodic magnitude\n");
			goto error;
		}

	}

	if (periodic.offset != periodic_old.offset) {
		s16 offset = periodic.offset;


		send_buffer[1] = effect.id + 1;
		send_buffer[2] = 0x0e;
		send_buffer[3] = 0x02;

		send_buffer[4] = offset & 0xff;
		send_buffer[5] = offset >> 8;

		ret = t500rs_send_int(t500rs->input_dev, send_buffer, &trans);
		if (ret) {
			hid_err(t500rs->hdev, "failed modifying periodic offset\n");
			goto error;
		}

	}

	if (periodic.phase != periodic_old.phase) {
		s16 phase = periodic.phase;


		send_buffer[1] = effect.id + 1;
		send_buffer[2] = 0x0e;
		send_buffer[3] = 0x04;

		send_buffer[4] = phase & 0xff;
		send_buffer[5] = phase >> 8;

		ret = t500rs_send_int(t500rs->input_dev, send_buffer, &trans);
		if (ret) {
			hid_err(t500rs->hdev, "failed modifying periodic phase\n");
			goto error;
		}

	}

	if (periodic.period != periodic_old.period) {
		s16 period = periodic.period;


		send_buffer[1] = effect.id + 1;
		send_buffer[2] = 0x0e;
		send_buffer[3] = 0x08;

		send_buffer[4] = period & 0xff;
		send_buffer[5] = period >> 8;

		ret = t500rs_send_int(t500rs->input_dev, send_buffer, &trans);
		if (ret) {
			hid_err(t500rs->hdev, "failed modifying periodic period\n");
			goto error;
		}

	}

	ret = t500rs_modify_envelope(t500rs,
			state,
			send_buffer,
			level,
			effect.replay.length,
			effect.id,
			periodic.envelope,
			periodic_old.envelope);
	if (ret) {
		hid_err(t500rs->hdev, "failed modifying periodic envelope\n");
		goto error;
	}

	ret = t500rs_modify_duration(t500rs, state, send_buffer);
	if (ret) {
		hid_err(t500rs->hdev, "failed modifying periodic duration\n");
		goto error;
	}

error:

	return ret;
}


static int t500rs_upload_constant(struct t500rs_device_entry *t500rs,
		struct t500rs_effect_state *state)
{
	u8 *send_buffer = t500rs->send_buffer;
	struct ff_effect effect = state->effect;
	struct ff_constant_effect constant = state->effect.u.constant;
	s16 level;
	u16 duration, offset;

	int ret, trans;

	/* some games, such as DiRT Rally 2 have a weird feeling to them, sort of
	 * like the wheel pulls just a bit to the right or left and then it just
	 * stops. I wouldn't be surprised if it's got something to do with the
	 * constant envelope, but right now I don't know.
	 */

	if (test_bit(FF_EFFECT_PLAYING, &state->flags)	&&
		test_bit(FF_EFFECT_QUEUE_UPDATE, &state->flags)) {
		__clear_bit(FF_EFFECT_QUEUE_UPLOAD, &state->flags);
		return t500rs_modify_constant(t500rs, state, send_buffer);
	}

	level = (constant.level * fixp_sin16(effect.direction * 360 / 0x10000)) / 0x7fff;
	if (effect.replay.length == 0)
		duration = 0xffff;
	else
		duration = effect.replay.length;

	offset = effect.replay.delay;


	send_buffer[1] = effect.id + 1;
	send_buffer[2] = 0x6a;

	send_buffer[3] = level & 0xff;
	send_buffer[4] = level >> 8;

	t500rs_fill_envelope(send_buffer, 5, level,
			duration, &constant.envelope);

	send_buffer[14] = 0x4f;

	send_buffer[15] = duration & 0xff;
	send_buffer[16] = duration >> 8;

	send_buffer[19] = offset & 0xff;
	send_buffer[20] = offset >> 8;

	send_buffer[22] = 0xff;
	send_buffer[23] = 0xff;

	ret = t500rs_send_int(t500rs->input_dev, send_buffer, &trans);
	if (ret)
		hid_err(t500rs->hdev, "failed uploading constant effect\n");

	return ret;
}

static int t500rs_upload_ramp(struct t500rs_device_entry *t500rs,
		struct t500rs_effect_state *state)
{
	u8 *send_buffer = t500rs->send_buffer;
	struct ff_effect effect = state->effect;
	struct ff_ramp_effect ramp = state->effect.u.ramp;
	int ret, trans;
	u16 difference, offset, top, bottom, duration;
	s16 level;

	if (test_bit(FF_EFFECT_PLAYING, &state->flags)	&&
		test_bit(FF_EFFECT_QUEUE_UPDATE, &state->flags)) {

		__clear_bit(FF_EFFECT_QUEUE_UPLOAD, &state->flags);

		return t500rs_modify_ramp(t500rs, state, send_buffer);
	}

	if (effect.replay.length == 0)
		duration = 0xffff;
	else
		duration = effect.replay.length;

	top = ramp.end_level > ramp.start_level ? ramp.end_level : ramp.start_level;
	bottom = ramp.end_level > ramp.start_level ? ramp.start_level : ramp.end_level;


	difference = ((top - bottom) * fixp_sin16(effect.direction * 360 / 0x10000)) / 0x7fff;
	level = (top * fixp_sin16(effect.direction * 360 / 0x10000)) / 0x7fff;
	offset = effect.replay.delay;


	send_buffer[1] = effect.id + 1;
	send_buffer[2] = 0x6b;

	send_buffer[3] = difference & 0xff;
	send_buffer[4] = difference >> 8;

	send_buffer[5] = level & 0xff;
	send_buffer[6] = level >> 8;

	send_buffer[9] = duration & 0xff;
	send_buffer[10] = duration >> 8;

	send_buffer[12] = 0x80;

	t500rs_fill_envelope(send_buffer, 13, level,
			effect.replay.length, &ramp.envelope);

	send_buffer[22] = ramp.end_level > ramp.start_level ? 0x04 : 0x05;
	send_buffer[23] = 0x4f;

	send_buffer[24] = duration & 0xff;
	send_buffer[25] = duration >> 8;

	send_buffer[28] = offset & 0xff;
	send_buffer[29] = offset >> 8;

	send_buffer[31] = 0xff;
	send_buffer[32] = 0xff;

	ret = t500rs_send_int(t500rs->input_dev, send_buffer, &trans);
	if (ret)
		hid_err(t500rs->hdev, "failed uploading ramp");

	return ret;
}

static int t500rs_upload_spring(struct t500rs_device_entry *t500rs,
		struct t500rs_effect_state *state)
{
	u8 *send_buffer = t500rs->send_buffer;
	struct ff_effect effect = state->effect;
	/* we only care about the first axis */
	struct ff_condition_effect spring = state->effect.u.condition[0];
	int ret, trans;
	u16 duration, right_coeff, left_coeff, deadband_right, deadband_left, offset;

	if (test_bit(FF_EFFECT_PLAYING, &state->flags)	&&
		test_bit(FF_EFFECT_QUEUE_UPDATE, &state->flags)) {

		__clear_bit(FF_EFFECT_QUEUE_UPLOAD, &state->flags);

		return t500rs_modify_damper(t500rs, state, send_buffer);
	}

	if (effect.replay.length == 0)
		duration = 0xffff;
	else
		duration = effect.replay.length;

	send_buffer[1] = effect.id + 1;
	send_buffer[2] = 0x64;

	right_coeff = spring.right_coeff * spring_level / 100;
	left_coeff = spring.left_coeff * spring_level / 100;

	deadband_right = 0xfffe - spring.deadband - spring.center;
	deadband_left = 0xfffe - spring.deadband + spring.center;

	offset = effect.replay.delay;

	send_buffer[3] = right_coeff & 0xff;
	send_buffer[4] = right_coeff >> 8;

	send_buffer[5] = left_coeff & 0xff;
	send_buffer[6] = left_coeff >> 8;

	send_buffer[7] = deadband_right & 0xff;
	send_buffer[8] = deadband_right >> 8;

	send_buffer[9] = deadband_left & 0xff;
	send_buffer[10] = deadband_left >> 8;

	memcpy(&send_buffer[11], spring_values, ARRAY_SIZE(spring_values));
	send_buffer[28] = 0x4f;

	send_buffer[29] = duration & 0xff;
	send_buffer[30] = duration >> 8;

	send_buffer[33] = offset & 0xff;
	send_buffer[34] = offset >> 8;

	send_buffer[36] = 0xff;
	send_buffer[37] = 0xff;

	ret = t500rs_send_int(t500rs->input_dev, send_buffer, &trans);
	if (ret)
		hid_err(t500rs->hdev, "failed uploading spring\n");

	return ret;
}

static int t500rs_upload_damper(struct t500rs_device_entry *t500rs,
		struct t500rs_effect_state *state)
{
	u8 *send_buffer = t500rs->send_buffer;
	struct ff_effect effect = state->effect;
	/* we only care about the first axis */
	struct ff_condition_effect spring = state->effect.u.condition[0];
	int ret, trans, input_level;
	u16 duration, right_coeff, left_coeff, deadband_right, deadband_left, offset;

	if (test_bit(FF_EFFECT_PLAYING, &state->flags)	&&
		test_bit(FF_EFFECT_QUEUE_UPDATE, &state->flags)) {

		__clear_bit(FF_EFFECT_QUEUE_UPLOAD, &state->flags);

		return t500rs_modify_damper(t500rs, state, send_buffer);
	}

	if (effect.replay.length == 0)
		duration = 0xffff;
	else
		duration = effect.replay.length;

	input_level = damper_level;
	if (state->effect.type == FF_FRICTION)
		input_level = friction_level;


	send_buffer[1] = effect.id + 1;
	send_buffer[2] = 0x64;

	right_coeff = spring.right_coeff * input_level / 100;
	left_coeff = spring.left_coeff * input_level / 100;

	deadband_right = 0xfffe - spring.deadband - spring.center;
	deadband_left = 0xfffe - spring.deadband + spring.center;

	offset = effect.replay.delay;

	send_buffer[3] = right_coeff & 0xff;
	send_buffer[4] = right_coeff >> 8;

	send_buffer[5] = left_coeff & 0xff;
	send_buffer[6] = left_coeff >> 8;

	send_buffer[7] = deadband_right & 0xff;
	send_buffer[8] = deadband_right >> 8;

	send_buffer[9] = deadband_left & 0xff;
	send_buffer[10] = deadband_left >> 8;

	memcpy(&send_buffer[11], damper_values, ARRAY_SIZE(damper_values));
	send_buffer[28] = 0x4f;

	send_buffer[29] = duration & 0xff;
	send_buffer[30] = duration >> 8;

	send_buffer[33] = offset & 0xff;
	send_buffer[34] = offset >> 8;

	send_buffer[36] = 0xff;
	send_buffer[37] = 0xff;

	ret = t500rs_send_int(t500rs->input_dev, send_buffer, &trans);
	if (ret)
		hid_err(t500rs->hdev, "failed uploading spring\n");

	return ret;
}

static int t500rs_upload_periodic(struct t500rs_device_entry *t500rs,
		struct t500rs_effect_state *state)
{
	u8 *send_buffer = t500rs->send_buffer;
	struct ff_effect effect = state->effect;
	struct ff_periodic_effect periodic = state->effect.u.periodic;
	int ret, trans;
	u16 duration, magnitude, phase, period, offset;
	s16 periodic_offset;

	if (test_bit(FF_EFFECT_PLAYING, &state->flags)	&&
		test_bit(FF_EFFECT_QUEUE_UPDATE, &state->flags)) {

		__clear_bit(FF_EFFECT_QUEUE_UPLOAD, &state->flags);

		return t500rs_modify_periodic(t500rs, state, send_buffer);
	}

	if (effect.replay.length == 0)
		duration = 0xffff;
	else
		duration = effect.replay.length;

	magnitude = (periodic.magnitude * fixp_sin16(effect.direction * 360 / 0x10000)) / 0x7fff;

	phase = periodic.phase;
	periodic_offset = periodic.offset;
	period = periodic.period;
	offset = effect.replay.delay;


	send_buffer[1] = effect.id + 1;
	send_buffer[2] = 0x6b;

	send_buffer[3] = magnitude & 0xff;
	send_buffer[4] = magnitude >> 8;

	send_buffer[7] = phase & 0xff;
	send_buffer[8] = phase >> 8;

	send_buffer[5] = periodic_offset & 0xff;
	send_buffer[6] = periodic_offset >> 8;

	send_buffer[9] = period & 0xff;
	send_buffer[10] = period >> 8;

	send_buffer[12] = 0x80;

	t500rs_fill_envelope(send_buffer, 13, magnitude,
			effect.replay.length, &periodic.envelope);

	send_buffer[21] = periodic.waveform - 0x57;
	send_buffer[22] = 0x4f;

	send_buffer[23] = duration & 0xff;
	send_buffer[24] = duration >> 8;

	send_buffer[27] = offset & 0xff;
	send_buffer[28] = offset >> 8;

	send_buffer[30] = 0xff;
	send_buffer[31] = 0xff;

	ret = t500rs_send_int(t500rs->input_dev, send_buffer, &trans);
	if (ret)
		hid_err(t500rs->hdev, "failed uploading periodic effect");

	return ret;
}

static int t500rs_upload_effect(struct t500rs_device_entry *t500rs,
		struct t500rs_effect_state *state)
{
	switch (state->effect.type) {
	case FF_CONSTANT:
		return t500rs_upload_constant(t500rs, state);
	case FF_RAMP:
		return t500rs_upload_ramp(t500rs, state);
	case FF_SPRING:
		return t500rs_upload_spring(t500rs, state);
	case FF_DAMPER:
	case FF_FRICTION:
	case FF_INERTIA:
		return t500rs_upload_damper(t500rs, state);
	case FF_PERIODIC:
		return t500rs_upload_periodic(t500rs, state);
	default:
		hid_err(t500rs->hdev, "invalid effect type: %x", state->effect.type);
		return -1;
	}
}

static int t500rs_timer_helper(struct t500rs_device_entry *t500rs)
{
	struct t500rs_effect_state *state;
	unsigned long jiffies_now = JIFFIES2MS(jiffies);
	int max_count = 0, effect_id, ret;

	for (effect_id = 0; effect_id < T500RS_MAX_EFFECTS; ++effect_id) {

		state = &t500rs->states[effect_id];

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

			ret = t500rs_upload_effect(t500rs, state);
			if (ret) {
				hid_err(t500rs->hdev, "failed uploading effects");
				return ret;
			}
		}

		if (test_bit(FF_EFFECT_QUEUE_START, &state->flags)) {
			__clear_bit(FF_EFFECT_QUEUE_START, &state->flags);
			__set_bit(FF_EFFECT_PLAYING, &state->flags);

			ret = t500rs_play_effect(t500rs, state);
			if (ret) {
				hid_err(t500rs->hdev, "failed starting effects\n");
				return ret;
			}

		}

		if (test_bit(FF_EFFECT_QUEUE_STOP, &state->flags)) {
			__clear_bit(FF_EFFECT_QUEUE_STOP, &state->flags);
			__clear_bit(FF_EFFECT_PLAYING, &state->flags);

			ret = t500rs_stop_effect(t500rs, state);
			if (ret) {
				hid_err(t500rs->hdev, "failed stopping effect\n");
				return ret;
			}
		}

		if (state->count > max_count)
			max_count = state->count;
	}

	return max_count;
}

static enum hrtimer_restart t500rs_timer(struct hrtimer *t)
{
	struct t500rs_device_entry *t500rs = container_of(t, struct t500rs_device_entry, hrtimer);
	int max_count;

	max_count = t500rs_timer_helper(t500rs);

	if (max_count > 0) {
		hrtimer_forward_now(&t500rs->hrtimer, ms_to_ktime(timer_msecs));
		return HRTIMER_RESTART;
	} else {
		return HRTIMER_NORESTART;
	}
}

static int t500rs_upload(struct input_dev *dev,
		struct ff_effect *effect, struct ff_effect *old)
{
	struct hid_device *hdev = input_get_drvdata(dev);
	struct t500rs_device_entry *t500rs;
	struct t500rs_effect_state *state;

	t500rs = t500rs_get_device(hdev);
	if (!t500rs) {
		hid_err(hdev, "could not get device\n");
		return -1;
	}

	if (effect->type == FF_PERIODIC && effect->u.periodic.period == 0)
		return -EINVAL;

	state = &t500rs->states[effect->id];

	spin_lock_irqsave(&t500rs->lock, t500rs->lock_flags);

	state->effect = *effect;

	if (old) {
		state->old = *old;
		__set_bit(FF_EFFECT_QUEUE_UPDATE, &state->flags);
	} else {
		__clear_bit(FF_EFFECT_QUEUE_UPDATE, &state->flags);
	}
	__set_bit(FF_EFFECT_QUEUE_UPLOAD, &state->flags);

	spin_unlock_irqrestore(&t500rs->lock, t500rs->lock_flags);

	return 0;
}

static int t500rs_play(struct input_dev *dev, int effect_id, int value)
{
	struct hid_device *hdev = input_get_drvdata(dev);
	struct t500rs_device_entry *t500rs;
	struct t500rs_effect_state *state;

	t500rs = t500rs_get_device(hdev);
	if (!t500rs) {
		hid_err(hdev, "could not get device\n");
		return -1;
	}

	state = &t500rs->states[effect_id];

	if (&state->effect == 0)
		return 0;

	spin_lock_irqsave(&t500rs->lock, t500rs->lock_flags);

	if (value > 0) {
		state->count = value;
		state->start_time = JIFFIES2MS(jiffies);
		__set_bit(FF_EFFECT_QUEUE_START, &state->flags);

		if (test_bit(FF_EFFECT_QUEUE_STOP, &state->flags))
			__clear_bit(FF_EFFECT_QUEUE_STOP, &state->flags);

	} else {
		__set_bit(FF_EFFECT_QUEUE_STOP, &state->flags);
	}

	if (!hrtimer_active(&t500rs->hrtimer))
		hrtimer_start(&t500rs->hrtimer, ms_to_ktime(timer_msecs), HRTIMER_MODE_REL);

	spin_unlock_irqrestore(&t500rs->lock, t500rs->lock_flags);
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
	struct t500rs_device_entry *t500rs;
	u8 *send_buffer;
	unsigned int range;
	int ret, trans;

	ret = kstrtouint(buf, 0, &range);
    if (ret) {
        hid_err(hdev, "kstrtouint failed at range_store: %i", ret);
        return ret;
    }

	t500rs = t500rs_get_device(hdev);
	if (!t500rs) {
		hid_err(hdev, "could not get device\n");
		return -1;
	}

	send_buffer = t500rs->send_buffer;

	if (range < 40)
		range = 40;

	if (range > 1080)
		range = 1080;

	range *= 0x3c;


	send_buffer[0] = 0x08;
	send_buffer[1] = 0x11;
	send_buffer[2] = range & 0xff;
	send_buffer[3] = range >> 8;

	ret = t500rs_send_int(t500rs->input_dev, send_buffer, &trans);
	if (ret) {
		hid_err(hdev, "failed sending interrupts\n");
		return -1;
	}

	t500rs->range = range / 0x3c;

    hid_info(hdev, "Current range is [%i] \n", t500rs->range);

	return count;
}

static ssize_t range_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct hid_device *hdev = to_hid_device(dev);
	struct t500rs_device_entry *t500rs;
	size_t count = 0;

	t500rs = t500rs_get_device(hdev);
	if (!t500rs) {
		hid_err(hdev, "could not get device\n");
		return -1;
	}

	count = scnprintf(buf, PAGE_SIZE, "%u\n", t500rs->range);
	return count;
}

static DEVICE_ATTR_RW(range);

static void t500rs_set_autocenter(struct input_dev *dev, u16 value)
{
	struct hid_device *hdev = input_get_drvdata(dev);
	struct t500rs_device_entry *t500rs;
	u8 *send_buffer;
	int ret, trans;

    hid_info(hdev, "Begin autocenter \n");

	t500rs = t500rs_get_device(hdev);
	if (!t500rs) {
		hid_err(hdev, "could not get device\n");
		return;
	}

	send_buffer = t500rs->send_buffer;

	send_buffer[0] = 0x08;
	send_buffer[1] = 0x04;
	send_buffer[2] = 0x01;

	ret = t500rs_send_int(dev, send_buffer, &trans);
	if (ret) {
		hid_err(hdev, "failed setting autocenter");
		return;
	}

	send_buffer[0] = 0x08;
	send_buffer[1] = 0x03;

	send_buffer[2] = value & 0xff;
	send_buffer[3] = value >> 8;

	ret = t500rs_send_int(dev, send_buffer, &trans);
	if (ret)
		hid_err(hdev, "failed setting autocenter");

    hid_info(hdev, "set autocenter \n");
}

static void t500rs_set_gain(struct input_dev *dev, u16 gain)
{
	struct hid_device *hdev = input_get_drvdata(dev);
	struct t500rs_device_entry *t500rs;
	u8 *send_buffer;
	int ret, trans;

    hid_info(hdev, "Starting set gain phase\n");

	t500rs = t500rs_get_device(hdev);
	if (!t500rs) {
		hid_err(hdev, "could not get device\n");
		return;
	}

	send_buffer = t500rs->send_buffer;
	send_buffer[0] = 0x02;
	send_buffer[1] = SCALE_VALUE_U16(gain, 8);

	ret = t500rs_send_int(dev, send_buffer, &trans);
	if (ret)
		hid_err(hdev, "failed setting gain: %i\n", ret);

    hid_info(hdev, "Ending set gain phase\n");
}

static void t500rs_destroy(struct ff_device *ff)
{
	// maybe not necessary?
}

static int t500rs_open(struct input_dev *dev)
{
	struct t500rs_device_entry *t500rs;
	struct hid_device *hdev = input_get_drvdata(dev);
	u8 *send_buffer;
	int ret, trans;

    hid_info(hdev, "Starting open phase\n");

	t500rs = t500rs_get_device(hdev);
	if (!t500rs) {
		hid_err(hdev, "could not get device\n");
		return -1;
	}

	send_buffer = t500rs->send_buffer;

	send_buffer[0] = 0x01;
	send_buffer[1] = 0x05;

	ret = t500rs_send_int(dev, send_buffer, &trans);
	if (ret) {
		hid_err(hdev, "failed sending interrupts\n");
		goto err;
	}
    hid_info(hdev, "Ending open phase\n");

err:
	return t500rs->open(dev);
}

static void t500rs_close(struct input_dev *dev)
{
	int ret, trans;
	struct hid_device *hdev = input_get_drvdata(dev);
	struct t500rs_device_entry *t500rs;
	u8 *send_buffer;

    hid_info(hdev, "Starting close phase\n");

	t500rs = t500rs_get_device(hdev);
	if (!t500rs) {
		hid_err(hdev, "could not get device\n");
		return;
	}

	send_buffer = t500rs->send_buffer;

	send_buffer[0] = 0x01;

	ret = t500rs_send_int(dev, send_buffer, &trans);
	if (ret) {
		hid_err(hdev, "failed sending interrupts\n");
		goto err;
	}
    hid_info(hdev, "Ending close phase... ok\n");
err:
	t500rs->close(dev);
}

static int t500rs_create_files(struct hid_device *hdev)
{
	int ret, ret2, ret3, ret4;

    hid_info(hdev, "Before creation of files\n");

	ret = device_create_file(&hdev->dev, &dev_attr_range);
    hid_info(hdev, "Creation of range file [%i]\n", ret);
	if (ret) {
		hid_warn(hdev, "unable to create sysfs interface for range\n");
		goto attr_range_err;
	}

	ret2 = device_create_file(&hdev->dev, &dev_attr_spring_level);
    hid_info(hdev, "Creation of spring file [%i]\n", ret2);
	if (ret2) {
		hid_warn(hdev, "unable to create sysfs interface for spring_level\n");
		goto attr_spring_err;
	}

	ret3 = device_create_file(&hdev->dev, &dev_attr_damper_level);
    hid_info(hdev, "Creation of damper file [%i]\n", ret3);
	if (ret3) {
		hid_warn(hdev, "unable to create sysfs interface for damper_level\n");
		goto attr_damper_err;
	}

	ret4 = device_create_file(&hdev->dev, &dev_attr_friction_level);
    hid_info(hdev, "Creation of friction file [%i]\n", ret4);
	if (ret4) {
		hid_warn(hdev, "unable to create sysfs interface for friction_level\n");
		goto attr_friction_err;
	}

	return ret && ret2 && ret3 && ret4;

	// if the creation of dev_attr_friction fails, we don't need to remove it
	// device_remove_file(&hdev->dev, &dev_attr_friction_level);
attr_friction_err:
	device_remove_file(&hdev->dev, &dev_attr_damper_level);
attr_damper_err:
	device_remove_file(&hdev->dev, &dev_attr_spring_level);
attr_spring_err:
	device_remove_file(&hdev->dev, &dev_attr_range);
attr_range_err:
	return ret;
}

static int t500rs_init(struct hid_device *hdev, const signed short *ff_bits)
{
	struct t500rs_device_entry *t500rs;
	struct t500rs_data *drv_data;
	struct list_head *report_list;
	struct hid_input *hidinput = list_entry(hdev->inputs.next,
			struct hid_input, list);
	struct input_dev *input_dev = hidinput->input;
	struct device *dev = &hdev->dev;
	struct usb_interface *usbif = to_usb_interface(dev->parent);
	struct usb_device *usbdev = interface_to_usbdev(usbif);
	struct ff_device *ff;
	char range[10] = "1024"; // max
	int i, ret;

	drv_data = hid_get_drvdata(hdev);
	if (!drv_data) {
		hid_err(hdev, "private driver data not allocated\n");
		ret = -ENOMEM;
		goto err;
	}

	t500rs = kzalloc(sizeof(struct t500rs_device_entry), GFP_KERNEL);
	if (!t500rs) {
		ret = -ENOMEM;
		goto t500rs_err;
	}

	t500rs->input_dev = input_dev;
	t500rs->hdev = hdev;
	t500rs->usbdev = usbdev;
	t500rs->usbif = usbif;

	t500rs->states = kzalloc(
			sizeof(struct t500rs_effect_state) * T500RS_MAX_EFFECTS, GFP_KERNEL);

	if (!t500rs->states) {
		ret = -ENOMEM;
		goto states_err;
	}

	t500rs->send_buffer = kzalloc(T500RS_BUFFER_LENGTH, GFP_KERNEL);
	if (!t500rs->send_buffer) {
		ret = -ENOMEM;
		goto send_err;
	}

	t500rs->firmware_response = kzalloc(sizeof(struct t500rs_firmware_response), GFP_KERNEL);
	if (!t500rs->firmware_response) {
		ret = -ENOMEM;
		goto firmware_err;
	}

	// Check firmware version
	ret = usb_control_msg(t500rs->usbdev,
			usb_rcvctrlpipe(t500rs->usbdev, 0),
			t500rs_firmware_request.bRequest,
			t500rs_firmware_request.bRequestType,
			t500rs_firmware_request.wValue,
			t500rs_firmware_request.wIndex,
			t500rs->firmware_response,
			t500rs_firmware_request.wLength,
			USB_CTRL_SET_TIMEOUT
			   );

	// Educated guess
    hid_info(hdev, "Current firmware version: %i", t500rs->firmware_response->firmware_version);
	if (t500rs->firmware_response->firmware_version < 31 && ret >= 0) {
		hid_err(t500rs->hdev,
			"firmware version %i is too old, please update.",
			t500rs->firmware_response->firmware_version
			);

		hid_info(t500rs->hdev, "note: this has to be done through Windows.");

		ret = -EINVAL;
		goto out;
	}

	spin_lock_init(&t500rs->lock);

	drv_data->device_props = t500rs;

	report_list = &hdev->report_enum[HID_OUTPUT_REPORT].report_list;

	// because we set the rdesc, we know exactly which report and field to use
	t500rs->report = list_entry(report_list->next, struct hid_report, list);
	t500rs->ff_field = t500rs->report->field[0];

	// set ff capabilities
	for (i = 0; ff_bits[i] >= 0; ++i)
		__set_bit(ff_bits[i], input_dev->ffbit);

	ret = input_ff_create(input_dev, T500RS_MAX_EFFECTS);
	if (ret) {
		hid_err(hdev, "could not create input_ff\n");
		goto out;
	}

	ff = input_dev->ff;
	ff->upload = t500rs_upload;
	ff->playback = t500rs_play;
	ff->set_gain = t500rs_set_gain;
	ff->set_autocenter = t500rs_set_autocenter;
	ff->destroy = t500rs_destroy;

	t500rs->open = input_dev->open;
	t500rs->close = input_dev->close;

	input_dev->open = t500rs_open;
	input_dev->close = t500rs_close;

    hid_info(hdev, "Before create files\n");
	ret = t500rs_create_files(hdev);
	if (ret) {
		// this might not be a catastrophic issue, but it could affect
		// programs such as oversteer, best play it safe
		hid_err(hdev, "could not create sysfs files\n");
		goto out;
	}
    hid_info(hdev, "After create files\n");

    hid_info(hdev, "Before timer init\n");
	hrtimer_init(&t500rs->hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	t500rs->hrtimer.function = t500rs_timer;

    hid_info(hdev, "Before range store\n");
	range_store(dev, &dev_attr_range, range, 10);
	t500rs_set_gain(input_dev, 0xffff);

	hid_info(hdev, "force feedback for t500rs\n");
	return 0;

out:
	kfree(t500rs->firmware_response);
firmware_err:
	kfree(t500rs->send_buffer);
send_err:
	kfree(t500rs->states);
states_err:
	kfree(t500rs);
t500rs_err:
	kfree(drv_data);
err:
	hid_err(hdev, "failed creating force feedback device\n");
	return ret;

}

static int t500rs_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	int ret;
	struct t500rs_data *drv_data;

	spin_lock_init(&lock);

	drv_data = kzalloc(sizeof(struct t500rs_data), GFP_KERNEL);
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

	ret = t500rs_init(hdev, (void *)id->driver_data);
	if (ret) {
		hid_err(hdev, "t500rs_init failed\n");
		goto err;
	}
	return 0;
err:
	return ret;
}

static void t500rs_remove(struct hid_device *hdev)
{
	struct t500rs_device_entry *t500rs;
	struct t500rs_data *drv_data;

	drv_data = hid_get_drvdata(hdev);
	t500rs = t500rs_get_device(hdev);
	if (!t500rs) {
		hid_err(hdev, "could not get device\n");
		return;
	}

	hrtimer_cancel(&t500rs->hrtimer);

	device_remove_file(&hdev->dev, &dev_attr_range);
	device_remove_file(&hdev->dev, &dev_attr_spring_level);
	device_remove_file(&hdev->dev, &dev_attr_damper_level);
	device_remove_file(&hdev->dev, &dev_attr_friction_level);

	hid_hw_stop(hdev);
	kfree(t500rs->states);
	kfree(t500rs->send_buffer);
	kfree(t500rs->firmware_response);
	kfree(t500rs);
	kfree(drv_data);
}

static __u8 *t500rs_report_fixup(struct hid_device *hdev, __u8 *rdesc, unsigned int *rsize)
{
	rdesc = t500rs_rdesc_fixed;
	*rsize = sizeof(t500rs_rdesc_fixed);
	return rdesc;
}

static const struct hid_device_id t500rs_devices[] = {
	{HID_USB_DEVICE(USB_VENDOR_ID_THRUSTMASTER, 0xb65e),
		.driver_data = (unsigned long)t500rs_ff_effects},
	{}
};
MODULE_DEVICE_TABLE(hid, t500rs_devices);

static struct hid_driver t500rs_driver = {
	.name = "t500rs",
	.id_table = t500rs_devices,
	.probe = t500rs_probe,
	.remove = t500rs_remove,
	.report_fixup = t500rs_report_fixup,
};
module_hid_driver(t500rs_driver);

MODULE_LICENSE("GPL");
