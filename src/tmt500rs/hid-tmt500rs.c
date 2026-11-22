// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  HID driver for Thrustmaster T500RS wheel base that provides Force feedback
 *
 *  Protocol documentation: docs/T500RS_FFBEFFECTS.md
 *
 *  Reports observed in Windows captures that this driver deliberately does
 *  NOT produce or parse:
 *  - 0x0a (OUT, F1 rim only, 6x during init): attachment activation handshake
 *  - 42 01 00 (OUT, 9/15/32-byte variants): protocol re-sync / reset
 *  - 0x07 (IN, 230 Hz state report): handled by the stock HID parser
 *  - 0x14 (IN, 6x during init): device-identification report
 *  - vendor request 0x49 (host-polled during init): capability reply
 *
 *  Copyright (c) 2025 Casimir Bonnet <casimir.bonnet@gmail.com>
 */

#include "hid-tmt500rs.h"
#include "../hid-tmff2.h"
#include <linux/hid.h>
#include <linux/input.h>
#include <linux/jiffies.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>

/* T500 RS only: power-on gain in percent. Default 100 keeps the init
 * 0x43 packet byte-identical to the historical hardcoded 0xff; Windows
 * seeds 90%.
 */
int default_gain = 100;
module_param(default_gain, int, 0);
MODULE_PARM_DESC(default_gain,
		"T500 RS init gain in percent (0-100, default 100)");

/* Packet sequence templates for each effect type. */
static const enum t500rs_seq_packet t500rs_seq_constant[] = {
	T500RS_SEQ_ENVELOPE,
	T500RS_SEQ_CONSTANT,
	T500RS_SEQ_MAIN,
};

/* Scale constant level (-32767..32767) to signed 8-bit (-127..127) */
static inline s8 t500rs_scale_const_level_s8(int level)
{
	/* Input validation and clamping */
	if (level > 32767)
		level = 32767;
	if (level < -32767)
		level = -32767;

	/* Use 32-bit arithmetic to prevent overflow */
	return (s8)((level * 127LL) / 32767);
}

/* Fold the effect direction into the level's sign and convert to s8.
 * The wire byte is UAPI-standard: positive = rightward pull.
 * A wheel has one force axis, so direction only picks left/right -
 * never sin()-scale the magnitude: games that encode force sign as
 * polar 0/180deg (kernel direction 0x0000/0x8000, e.g. the rFactor
 * family) sit exactly where sin() == 0 and would be silenced.
 * See docs/T500RS_FFBEFFECTS.md section 7.
 */
static inline s8 t500rs_scale_const_with_direction(int level, u16 direction)
{
	if (fixp_sin16(direction * 360 / 0x10000) < 0)
		level = -level;

	return t500rs_scale_const_level_s8(level);
}

/* Debug logging helper: pass struct t500rs_device_entry * explicitly */
#define T500RS_DBG(dev, fmt, ...) hid_dbg((dev)->hdev, fmt, ##__VA_ARGS__)

/* T500RS device data */
struct t500rs_device_entry {
	struct hid_device *hdev;
	struct input_dev *input_dev;

	u8 *send_buffer;
	size_t buffer_length;

	/*
	 * Software-expiry tracker. The T500RS hardware never auto-stops an
	 * effect: once STARTed it runs until an explicit 0x41 STOP. The tmff2
	 * core relies on hardware auto-stop (which T500RS lacks), so we enforce
	 * replay.length here. A single re-arming delayed_work scans active[]
	 * and sends a per-slot 0x41 STOP when each finite effect's time elapses.
	 *
	 * STOP is per-slot: the 0x41 effect_id byte addresses one slot at
	 * a time. There is no need for a "global STOP" or a playing-flag
	 * guard, because each STOP only halts its own effect.
	 *
	 * expiry_buffer is a dedicated DMA-safe buffer so the worker (which runs
	 * outside the core FFB worker) never races send_buffer (mirrors the
	 * set_range pattern).
	 */
	spinlock_t expiry_lock;
	struct delayed_work expiry_work;
	u8 *expiry_buffer;
	struct t500rs_active_effect {
		bool active; /* has a finite expiry deadline (drives expiry_work) */
		u8 hw_id; /* hardware slot to STOP when this expires */
		unsigned long start_ms; /* jiffies_to_msecs(jiffies) at play */
		unsigned long total_ms; /* (delay+length)*count; 0 == infinite */
	} active[T500RS_MAX_EFFECTS];

};

/*
 * Build a protocol-accurate 0x01 main upload packet.
 *
 * Per the T500RS USB protocol documentation:
 * - effect_id: 16-bit LE hardware effect slot (0..15 for now)
 * - duration_ms: duration in milliseconds
 * - delay_ms: delay before effect starts
 * - param_sub: parameter subtype (used by 0x03/0x04/0x05)
 * - envelope_sub: envelope subtype (used by 0x02)
 *
 * Effect type values this driver puts on the wire:
 * - 0x00 = Constant
 *
 * NOTE: Direction is sent separately in a 0x03 packet for constant force,
 * not in this 0x01 packet.
 *
 * This is a pure constructor: callers must validate effect_id/effect_type
 * beforehand (the MAIN sequence step derives both from validated effect
 * fields; effect_id mirrors the hardware slot index via
 * t500rs_effect_to_hw_id()).
 */
static void t500rs_build_r01_main(struct t500rs_pkt_r01_main *p, u8 effect_id,
				  u8 effect_type, u16 duration_ms, u16 delay_ms,
				  u16 param_sub, u16 envelope_sub)
{
	memset(p, 0, sizeof(*p));
	p->id = T500RS_PKT_MAIN;
	p->effect_id = effect_id;
	p->effect_type = effect_type;
	p->control = T500RS_CONTROL_DEFAULT;
	p->duration_ms = cpu_to_le16(duration_ms);
	p->delay_ms = cpu_to_le16(delay_ms);
	p->reserved1 = 0;
	p->packet_code_1 = cpu_to_le16(param_sub);
	p->packet_code_2 = cpu_to_le16(envelope_sub);
	p->reserved2 = 0;
}

/* Forward declarations for functions used by helper functions */
static int t500rs_send_hid(struct t500rs_device_entry *t500rs, u8 *data,
			   size_t len);
static int t500rs_send_stop(struct t500rs_device_entry *t500rs, u8 effect_id);
static int t500rs_send_start(struct t500rs_device_entry *t500rs, u8 effect_id);
static int t500rs_send_stop_now(struct t500rs_device_entry *t500rs, u8 *buf,
				u8 effect_id);
static int t500rs_send_start_now(struct t500rs_device_entry *t500rs, u8 *buf,
				 u8 effect_id);
static void t500rs_expiry_work(struct work_struct *work);
static void t500rs_build_r03_constant(struct t500rs_r03_const *p, u8 code,
				      s8 level);
static void t500rs_build_r02_envelope(struct t500rs_pkt_r02_envelope *p,
				      u8 subtype, const struct ff_envelope *env,
				      bool allow_nonzero);

/*
 * Build and send a 0x03 constant force packet.
 *
 * This helper function encapsulates the common pattern of building and
 * sending a constant force packet, reducing code duplication and improving
 * maintainability. Handles level scaling with direction projection.
 *
 * Parameters:
 * - t500rs: Device context
 * - buf: Buffer to use for packet construction
 * - code: Packet code (from param_sub)
 * - level: Constant force level (-32767 to 32767)
 * - direction: Effect direction (0-65535)
 *
 * Returns: 0 on success, negative errno on failure
 */
static int t500rs_send_constant_packet(struct t500rs_device_entry *t500rs,
				       u8 *buf, u8 code,
				       s16 level, u16 direction)
{
	struct t500rs_r03_const *r3;
	s8 scaled_level;

	if (!t500rs || !buf)
		return -EINVAL;

	/* Scale level with direction projection */
	scaled_level = t500rs_scale_const_with_direction(level, direction);

	/* Build and send packet */
	r3 = (struct t500rs_r03_const *)buf;
	t500rs_build_r03_constant(r3, code, scaled_level);

	return t500rs_send_hid(t500rs, buf, sizeof(*r3));
}

/*
 * Build and send a 0x02 envelope packet.
 *
 * This helper function encapsulates the common pattern of building and
 * sending an envelope packet, reducing code duplication and improving
 * maintainability. Determines envelope availability based on effect type.
 *
 * Per firmware behavior, only ramp effects support non-zero envelope values.
 * Periodic and constant effects must send zero envelope values due to
 * firmware limitations.
 *
 * Parameters:
 * - t500rs: Device context
 * - buf: Buffer to use for packet construction
 * - subtype: Envelope subtype (from env_sub)
 * - effect: Effect containing envelope parameters
 *
 * Returns: 0 on success, negative errno on failure
 */
static int t500rs_send_envelope_packet(struct t500rs_device_entry *t500rs,
				       u8 *buf, u8 subtype,
				       const struct ff_effect *effect)
{
	struct t500rs_pkt_r02_envelope *env;
	const struct ff_envelope *envelope = NULL;
	bool allow_envelope = false;

	if (!t500rs || !buf || !effect)
		return -EINVAL;

	/* Constant force must send an all-zero envelope: the firmware
	 * handles only zero envelope fields for it. */
	envelope = &effect->u.constant.envelope;
	allow_envelope = false;

	/* Build and send envelope packet */
	env = (struct t500rs_pkt_r02_envelope *)buf;
	t500rs_build_r02_envelope(env, subtype, envelope, allow_envelope);

	return t500rs_send_hid(t500rs, buf, sizeof(*env));
}

/*
 * Build a 0x03 constant force packet.
 *
 * Per the T500RS USB protocol documentation:
 * - code: low byte of param_subtype from 0x01 (e.g., 0x0e)
 * - reserved: always 0x00
 * - level: signed -127 to +127
 */
static void t500rs_build_r03_constant(struct t500rs_r03_const *p, u8 code,
				      s8 level)
{
	p->id = T500RS_PKT_CONSTANT;
	p->code = code;
	p->zero = 0x00;
	p->level = level;
}

/*
 * Scale envelope level from Linux FFB subsystem format to device format.
 * Linux FFB : 0-32767
 * Device: 0-255
 * Formula: device_level = os_ffb_level * 255 / 32767
 */
static inline u8 t500rs_scale_envelope_level(u16 os_ffb_level)
{
	/* Input validation and clamping */
	if (os_ffb_level > 32767)
		os_ffb_level = 32767;

	/* Use long long arithmetic to prevent overflow */
	return (u8)((os_ffb_level * 255LL) / 32767);
}

/*
 * Build a protocol-accurate 0x02 envelope packet.
 *
 * Per the T500RS USB protocol documentation:
 * - subtype: low byte of env_sub from 0x01 (e.g., 0x1c)
 * - attack_len: attack duration in milliseconds
 * - attack_level: 0-255 (scaled from Linux FFB 0-32767)
 * - fade_len: fade duration in milliseconds
 * - fade_level: 0-255 (scaled from Linux FFB 0-32767)
 * - reserved: always 0x00
 */
static void t500rs_build_r02_envelope(struct t500rs_pkt_r02_envelope *p,
				      u8 subtype, const struct ff_envelope *env,
				      bool allow_nonzero)
{
	memset(p, 0, sizeof(*p));
	p->id = 0x02;
	p->subtype = subtype;

	/*
	* The Windows driver always sends all-zero envelopes for periodic
	* and constant effects; non-zero values have never been observed on
	* the wire for those types. Only ramp effects may carry a non-zero
	* envelope (allow_nonzero); anything else the game asked for is
	* warned about once and dropped.
	*/
	if (env && allow_nonzero) {
		p->attack_len = cpu_to_le16(env->attack_length);
		p->attack_level =
			t500rs_scale_envelope_level(env->attack_level);
		p->fade_len = cpu_to_le16(env->fade_length);
		p->fade_level = t500rs_scale_envelope_level(env->fade_level);
	} else if (env && (env->attack_length || env->attack_level ||
			   env->fade_length || env->fade_level)) {
		/* The user supplied a non-zero envelope that the device cannot
		 * apply to this effect type (firmware rejects it with EPROTO).
		 * Warn once and silently drop it by sending zeros.
		 */
		pr_warn_once(
			"t500rs: non-zero envelope ignored for this effect type\n");
	}
	/* else: zero/no envelope -> sending zeros is normal protocol behavior */
}

/* Supported parameters */
static unsigned long t500rs_params = PARAM_GAIN | PARAM_RANGE;

/* Supported effects. */
const signed short t500rs_effects[] = { FF_CONSTANT,
					FF_GAIN,       FF_AUTOCENTER,
					-1 };

/*
 * Resolve the hardware effect slot index for a given effect.
 *
 * Constant force is pinned to slot 0 (its subtypes are fixed in the
 * firmware: param_sub=0x000e, env_sub=0x001c - see
 * docs/T500RS_FFBEFFECTS.md section 4).
 */
static u8 t500rs_effect_to_hw_id(const struct ff_effect *effect)
{
	return 0;
}

/*
 * Send a sequence of packets for effect upload.
 * Abstracts the hardcoded packet orders in upload functions.
 *
 * The 0x01 effect_id byte and the 0x41 START/STOP effect_id byte both mirror
 * the hardware slot derived above. Constant force uses fixed subtypes
 * (T500RS_CONSTANT_PARAM_SUB/ENV_SUB).
 */
static int t500rs_send_packet_sequence(struct t500rs_device_entry *t500rs,
				       const struct tmff2_effect_state *state,
				       const enum t500rs_seq_packet *sequence,
				       size_t seq_len)
{
	const struct ff_effect *effect = &state->effect;
	u8 *buf = t500rs->send_buffer;
	u8 hw_id = t500rs_effect_to_hw_id(effect);
	u16 param_sub = T500RS_CONSTANT_PARAM_SUB;
	u16 env_sub = T500RS_CONSTANT_ENV_SUB;
	int ret;

	for (size_t i = 0; i < seq_len; i++) {
		/* Log sequence progress for debugging */
		T500RS_DBG(t500rs,
			   "Sequence step %zu/%zu: packet type 0x%02x\n", i + 1,
			   seq_len, sequence[i]);

		switch (sequence[i]) {
		case T500RS_SEQ_ENVELOPE: {
			ret = t500rs_send_envelope_packet(t500rs, buf,
							  (u8)env_sub, effect);
			break;
		}

		case T500RS_SEQ_CONSTANT: {
			ret = t500rs_send_constant_packet(t500rs, buf,
							  (u8)param_sub,
							  effect->u.constant.level,
							  effect->direction);
			break;
		}

		case T500RS_SEQ_MAIN: {
			u8 effect_type = T500RS_EFFECT_CONSTANT;
			u16 duration_ms = effect->replay.length ?
						  effect->replay.length :
						  0xffff;
			u16 delay_ms = effect->replay.delay;

			struct t500rs_pkt_r01_main *m =
				(struct t500rs_pkt_r01_main *)buf;
			t500rs_build_r01_main(m, hw_id, effect_type,
					      duration_ms, delay_ms, param_sub,
					      env_sub);

			ret = t500rs_send_hid(
				t500rs, buf,
				sizeof(struct t500rs_pkt_r01_main));
			break;
		}

		default:
			ret = -EINVAL;
		}

		if (ret) {
			hid_err(t500rs->hdev,
				"Sequence failed at step %zu/%zu (packet type 0x%02x): %d\n",
				i + 1, seq_len, sequence[i], ret);
			return ret;
		}
	}

	T500RS_DBG(t500rs, "Sequence completed successfully (%zu packets)\n",
		   seq_len);
	return 0;
}

static int t500rs_set_gain(void *data, u16 gain)
{
	struct t500rs_device_entry *t500rs = data;
	u8 *buf;
	u8 device_gain_byte;
	int ret;

	if (!t500rs->send_buffer) {
		hid_err(t500rs->hdev, "t500rs_set_gain: NULL send buffer\n");
		return -ENOMEM;
	}

	buf = t500rs->send_buffer;

	/* Scale 0..65535 to device 0..255 */
	device_gain_byte = (u8)((gain * 255ULL) / T500RS_GAIN_MAX);

	/* Per-frame gain changes are common; keep this at dbg to avoid
	 * flooding dmesg. */
	hid_dbg(t500rs->hdev, "FFB: set_gain %u -> device %u\n", gain,
		device_gain_byte);

	buf[0] = T500RS_PKT_GAIN;
	buf[1] = device_gain_byte;

	ret = t500rs_send_hid(t500rs, buf, 2);
	if (ret)
		hid_err(t500rs->hdev, "FFB: Failed to set gain: %d\n", ret);
	return ret;
}

/* Send data via HID output report (blocking) */
static int t500rs_send_hid(struct t500rs_device_entry *t500rs, u8 *data,
			   size_t len)
{
	int ret;

	/* Input validation */
	if (len == 0 || len > T500RS_BUFFER_LENGTH) {
		hid_err(t500rs->hdev,
			"t500rs_send_hid: Invalid length %zu (max %d)\n", len,
			T500RS_BUFFER_LENGTH);
		return -EINVAL;
	}

	ret = hid_hw_output_report(t500rs->hdev, data, len);
	if (ret < 0) {
		hid_err(t500rs->hdev, "HID output report failed: %d\n", ret);
		return ret;
	}

	if (ret != len) {
		hid_err(t500rs->hdev,
			"HID output report truncated: sent %d, expected %zu\n",
			ret, len);
		return -EIO;
	}

	return 0;
}

/*
 * Send STOP command (0x41) for the given hardware slot into the supplied
 * buffer. Callers in the core FFB worker use t500rs_send_stop() (shared
 * send_buffer); the expiry worker uses this with its own DMA-safe buffer so
 * it never races the core worker.
 */
static int t500rs_send_stop_now(struct t500rs_device_entry *t500rs, u8 *buf,
				u8 effect_id)
{
	struct t500rs_r41_cmd *r41;

	if (!t500rs)
		return -ENODEV;
	if (!buf)
		return -ENOMEM;

	r41 = (struct t500rs_r41_cmd *)buf;
	r41->id = 0x41;
	r41->effect_id = effect_id;
	r41->command = 0x00; /* STOP */
	r41->arg = 0x01;
	return t500rs_send_hid(t500rs, (u8 *)r41, sizeof(*r41));
}

/*
 * Send STOP command for the given hardware slot. Per protocol the 0x41
 * effect_id addresses one slot at a time; this halts only that slot's
 * playback, leaving all other slots intact.
 */
static int t500rs_send_stop(struct t500rs_device_entry *t500rs, u8 effect_id)
{
	if (!t500rs)
		return -ENODEV;
	if (!t500rs->send_buffer)
		return -ENOMEM;
	return t500rs_send_stop_now(t500rs, t500rs->send_buffer, effect_id);
}

/*
 * Send START command (0x41) for the given hardware slot. No duration/count
 * field: the T500RS runs the effect until an explicit 0x41 STOP, which the
 * driver enforces in software via the expiry tracker.
 */
static int t500rs_send_start_now(struct t500rs_device_entry *t500rs, u8 *buf,
				 u8 effect_id)
{
	struct t500rs_r41_cmd *r41;

	if (!t500rs)
		return -ENODEV;
	if (!buf)
		return -ENOMEM;

	r41 = (struct t500rs_r41_cmd *)buf;
	r41->id = 0x41;
	r41->effect_id = effect_id;
	r41->command = 0x41; /* START */
	/* 0xff is the dominant Windows START argument; 0x01 is the only
	 * counter-example ever observed. STOP always uses 0x01.
	 */
	r41->arg = 0xff;
	return t500rs_send_hid(t500rs, (u8 *)r41, sizeof(*r41));
}

static int t500rs_send_start(struct t500rs_device_entry *t500rs, u8 effect_id)
{
	if (!t500rs)
		return -ENODEV;
	if (!t500rs->send_buffer)
		return -ENOMEM;
	return t500rs_send_start_now(t500rs, t500rs->send_buffer, effect_id);
}

/* Upload constant force effect */
static int t500rs_upload_constant(struct t500rs_device_entry *t500rs,
				  const struct tmff2_effect_state *state)
{
	const struct ff_effect *effect = &state->effect;
	int ret;
	int level = effect->u.constant.level;

	/* Note: Gain is applied in play_effect, not here */
	T500RS_DBG(t500rs, "Upload constant: id=%d, level=%d, dir=%u\n",
		   effect->id, level, effect->direction);

	/* Send packet sequence for constant effect. Constant force uses
	 * fixed subtypes (T500RS_CONSTANT_PARAM_SUB/ENV_SUB) and hardware
	 * slot 0. */
	ret = t500rs_send_packet_sequence(
		t500rs, state, t500rs_seq_constant,
		sizeof(t500rs_seq_constant) / sizeof(t500rs_seq_constant[0]));
	if (ret) {
		hid_err(t500rs->hdev,
			"Failed to send constant effect sequence: %d\n", ret);
		return ret;
	}

	T500RS_DBG(t500rs, "Constant effect %d uploaded\n", effect->id);
	return 0;
}

/* Upload effect */
static int t500rs_upload_effect(void *data,
				const struct tmff2_effect_state *state)
{
	struct t500rs_device_entry *t500rs = data;
	const struct ff_effect *effect;
	int ret;

	effect = &state->effect;

	/* Validate effect ID range */
	if (effect->id >= T500RS_MAX_EFFECTS) {
		hid_err(t500rs->hdev, "Effect ID %d exceeds maximum %d\n",
			effect->id, T500RS_MAX_EFFECTS);
		return -EINVAL;
	}

	/* Validate effect parameters based on type */
	/* Per-type range checks. We only reject values that are genuinely
	 * malformed against the Linux input UAPI contract; values within the
	 * field's representable range are handled by the scaling helpers'
	 * clamping, so they are not validated here.
	 *
	 * Specifically NOT checked (all either impossible for the field's
	 * type or already covered by helper clamping):
	 *  - constant.level  (__s16; helper clamps to [-32767,32767])
	 *  - replay.delay (__u16; cannot exceed 65535)
	 */
	if (effect->type != FF_CONSTANT) {
		hid_err(t500rs->hdev, "Unsupported effect type: %d\n",
			effect->type);
		return -EINVAL;
	}

	/* Direction is provided by the Linux FF subsystem as 0..65535 (u16);
	 * projection onto the wheel axis happens in
	 * t500rs_scale_const_with_direction(), so accept the full u16
	 * range here. */

	ret = t500rs_upload_constant(t500rs, state);

	if (ret < 0) {
		hid_err(t500rs->hdev,
			"Failed to upload effect type %d, id %d: %d\n",
			effect->type, effect->id, ret);
	}
	return ret;
}

/*
 * (Re)arm the expiry worker for the soonest still-active finite effect.
 * Caller must hold t500rs->expiry_lock. Finite effects (total_ms != 0) get a
 * delayed_work at their deadline; infinite effects (total_ms == 0) never
 * auto-stop (Linux FFB semantics) and are skipped. If nothing finite is
 * pending the worker is cancelled.
 */
static void t500rs_expiry_arm_locked(struct t500rs_device_entry *t500rs)
{
	unsigned long soonest = 0;
	bool found = false;

	for (int i = 0; i < T500RS_MAX_EFFECTS; i++) {
		struct t500rs_active_effect *a = &t500rs->active[i];
		unsigned long deadline;

		if (!a->active || a->total_ms == 0)
			continue;
		deadline = a->start_ms + a->total_ms;
		if (!found || time_before(deadline, soonest)) {
			soonest = deadline;
			found = true;
		}
	}

	if (found) {
		long delay = (long)soonest - (long)jiffies;

		if (delay < 0)
			delay = 0;
		mod_delayed_work(system_wq, &t500rs->expiry_work,
				 (unsigned long)delay);
	} else {
		cancel_delayed_work(&t500rs->expiry_work);
	}
}

/*
 * Expiry worker. Scans active[]; any finite effect whose time has elapsed is
 * marked inactive and a per-slot 0x41 STOP is sent for it. Each STOP halts
 * only its own slot, so concurrent effects are not disturbed.
 */
static void t500rs_expiry_work(struct work_struct *work)
{
	struct t500rs_device_entry *t500rs =
		container_of(to_delayed_work(work), struct t500rs_device_entry,
			     expiry_work);
	unsigned long flags;
	unsigned long now = jiffies_to_msecs(jiffies);

	spin_lock_irqsave(&t500rs->expiry_lock, flags);
	for (int i = 0; i < T500RS_MAX_EFFECTS; i++) {
		struct t500rs_active_effect *a = &t500rs->active[i];

		if (!a->active || a->total_ms == 0)
			continue;
		if (now < a->start_ms + a->total_ms)
			continue;

		a->active = false;
		/* hw_id was captured at play time (slot 0 for constant
		 * force) - it is NOT derived from the index here, so finite
		 * constants STOP the slot they actually run on. */
		t500rs_send_stop_now(t500rs, t500rs->expiry_buffer, a->hw_id);
	}
	t500rs_expiry_arm_locked(t500rs);
	spin_unlock_irqrestore(&t500rs->expiry_lock, flags);
}

/*
 * Play effect - send START command (0x41) for the effect and arm the
 * software expiry tracker so finite effects actually terminate.
 */
static int t500rs_play_effect(void *data,
			      const struct tmff2_effect_state *state)
{
	struct t500rs_device_entry *t500rs = data;
	const struct ff_effect *effect = &state->effect;
	unsigned long flags;
	int ret;

	/* Validate effect ID range */
	if (effect->id >= T500RS_MAX_EFFECTS) {
		hid_err(t500rs->hdev, "Effect ID %d exceeds maximum %d\n",
			effect->id, T500RS_MAX_EFFECTS);
		return -EINVAL;
	}

	/* Validate effect type is supported */
	switch (effect->type) {
	case FF_CONSTANT:
		break;
	default:
		hid_err(t500rs->hdev, "Unsupported effect type for play: %d\n",
			effect->type);
		return -EINVAL;
	}

	ret = t500rs_send_start(t500rs, t500rs_effect_to_hw_id(effect));
	if (ret == 0) {
		/* Match the core's auto-expiry math (hid-tmff2.c): total playing
		 * time is (delay + length) * count. Infinite (length == 0) never
		 * auto-stops. */
		unsigned long total = (unsigned long)(effect->replay.delay +
						     effect->replay.length) *
					state->count;

		spin_lock_irqsave(&t500rs->expiry_lock, flags);
		if (total == 0) {
			/* Infinite effect: never auto-stop. */
			t500rs->active[effect->id].active = false;
		} else {
			t500rs->active[effect->id].active = true;
			t500rs->active[effect->id].hw_id =
				t500rs_effect_to_hw_id(effect);
			t500rs->active[effect->id].start_ms =
				jiffies_to_msecs(jiffies);
			t500rs->active[effect->id].total_ms = total;
		}
		t500rs_expiry_arm_locked(t500rs);
		spin_unlock_irqrestore(&t500rs->expiry_lock, flags);

		T500RS_DBG(t500rs, "Started effect %d (total=%lu ms)\n",
			   effect->id, total);
	}
	return ret;
}

/*
 * Stop effect - deactivate the software expiry slot and send a per-slot
 * 0x41 STOP. Each STOP addresses only its own slot, so concurrent effects
 * remain unaffected.
 */
static int t500rs_stop_effect(void *data,
			      const struct tmff2_effect_state *state)
{
	struct t500rs_device_entry *t500rs = data;
	const struct ff_effect *effect = &state->effect;
	unsigned long flags;

	/* Validate effect ID range */
	if (effect->id >= T500RS_MAX_EFFECTS) {
		hid_err(t500rs->hdev, "Effect ID %d exceeds maximum %d\n",
			effect->id, T500RS_MAX_EFFECTS);
		return -EINVAL;
	}

	if (!t500rs->send_buffer) {
		hid_err(t500rs->hdev, "t500rs_stop_effect: NULL send buffer\n");
		return -ENOMEM;
	}

	spin_lock_irqsave(&t500rs->expiry_lock, flags);
	t500rs->active[effect->id].active = false;
	t500rs_expiry_arm_locked(t500rs);
	spin_unlock_irqrestore(&t500rs->expiry_lock, flags);

	return t500rs_send_stop(t500rs, t500rs_effect_to_hw_id(effect));
}

/*
 * Update effect - send parameter updates without re-uploading
 *
 * Note: Only parameter-specific packets (0x03) are updated.
 * Duration and delay changes (from 0x01 packet) require full re-upload.
 * This limitation is acceptable as duration/delay modifications are rare
 * in gaming applications and the hardware may not support runtime updates
 * of these fields.
 */
static int t500rs_update_effect(void *data,
				const struct tmff2_effect_state *state)
{
	struct t500rs_device_entry *t500rs = data;
	const struct ff_effect *effect = &state->effect;
	const struct ff_effect *old = &state->old;
	u8 *buf;

	if (!t500rs)
		return -ENODEV;

	buf = t500rs->send_buffer;
	if (!buf)
		return -ENOMEM;

	switch (effect->type) {
	case FF_CONSTANT: {
		if (effect->u.constant.level == old->u.constant.level &&
		    effect->direction == old->direction)
			return 0;

		/* Constant force uses fixed subtypes (see docs/T500RS_FFBEFFECTS.md). */
		return t500rs_send_constant_packet(
			t500rs, buf, (u8)T500RS_CONSTANT_PARAM_SUB,
			effect->u.constant.level, effect->direction);
	}

	default:
		return -EOPNOTSUPP;
	}
}

/* Set autocenter */
static int t500rs_set_autocenter(void *data, u16 autocenter)
{
	struct t500rs_device_entry *t500rs = data;
	u8 *buf;
	int ret;
	u8 autocenter_percent;

	if (!t500rs)
		return -ENODEV;

	autocenter_percent = (u8)((autocenter * 100) / 65535);

	/* 
	* Wine compatibility: Some games (e.g., LFS under Wine) set autocenter to
	* 100%% at startup. That leaves a permanent strong
	* centering force which masks/overpowers other forces. To avoid this, message
	* the requests for the user to revert the gain value to expected value.
	*/
	if (autocenter_percent >= 100) {
		hid_warn(
			t500rs->hdev,
			"Game might have set autocenter to 100%%, you might want to set "
			"it back to expected value using oversteer (or keep oversteer "
			"open) or system gain.");
	}

	buf = t500rs->send_buffer;
	if (!buf)
		return -ENOMEM;

	/* Enable autocenter: Report 0x40 0x04 0x01 */
	struct t500rs_pkt_r40_config *config =
		(struct t500rs_pkt_r40_config *)buf;
	config->id = 0x40;
	config->subcmd = 0x04;
	config->data1 = 0x01; /* Enable */
	config->data2 = 0x00;
	ret = t500rs_send_hid(t500rs, buf, 4);
	if (ret)
		return ret;

	/* Set autocenter strength: Report 0x40 0x03 [value] */
	struct t500rs_pkt_r40_config *strength =
		(struct t500rs_pkt_r40_config *)buf;
	strength->id = 0x40;
	strength->subcmd = 0x03;
	strength->data1 = autocenter_percent; /* 0-100 percentage */
	strength->data2 = 0x00;
	ret = t500rs_send_hid(t500rs, buf, 4);
	if (ret)
		return ret;

	/* Apply settings: Report 0x42 0x05 */
	buf[0] = 0x42;
	buf[1] = 0x05;
	ret = t500rs_send_hid(t500rs, buf, 2);
	if (ret)
		return ret;

	return 0;
}

/* Set wheel rotation range */
static int t500rs_set_range(void *data, u16 range)
{
	struct t500rs_device_entry *t500rs = data;
	/* Use a dedicated heap buffer, NOT t500rs->send_buffer. The parent
	 * calls set_range directly from sysfs process context (range_store),
	 * which races the FFB worker that reuses send_buffer for
	 * upload/update/play/stop. Mirrors the documented T300RS fix.
	 *
	 * NB: the buffer must be DMA-safe because hid_hw_output_report()
	 * maps it for USB DMA; a stack buffer is rejected by the USB HCD
	 * ("transfer buffer is on stack"). kmalloc memory is DMA-safe.
	 */
	u8 *buf = kzalloc(4, GFP_KERNEL);
	int ret;
	u16 range_value;

	if (!buf) {
		hid_err(t500rs->hdev, "could not allocate range buffer\n");
		return -ENOMEM;
	}

	/* Validate range - minimum 40 degrees, maximum 1080 degrees */
	if (range < T500RS_RANGE_MIN)
		range = T500RS_RANGE_MIN;

	if (range > T500RS_RANGE_MAX)
		range = T500RS_RANGE_MAX;

	T500RS_DBG(t500rs, "Setting wheel range to %u degrees\n", range);

	/* Device expects LITTLE-ENDIAN and value = range * 60. */
	range_value = range * 60;

	/* Send Report 0x40 0x11 [value_lo] [value_hi] to set range */
	{
		struct t500rs_pkt_r40_config *config =
			(struct t500rs_pkt_r40_config *)buf;
		config->id = 0x40;
		config->subcmd = 0x11;
		config->data1 = range_value &
				0xFF; /* Low byte first (little-endian) */
		config->data2 = (range_value >> 8) &
				0xFF; /* High byte second */
	}

	ret = t500rs_send_hid(t500rs, buf, 4);
	if (ret) {
		hid_err(t500rs->hdev, "Failed to send range command: %d\n",
			ret);
		goto out;
	}

	/* Apply settings with Report 0x42 0x05 */
	buf[0] = 0x42;
	buf[1] = 0x05;
	ret = t500rs_send_hid(t500rs, buf, 2);
	if (ret) {
		hid_err(t500rs->hdev, "Failed to apply range settings: %d\n",
			ret);
		goto out;
	}

	T500RS_DBG(t500rs, "Range set to %u degrees (final value=0x%04x)\n",
		   range, range_value);

out:
	kfree(buf);
	return ret;
}

/* Initialize T500RS device.
 *
 * open_mode is intentionally unused here: this variant installs no
 * open/close callback (see t500rs_populate_api), so there is nothing to
 * gate on open/close. FFB is armed once during this init (0x42 0x04/
 * 0x05/0x00 handshake + 0x40 FFB-enable + 0x43 gain) and stays armed;
 * the parent falls back to the default HID input open/close. The parent's
 * open_mode module param therefore has no effect on this wheel, which is
 * the correct behavior given the single-armed init strategy.
 */
static int t500rs_wheel_init(struct tmff2_device_entry *tmff2, int open_mode)
{
	struct t500rs_device_entry *t500rs = NULL;
	u8 *init_buf; /* Will use send_buffer for transfers */
	int ret;
	int i;

	/* Sanity check protocol packet sizes against documentation */
	BUILD_BUG_ON(sizeof(struct t500rs_pkt_r01_main) != 15);

	/* Validate input parameters */
	if (!tmff2) {
		pr_err("t500rs_wheel_init: NULL tmff2 structure\n");
		return -EINVAL;
	}

	if (!tmff2->hdev || !tmff2->input_dev) {
		pr_err("t500rs_wheel_init: Invalid tmff2 structure"
		       " (missing hdev or input_dev)\n");
		return -EINVAL;
	}

	hid_dbg(tmff2->hdev, "T500RS: Initializing HID mode\n");

	/* Allocate device data */
	t500rs = kzalloc(sizeof(*t500rs), GFP_KERNEL);
	if (!t500rs) {
		hid_err(tmff2->hdev,
			"Failed to allocate t500rs device structure\n");
		ret = -ENOMEM;
		goto err_alloc;
	}

	/* Initialize device structure */
	t500rs->hdev = tmff2->hdev;
	t500rs->input_dev = tmff2->input_dev;

	/* Allocate send buffer */
	t500rs->buffer_length = T500RS_BUFFER_LENGTH;

	t500rs->send_buffer = kzalloc(t500rs->buffer_length, GFP_KERNEL);
	if (!t500rs->send_buffer) {
		hid_err(tmff2->hdev,
			"Failed to allocate send buffer (%zu bytes)\n",
			t500rs->buffer_length);
		ret = -ENOMEM;
		goto err_buffer_alloc;
	}

	/* Allocate dedicated DMA-safe buffer for the expiry worker so it never
	 * races the shared send_buffer used by the core FFB worker. */
	t500rs->expiry_buffer = kzalloc(t500rs->buffer_length, GFP_KERNEL);
	if (!t500rs->expiry_buffer) {
		hid_err(tmff2->hdev,
			"Failed to allocate expiry buffer (%zu bytes)\n",
			t500rs->buffer_length);
		ret = -ENOMEM;
		goto err_expiry_alloc;
	}

	spin_lock_init(&t500rs->expiry_lock);
	INIT_DELAYED_WORK(&t500rs->expiry_work, t500rs_expiry_work);
	memset(t500rs->active, 0, sizeof(t500rs->active));

	/* Store device data in tmff2 BEFORE any operations that might fail */
	tmff2->data = t500rs;

	/* Use send_buffer for all HID transfers */
	init_buf = t500rs->send_buffer;

	T500RS_DBG(t500rs, "Sending initialization sequence...\n");

	/* Report 0x42 - Init/status commands (2 bytes each)
	* Windows sends these at startup: 0x42 0x04, 0x42 0x05, 0x42 0x00
	* These appear to initialize the FFB subsystem state.
	*
	* The opening sync (0x42 0x04) is mandatory: if the device cannot
	* even acknowledge the first handshake, FFB will be dead and binding
	* would advertise a non-functional FF device. Fail probe loudly so the
	* failure is visible (wheel_destroy, called by the parent, frees the
	* buffers allocated above).
	*/
	memset(init_buf, 0, 2);
	init_buf[0] = 0x42;
	init_buf[1] = 0x04;
	ret = t500rs_send_hid(t500rs, init_buf, 2);
	if (ret) {
		hid_err(t500rs->hdev,
			"Mandatory init sync 0x42 0x04 failed: %d\n", ret);
		return ret;
	}

	memset(init_buf, 0, 2);
	init_buf[0] = 0x42;
	init_buf[1] = 0x05;
	ret = t500rs_send_hid(t500rs, init_buf, 2);
	if (ret)
		hid_warn(t500rs->hdev, "Init command 0x42 0x05 failed: %d\n",
			 ret);

	memset(init_buf, 0, 2);
	init_buf[0] = 0x42;
	init_buf[1] = 0x00;
	ret = t500rs_send_hid(t500rs, init_buf, 2);
	if (ret)
		hid_warn(t500rs->hdev, "Init command 0x42 0x00 failed: %d\n",
			 ret);

	/* Defensive state reset: a module reload (rmmod/insmod) does not
	 * reset the firmware - only a USB replug does. Effects left playing
	 * by a previous session survive into this probe and desync the new
	 * session (observed: after a reload, LFS under Wine had no forces
	 * until the wheel was unplugged/replugged). Bring the wheel to a
	 * known state: STOP every hardware slot. Advisory - on failure the
	 * residue stays, which is exactly the pre-reset behavior.
	 */
	for (i = 0; i < T500RS_MAX_HW_EFFECTS; i++) {
		ret = t500rs_send_stop(t500rs, i);
		if (ret)
			hid_warn(t500rs->hdev,
				 "Init slot STOP %d failed: %d\n", i, ret);
	}

	/* Report 0x40 - Disable built-in autocenter (4 bytes). Advisory:
	 * if this fails the base keeps its default autocenter, which the
	 * set_autocenter callback can still override later.
	 *
	 * Note: 0x40 subcommand 0x11 is the RANGE command
	 * (docs/T500RS_FFBEFFECTS.md section 5.7) and is never sent at init.
	 */
	{
		struct t500rs_pkt_r40_config *config =
			(struct t500rs_pkt_r40_config *)init_buf;
		config->id = 0x40;
		config->subcmd = 0x04;
		// Keep explicit zeros even though memset() clears them.
		config->data1 = 0x00;
		config->data2 = 0x00;
	}
	ret = t500rs_send_hid(t500rs, init_buf, 4);
	if (ret)
		hid_warn(t500rs->hdev,
			 "Autocenter-disable (0x40 0x04) failed: %d\n", ret);

	/* Report 0x43 - Set global gain (2 bytes). Advisory: seed the device
	 * gain from the `default_gain` module param (percent, 0-100). The
	 * default of 100 yields 0xff - byte-identical to the init sequence
	 * this driver has always sent, so the wire format is unchanged unless
	 * the user opts in (Windows seeds 90%). The set_gain callback
	 * re-applies the shared `gain` param later; a failure here just
	 * leaves whatever gain the base already has.
	 */
	memset(init_buf, 0, 2);
	init_buf[0] = 0x43;
	init_buf[1] = (u8)(clamp_t(int, default_gain, 0, 100) * 255 / 100);
	ret = t500rs_send_hid(t500rs, init_buf, 2);
	if (ret)
		hid_warn(t500rs->hdev,
			 "Initial gain set (0x43) failed: %d\n", ret);

	hid_info(t500rs->hdev, "T500RS initialized successfully (HID mode)\n");
	T500RS_DBG(t500rs, "Buffer: %zu bytes\n", t500rs->buffer_length);

	/* Advertise capabilities now that init succeeded */
	tmff2->params = t500rs_params;
	tmff2->max_effects = T500RS_MAX_EFFECTS;
	memcpy(tmff2->supported_effects, t500rs_effects,
	       sizeof(t500rs_effects));

	return 0;

err_expiry_alloc:
	kfree(t500rs->expiry_buffer);
	kfree(t500rs->send_buffer);
err_buffer_alloc:
	/* t500rs structure is allocated but not yet stored in tmff2->data */
	kfree(t500rs);
err_alloc:
	return ret;
}

/* Cleanup T500RS device */
static int t500rs_wheel_destroy(void *data)
{
	struct t500rs_device_entry *t500rs = data;

	if (!t500rs) {
		pr_warn("t500rs_wheel_destroy: NULL data pointer\n");
		return 0;
	}

	T500RS_DBG(t500rs, "T500RS: Cleaning up\n");

	/* Cancel any pending work before freeing its buffer. */
	cancel_delayed_work_sync(&t500rs->expiry_work);

	/* Free resources in reverse order of allocation */
	if (t500rs->expiry_buffer) {
		kfree(t500rs->expiry_buffer);
		t500rs->expiry_buffer = NULL;
	}

	if (t500rs->send_buffer) {
		kfree(t500rs->send_buffer);
		t500rs->send_buffer = NULL;
	}

	kfree(t500rs);

	return 0;
}

/* Populate API callbacks.
 *
 * No wheel_fixup is registered: the stock T500RS report descriptor already
 * correctly declares the wheel X axis (0..65535), pedals (Y/Rz/Slider,
 * 0..1023), 13 buttons, and an 8-way hat - it is well-formed and needs no
 * patching. The FFB output path also does not depend on the descriptor (it
 * sends raw packets via hid_hw_output_report()).
 *
 * NOTE: Oversteer expects pedals on the Simulation-page usages
 * (ABS_GAS/ABS_BRAKE/ABS_THROTTLE), but the hardware reports them as
 * Y/Rz/Slider. Remapping those in the descriptor breaks games that bind to
 * the stock Y/Rz layout, so the fix must live in userspace (SDL / Wine /
 * game-specific mapping), not here.
 *
 * No open/close callback is installed: FFB is armed once in wheel_init and
 * the parent falls back to the default HID open/close (see comment on
 * t500rs_wheel_init re: open_mode).
 */
int t500rs_populate_api(struct tmff2_device_entry *tmff2)
{
	tmff2->play_effect = t500rs_play_effect;
	tmff2->upload_effect = t500rs_upload_effect;
	tmff2->update_effect = t500rs_update_effect;
	tmff2->stop_effect = t500rs_stop_effect;

	tmff2->set_gain = t500rs_set_gain;
	tmff2->set_autocenter = t500rs_set_autocenter;
	tmff2->set_range = t500rs_set_range;

	tmff2->wheel_init = t500rs_wheel_init;
	tmff2->wheel_destroy = t500rs_wheel_destroy;

	return 0;
}
