
// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Force feedback support for Thrustmaster T500RS
 *
 * HID implementation using HID output reports for all communication.
 *
 * Protocol documentation: docs/T500RS_USB_Protocol_Analysis.md
 *
 * Key protocol details (verified against Windows USB captures):
 * - 0x01 packet: Main upload (15 bytes) - effect_id, direction, duration,
 * delay, code1/2
 * - 0x02 packet: Envelope (9 bytes) - attack/fade levels and times
 * - 0x03 packet: Constant force level (4 bytes)
 * - 0x04 packet: Periodic/Ramp parameters (8 bytes) - code 0x2a, period in ms
 * - 0x05 packet: Conditional parameters (11 bytes) - two packets per effect
 * (X/Y)
 * - 0x41 packet: START/STOP command (4 bytes) - per-effect hw_id
 *
 * Hardware supports 16 concurrent effects with internal mixing.
 * Protocol analysis based on 70+ test captures from Windows driver.
 */

#include "../hid-tmff2.h"
#include "hid-tmt500rs.h"
#include <linux/hid.h>
#include <linux/input.h>

/* Packet sequence templates for each effect type */
static const enum t500rs_seq_packet t500rs_seq_constant[] = {
    T500RS_SEQ_ENVELOPE,
    T500RS_SEQ_CONSTANT,
    T500RS_SEQ_MAIN,
};

static const enum t500rs_seq_packet t500rs_seq_periodic[] = {
    T500RS_SEQ_STOP,     T500RS_SEQ_SYNC_42_05,    T500RS_SEQ_SYNC_42_04,
    T500RS_SEQ_ENVELOPE, T500RS_SEQ_PERIODIC_RAMP, T500RS_SEQ_MAIN,
};

static const enum t500rs_seq_packet t500rs_seq_ramp[] = {
    T500RS_SEQ_STOP,
    T500RS_SEQ_ENVELOPE,
    T500RS_SEQ_PERIODIC_RAMP,
    T500RS_SEQ_MAIN,
};

static const enum t500rs_seq_packet t500rs_seq_condition[] = {
    T500RS_SEQ_CONDITION_X,
    T500RS_SEQ_CONDITION_Y,
    T500RS_SEQ_MAIN,
};

/* Scale constant level (-32767..32767) to signed 8-bit (-127..127) */
static inline s8 t500rs_scale_const_level_s8(int level) {
  /* Input validation and clamping */
  if (level > 32767)
    level = 32767;
  if (level < -32767)
    level = -32767;

  /* Use 32-bit arithmetic to prevent overflow */
  return (s8)((level * 127LL) / 32767);
}

/* Apply effect direction to a constant level and convert to s8.
 * Mirrors t300rs_calculate_constant_level()'s projection semantics but
 * keeps the full T500RS range and uses t500rs_scale_const_level_s8() for
 * clamping and conversion.
 */
static inline s8 t500rs_scale_const_with_direction(int level, u16 direction) {
  int projected;

  projected = (level * fixp_sin16(direction * 360 / 0x10000)) / 0x7fff;

  return t500rs_scale_const_level_s8(projected);
}

/* Scale magnitude (0..32767 or signed) to 7-bit (0..127) */
static inline u8 t500rs_scale_mag_u7(int magnitude) {
  /* Input validation and clamping */
  if (magnitude < 0)
    magnitude = -magnitude;
  if (magnitude > 32767)
    magnitude = 32767;

  /* Use 32-bit arithmetic to prevent overflow */
  return (u8)((magnitude * 127LL) / 32767);
}

/*
 * Map logical effect ID to hardware effect ID.
 * hw_id = logical_id + 1
 *
 * This avoids hardware index 0 entirely, which has quirky behavior
 * (only valid for constant effects). By always using indices 1-15,
 * all effect types work uniformly with no special-casing needed.
 *
 * Trade-off: 15 effect slots instead of 16, but simpler code and
 * no risk of index 0 misuse. Most games don't need 16 simultaneous effects.
 */
static inline unsigned int t500rs_logical_to_hw_id(unsigned int logical_id) {
  /* Clamp to valid range: logical 0-14 -> hw 1-15 */
  if (logical_id >= T500RS_MAX_EFFECTS)
    logical_id = T500RS_MAX_EFFECTS - 1;
  return logical_id + 1;
}

/* Map hardware effect index to parameter/envelope subtypes as per protocol:
 * Per protocol analysis, subtypes are calculated as:
 *  param_sub = 0x000e + 0x001c * idx
 *  env_sub   = 0x001c + 0x001c * idx
 * idx is the hardware effect ID (1..15 with simplified architecture).
 */
static inline void t500rs_index_to_subtypes(unsigned int idx, u16 *param_sub,
                                            u16 *env_sub) {
  /* Validate inputs */
  if (idx >= T500RS_MAX_HW_EFFECTS) {
    idx = T500RS_MAX_HW_EFFECTS - 1;  /* Clamp to valid range */
  }

  *param_sub = 0x000e + (0x001c * idx);
  *env_sub = 0x001c + (0x001c * idx);
}

/* Debug logging helper: pass struct t500rs_device_entry * explicitly */
#define T500RS_DBG(dev, fmt, ...) hid_dbg((dev)->hdev, fmt, ##__VA_ARGS__)

/* T500RS device data */
struct t500rs_device_entry {
   struct hid_device *hdev;
   struct input_dev *input_dev;

   u8 *send_buffer;
   size_t buffer_length;
};

/*
 * Scale direction from Linux ff_effect format to T500RS protocol format.
 *
 * Linux ff_effect.direction: 0-65535 (0 = forward, 16384 = right, 32768 = back,
 * 49152 = left) T500RS protocol: 0-35999 in 0.01 degree units (0 = 0°, 9000 =
 * 90°, 18000 = 180°, etc.)
 *
 * Conversion: device_dir = (os_ffb_dir * 36000) / 65536
 * This maps 0-65535 → 0-35999 (approximately, since 65535 → 35999.45)
 */
static inline u16 t500rs_scale_direction(u16 os_ffb_dir) {
  /* Use 32-bit arithmetic to avoid overflow */
  return (u16)(((u32)os_ffb_dir * 36000) / 65536);
}

/*
 * Build a protocol-accurate 0x01 main upload packet.
 *
 * Per the T500RS USB protocol documentation:
 * - effect_id: 16-bit LE hardware effect slot (0..15 for now)
 * - duration_ms: duration in milliseconds
 * - delay_ms: delay before effect starts
 * - code1: parameter subtype (used by 0x03/0x04/0x05)
 * - code2: envelope subtype (used by 0x02), or second conditional subtype
 *
 * Per Windows captures, effect_type values are:
 * - 0x00 = Constant
 * - 0x22 = Sine
 * - 0x21 = Triangle (inferred)
 * - 0x23 = Sawtooth Up (inferred)
 * - 0x24 = Sawtooth Down (inferred)
 * - 0x40 = Spring
 * - 0x41 = Damper/Friction/Inertia
 *
 * NOTE: Direction is sent separately in a 0x03 packet for constant force,
 * not in this 0x01 packet.
 */
static int t500rs_build_r01_main(struct t500rs_pkt_r01_main *p, u8 effect_id,
                                 u8 effect_type, u16 duration_ms, u16 delay_ms,
                                 u16 code1, u16 code2) {
  /* Validate effect_id */
  if (effect_id >= T500RS_MAX_HW_EFFECTS) {
    pr_err("t500rs: Invalid effect_id %u (max %d)\n",
           effect_id, T500RS_MAX_HW_EFFECTS - 1);
    return -EINVAL;
  }

  /* Validate effect_type against known constants */
  switch (effect_type) {
  case T500RS_EFFECT_CONSTANT:
  case T500RS_EFFECT_SINE:
  case T500RS_EFFECT_TRIANGLE:
  case T500RS_EFFECT_SAW_UP:
  case T500RS_EFFECT_SAW_DOWN:
  case T500RS_EFFECT_SPRING:
  case T500RS_EFFECT_DAMPER:  /* Note: DAMPER, FRICTION, INERTIA all use 0x41 */
    break;
  default:
    pr_err("t500rs: Unknown effect_type 0x%02x\n", effect_type);
    return -EINVAL;
  }

  /* Validate packet codes are non-zero (0x0000 likely indicates bug) */
  if (code1 == 0 || code2 == 0) {
    pr_warn("t500rs: Suspicious packet codes: code1=0x%04x code2=0x%04x\n",
            code1, code2);
  }

  memset(p, 0, sizeof(*p));
  p->id = T500RS_PKT_MAIN;
  p->effect_id = effect_id;
  p->effect_type = effect_type;
  p->control = T500RS_CONTROL_DEFAULT;
  p->duration_ms = cpu_to_le16(duration_ms);
  p->delay_ms = cpu_to_le16(delay_ms);
  p->reserved1 = 0;
  p->packet_code_1 = cpu_to_le16(code1);
  p->packet_code_2 = cpu_to_le16(code2);
  p->reserved2 = 0;

  return 0;
}

/*
 * Build a protocol-accurate 0x04 periodic/ramp packet.
 *
 * Per the T500RS USB protocol documentation:
 * - code: low byte of param_subtype from 0x01 (e.g., 0x2a for periodic, not
 * 0x0e!)
 * - magnitude: 0..127 (scaled from 0..32767)
 * - offset: signed DC offset (scaled from -32768..32767 to device range)
 * - phase: 0..255 (256 steps for 360°, scaled from 0..35999)
 * - period_ms: period in MILLISECONDS (no Hz*100 conversion!)
 * - reserved: always 0
 *
 * Scaling formulas (from protocol doc):
 *   device_mag   = os_ffb_mag * 127 / 32767
 *   device_phase = (os_ffb_phase * 256 / 36000) & 0xFF
 *   device_offset = os_ffb_offset / 256  (approximate, TBD based on testing)
 *   period_ms    = direct copy (no frequency conversion)
 */
static void t500rs_build_r04_periodic(struct t500rs_pkt_r04_periodic_ramp *p,
                                      u8 code, u8 magnitude, s8 offset,
                                      u8 phase, u16 period_ms) {
  /* Byte order per Windows USB captures (example: 04 2a 00 06 00 3f 0a 00):
   *   b0=T500RS_PKT_PERIODIC, b1=code, b2=reserved1, b3=mag, b4=offset,
   * b5=phase, b6-b7=period
   */
  memset(p, 0, sizeof(*p));
  p->id = T500RS_PKT_PERIODIC;           /* b0 */
  p->code = code;                        /* b1 */
  p->reserved1 = 0;                      /* b2: always 0x00 */
  p->magnitude = magnitude;              /* b3 */
  p->offset = (u8)offset;                /* b4 */
  p->phase = phase;                      /* b5 */
  p->period_ms = cpu_to_le16(period_ms); /* b6-b7 */
}

/*
 * Scale periodic magnitude with direction projection.
 *
 * For periodic effects, the direction determines the axis of oscillation.
 * We project the magnitude onto the wheel axis using sin(direction).
 *
 * When the projected magnitude is negative, we:
 * 1. Take the absolute value (wheel only supports positive magnitudes)
 * 2. Add 180° to the phase to maintain correct force direction
 *
 * Linux FFB magnitude: 0..32767 (unsigned)
 * Linux FFB direction: 0..65535 (0=forward, 16384=right, 32768=back, 49152=left)
 * Linux FFB phase: 0..65535 (0..360° in 1/65536 units)
 * Device magnitude: 0..127
 *
 * @param os_ffb_mag: Original magnitude from Linux FFB (0..32767)
 * @param direction: Effect direction from Linux FFB (0..65535)
 * @param phase_ptr: Pointer to phase value; will be adjusted if projection is negative
 * @return: Scaled magnitude (0..127)
 */
static inline u8 t500rs_scale_periodic_with_direction(int os_ffb_mag,
                                                       u16 direction,
                                                       u16 *phase_ptr) {
  int projected;

  /* Project magnitude based on direction (same formula as T300RS) */
  projected = (os_ffb_mag * fixp_sin16(direction * 360 / 0x10000)) / 0x7fff;

  if (projected < 0) {
    /* Wheel handles positive magnitudes only */
    projected = -projected;

    /* Add 180° to phase to maintain correct force direction.
     * Phase is in 0..65535 range (Linux FFB), 180° = 0x8000 */
    if (phase_ptr)
      *phase_ptr = (*phase_ptr + 0x8000) % 0x10000;
  }

  /* Clamp to valid range */
  if (projected > 32767)
    projected = 32767;

  /* Scale to device range: 0..32767 -> 0..127 */
  return (u8)((projected * 127LL) / 32767);
}

/*
 * Scale periodic phase from Linux FFB subsystem format to device format.
 * Linux FFB: 0..35999 (0.01 degree units, 0-359.99°)
 * Device: 0..255 (256 steps for 360°)
 */
static inline u8 t500rs_scale_periodic_phase(u16 os_ffb_phase) {
  /* Clamp to valid range just in case */
  if (os_ffb_phase > 35999)
    os_ffb_phase = 35999;
  return (u8)((os_ffb_phase * 256) / 36000);
}

/*
 * Scale periodic offset from Linux FFB subsystem format to device format.
 * Linux FFB: -32768..32767
 * Device: signed, stored as s8 (-128..127)
 * Note: exact mapping TBD based on testing; using simple /256 for now.
 */
static inline s8 t500rs_scale_periodic_offset(s16 os_ffb_offset) {
  return (s8)(os_ffb_offset / 256);
}

/*
 * Build a 0x04 packet for ramp effects.
 *
 * Per the T500RS USB protocol documentation, ramp effects use the same
 * 0x04 packet structure as periodic effects. The encoding is:
 * - magnitude: scaled from start/end levels (midpoint or average)
 * - offset: difference between start and end (direction of ramp)
 * - phase: typically 0 for ramp
 * - period_ms: ramp duration in milliseconds
 *
 * Note: exact mapping of start/end to magnitude/offset is uncertain;
 * Windows captures show identical packets for different ramp parameters.
 * Current implementation uses a simple average for magnitude.
 */
static void t500rs_build_r04_ramp(struct t500rs_pkt_r04_periodic_ramp *p,
                                  u8 code, s16 start_level, s16 end_level,
                                  u16 duration_ms) {
  int avg_level;
  u8 magnitude;
  s8 offset;

  memset(p, 0, sizeof(*p));

  /* Compute average magnitude from start/end levels */
  avg_level = (abs(start_level) + abs(end_level)) / 2;
  magnitude = (u8)((avg_level * 127) / 32767);

  /* Offset encodes direction: positive = ramping up, negative = ramping down */
  /* Simple approximation: (end - start) / 512 to fit in s8 range */
  offset = (s8)((end_level - start_level) / 512);

  /* Byte order per Windows USB captures: b0=id, b1=code, b2=reserved1, b3=mag,
   * b4=offset, b5=phase, b6-b7=period */
  p->id = 0x04;                            /* b0 */
  p->code = code;                          /* b1 */
  p->reserved1 = 0;                        /* b2: always 0x00 */
  p->magnitude = magnitude;                /* b3 */
  p->offset = (u8)offset;                  /* b4 */
  p->phase = 0;                            /* b5: Ramp doesn't use phase */
  p->period_ms = cpu_to_le16(duration_ms); /* b6-b7 */
}

/*
 * Build a 0x05 conditional effect packet.
 *
 * Per Windows captures (T500RS_USB_Protocol_Analysis.md):
 * - Two 0x05 packets required per conditional effect (X-axis and Y-axis)
 * - Coefficients are currently sent as zero (needs more capture verification)
 * - Deadband and center are now experimental - some captures show non-zero values:
 *   - Spring Deadband test: deadband=500 -> 0x0007, deadband=5000 -> 0x0099
 *   - This suggests scaling: device_deadband = (os_ffb_deadband * 255) / 65535
 *
 * Parameters:
 * - code: From 0x01 packet bytes 9-10 (first packet) or 11-12 (second packet)
 * - saturation: Scaled saturation value (0-100) for both right/left channels
 * - deadband: Raw deadband from ff_condition_effect (0-65535)
 * - center: Raw center from ff_condition_effect (-32767 to +32767)
 */
static void t500rs_build_r05_condition(struct t500rs_pkt_r05_condition *p,
                                       u8 code, u8 saturation,
                                       u16 deadband, s16 center) {
  memset(p, 0, sizeof(*p));
  p->id = T500RS_PKT_CONDITIONAL;
  p->code = code;

  /* Coefficients: keep zero for now (needs capture verification) */
  p->right_coeff = 0;
  p->left_coeff = 0;

  /*
   * Experimental deadband/center support
   * Center: scale from -32767..+32767 to 0..255 (128 = center)
   */
  p->deadband = cpu_to_le16((deadband * 255) / 65535);
  p->center = (u8)(((center + 32767) * 255) / 65535);

  p->right_sat = saturation;
  p->left_sat = saturation;
}

/*
 * Scale constant force level from Linux FFB subsystem format to device format.
 *
 * Per the T500RS USB protocol documentation:
 * - Linux FFB level: 0-65535 (unsigned)
 * - Device level: -127 to +127 (signed 8-bit)
 * - Formula: device_level = (os_ffb_level * 255 / 65535) - 127
 *
 * This maps:
 *   Linux FFB 0     → Device -127 (max negative)
 *   Linux FFB 32767 → Device 0 (neutral)
 *   Linux FFB 65535 → Device +127 (max positive)
 */
static inline s8 t500rs_scale_constant_level(u16 os_ffb_level) {
  s32 tmp = ((s32)os_ffb_level * 255) / 65535;
  return (s8)(tmp - 127);
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
                                      s8 level) {
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
static inline u8 t500rs_scale_envelope_level(u16 os_ffb_level) {
  /* Input validation and clamping */
  if (os_ffb_level > 32767)
    os_ffb_level = 32767;

  /* Use 32-bit arithmetic to prevent overflow */
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
                                      u8 subtype,
                                      const struct ff_envelope *env,
                                      bool allow_nonzero) {
  memset(p, 0, sizeof(*p));
  p->id = 0x02;
  p->subtype = subtype;

  /*
   * CRITICAL FIRMWARE BUG WORKAROUND:
   * Per T500RS_USB_Protocol_Analysis.md, the device firmware rejects
   * non-zero envelope values for periodic and constant effects with
   * EPROTO (-71). Only ramp effects can safely use envelopes.
   *
   * Windows driver always sends zeros for periodic/constant:
   * 02 38 00 00 00 00 00 00 00
   */
  if (env && allow_nonzero) {
    p->attack_len = cpu_to_le16(env->attack_length);
    p->attack_level = t500rs_scale_envelope_level(env->attack_level);
    p->fade_len = cpu_to_le16(env->fade_length);
    p->fade_level = t500rs_scale_envelope_level(env->fade_level);
  } else {
    /*
     * User requested envelope but device doesn't support it.
     * Log once to inform user, then send zeros.
     */
    pr_warn_once("t500rs: Envelope requested but not supported for this effect type\n");
  }
}

/* Supported parameters */
static unsigned long t500rs_params = PARAM_SPRING_LEVEL | PARAM_DAMPER_LEVEL |
                                    PARAM_FRICTION_LEVEL | PARAM_GAIN |
                                    PARAM_RANGE;

/*
 * Supported effects.
 *
 * NOTE: FF_SQUARE is intentionally OMITTED. The tool used to generate 
 * the Square effect was not supporting it at the time of the captures.
 * Another pass of implementation will be done after that tool will support it
 * and new captures are done for this effect.
 */
const signed short t500rs_effects[] = {
    FF_CONSTANT, FF_SPRING, FF_DAMPER,     FF_FRICTION, FF_INERTIA,
    FF_PERIODIC, FF_SINE,   FF_TRIANGLE,   FF_SAW_UP,   FF_SAW_DOWN,
    FF_RAMP,     FF_GAIN,   FF_AUTOCENTER, -1};

/* Forward declarations to avoid implicit declarations before worker uses them
 */
static int t500rs_send_hid(struct t500rs_device_entry *t500rs, const u8 *data,
                           size_t len);
static inline int t500rs_send_stop(struct t500rs_device_entry *t500rs,
                                   u8 hw_effect_id);
static int t500rs_set_autocenter(void *data, u16 autocenter);
static int t500rs_set_range(void *data, u16 range);
static int t500rs_upload_effect(void *data,
                                const struct tmff2_effect_state *state);
static int t500rs_update_effect(void *data,
                                const struct tmff2_effect_state *state);
static int t500rs_play_effect(void *data,
                              const struct tmff2_effect_state *state);
static int t500rs_stop_effect(void *data,
                              const struct tmff2_effect_state *state);

/*
 * Send a sequence of packets for effect upload.
 * Abstracts the hardcoded packet orders in upload functions.
 */
static int t500rs_send_packet_sequence(struct t500rs_device_entry *t500rs,
                                       const struct tmff2_effect_state *state,
                                       u8 hw_id,
                                       const enum t500rs_seq_packet *sequence,
                                       size_t seq_len) {
  const struct ff_effect *effect = &state->effect;
  u8 *buf = t500rs->send_buffer;
  int ret;
  u16 param_sub, env_sub;

  t500rs_index_to_subtypes(hw_id, &param_sub, &env_sub);

  for (size_t i = 0; i < seq_len; i++) {
    /* Log sequence progress for debugging */
    T500RS_DBG(t500rs, "Sequence step %zu/%zu: packet type 0x%02x\n",
               i + 1, seq_len, sequence[i]);

    switch (sequence[i]) {
    case T500RS_SEQ_STOP:
      ret = t500rs_send_stop(t500rs, hw_id);
      break;
    case T500RS_SEQ_SYNC_42_05:
      buf[0] = 0x42;
      buf[1] = 0x05;
      ret = t500rs_send_hid(t500rs, buf, 2);
      break;
    case T500RS_SEQ_SYNC_42_04:
      buf[0] = 0x42;
      buf[1] = 0x04;
      ret = t500rs_send_hid(t500rs, buf, 2);
      break;
    case T500RS_SEQ_ENVELOPE: {
      struct t500rs_pkt_r02_envelope *env =
          (struct t500rs_pkt_r02_envelope *)buf;
      const struct ff_envelope *envelope = NULL;
      bool allow_envelope = false;

      if (effect->type == FF_RAMP) {
        envelope = &effect->u.ramp.envelope;
        allow_envelope = true;  /* Ramp supports envelope */
      } else if (effect->type == FF_CONSTANT || effect->type == FF_PERIODIC) {
        envelope = &effect->u.periodic.envelope;
        allow_envelope = false;  /* Firmware bug: must send zeros */
      }

      t500rs_build_r02_envelope(env, (u8)(env_sub & 0xff), envelope, allow_envelope);
      ret = t500rs_send_hid(t500rs, buf, sizeof(struct t500rs_pkt_r02_envelope));
    }
      break;
    case T500RS_SEQ_CONSTANT: {
      s8 level = t500rs_scale_const_with_direction(effect->u.constant.level,
                                                   effect->direction);
      struct t500rs_r03_const *r3 = (struct t500rs_r03_const *)buf;
      t500rs_build_r03_constant(r3, (u8)(param_sub & 0xff), level);
      ret = t500rs_send_hid(t500rs, buf, sizeof(struct t500rs_r03_const));
    }
      break;
    case T500RS_SEQ_PERIODIC_RAMP: {
        if (effect->type == FF_RAMP) {
          struct t500rs_pkt_r04_periodic_ramp *p =
              (struct t500rs_pkt_r04_periodic_ramp *)buf;
          t500rs_build_r04_ramp(p, (u8)(param_sub & 0xff),
                                effect->u.ramp.start_level,
                                effect->u.ramp.end_level, effect->replay.length);
        } else {
          /* Apply direction projection to magnitude and adjust phase if needed */
          u16 phase_raw = effect->u.periodic.phase;
          u8 mag = t500rs_scale_periodic_with_direction(
              effect->u.periodic.magnitude, effect->direction, &phase_raw);
          u8 phase = t500rs_scale_periodic_phase(phase_raw);
          s8 offset = t500rs_scale_periodic_offset(effect->u.periodic.offset);
          u16 period_ms = effect->u.periodic.period;
          if (period_ms == 0) {
            hid_err(t500rs->hdev, "Periodic effect period cannot be zero\n");
            return -EINVAL;
          }
          struct t500rs_pkt_r04_periodic_ramp *p =
              (struct t500rs_pkt_r04_periodic_ramp *)buf;
          t500rs_build_r04_periodic(p, (u8)(param_sub & 0xff), mag, offset, phase,
                                    period_ms);
        }
        ret = t500rs_send_hid(t500rs, buf,
                              sizeof(struct t500rs_pkt_r04_periodic_ramp));
        if (ret)
          break;
      }
        break;
    case T500RS_SEQ_CONDITION_X: {
      u8 saturation = 0;
      const struct ff_condition_effect *cond = &effect->u.condition[0];
      switch (effect->type) {
      case FF_SPRING:
        saturation = T500RS_SAT_SPRING;
        break;
      case FF_DAMPER:
        saturation = T500RS_SAT_DAMPER;
        break;
      case FF_FRICTION:
        saturation = T500RS_SAT_FRICTION;
        break;
      case FF_INERTIA:
        saturation = T500RS_SAT_INERTIA;
        break;
      default:
        saturation = T500RS_SAT_DAMPER;
        break;
      }
      struct t500rs_pkt_r05_condition *p =
          (struct t500rs_pkt_r05_condition *)buf;
      t500rs_build_r05_condition(p, (u8)(param_sub), saturation,
                                 cond->deadband, cond->center);
      ret = t500rs_send_hid(t500rs, buf, sizeof(struct t500rs_pkt_r05_condition));
    }
      break;
    case T500RS_SEQ_CONDITION_Y: {
      u8 saturation = 0;
      /* Y-axis: use condition[1] if available, else zeros */
      const struct ff_condition_effect *cond = &effect->u.condition[1];
      switch (effect->type) {
      case FF_SPRING:
        saturation = T500RS_SAT_SPRING;
        break;
      case FF_DAMPER:
        saturation = T500RS_SAT_DAMPER;
        break;
      case FF_FRICTION:
        saturation = T500RS_SAT_FRICTION;
        break;
      case FF_INERTIA:
        saturation = T500RS_SAT_INERTIA;
        break;
      default:
        saturation = T500RS_SAT_DAMPER;
        break;
      }
      struct t500rs_pkt_r05_condition *p =
          (struct t500rs_pkt_r05_condition *)buf;
      t500rs_build_r05_condition(p, (u8)(env_sub & 0xff), saturation,
                                 cond->deadband, cond->center);
      ret = t500rs_send_hid(t500rs, buf, sizeof(struct t500rs_pkt_r05_condition));
    }
      break;
    case T500RS_SEQ_MAIN: {
      u8 effect_type = 0;
      switch (effect->type) {
      case FF_CONSTANT:
        effect_type = T500RS_EFFECT_CONSTANT;
        break;
      case FF_SPRING:
        effect_type = T500RS_EFFECT_SPRING;
        break;
      case FF_DAMPER:
        effect_type = T500RS_EFFECT_DAMPER;
        break;
      case FF_FRICTION:
        effect_type = T500RS_EFFECT_FRICTION;
        break;
      case FF_INERTIA:
        effect_type = T500RS_EFFECT_INERTIA;
        break;
      case FF_PERIODIC:
        switch (effect->u.periodic.waveform) {
        case FF_SINE:
          effect_type = T500RS_EFFECT_SINE;
          break;
        case FF_TRIANGLE:
          effect_type = T500RS_EFFECT_TRIANGLE;
          break;
        case FF_SAW_UP:
          effect_type = T500RS_EFFECT_SAW_UP;
          break;
        case FF_SAW_DOWN:
          effect_type = T500RS_EFFECT_SAW_DOWN;
          break;
        default:
          return -EINVAL;
        }
        break;
      case FF_RAMP:
        effect_type = T500RS_EFFECT_SAW_DOWN;
        break;
      default:
        return -EINVAL;
      }
      u16 duration_ms = effect->replay.length ? effect->replay.length : 0xffff;
      u16 delay_ms = effect->replay.delay;
      struct t500rs_pkt_r01_main *m = (struct t500rs_pkt_r01_main *)buf;
      ret = t500rs_build_r01_main(m, hw_id, effect_type, duration_ms, delay_ms,
                                  param_sub, env_sub);
      if (ret)
        break;
      ret = t500rs_send_hid(t500rs, buf, sizeof(struct t500rs_pkt_r01_main));
    }
      break;
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

  T500RS_DBG(t500rs, "Sequence completed successfully (%zu packets)\n", seq_len);
  return 0;
}

static int t500rs_set_gain(void *data, u16 gain) {
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

  hid_info(t500rs->hdev, "FFB: set_gain %u -> device %u\n", gain,
           device_gain_byte);

  /* Safe buffer access with bounds checking */
  buf[0] = T500RS_PKT_GAIN;
  buf[1] = device_gain_byte;

  ret = t500rs_send_hid(t500rs, buf, 2);
  if (ret == 0)
    hid_info(t500rs->hdev, "FFB: Gain set successfully\n");
  else
    hid_err(t500rs->hdev, "FFB: Failed to set gain: %d\n", ret);
  return ret;
}

/* Send data via HID output report (blocking) */
static int t500rs_send_hid(struct t500rs_device_entry *t500rs, const u8 *data,
                           size_t len) {
  int ret;

  /* Input validation */
  if (len == 0 || len > T500RS_BUFFER_LENGTH) {
    hid_err(t500rs->hdev, "t500rs_send_hid: Invalid length %zu (max %d)\n", len,
            T500RS_BUFFER_LENGTH);
    return -EINVAL;
  }

  ret = hid_hw_output_report(t500rs->hdev, (u8 *)data, len);
  if (ret < 0) {
    hid_err(t500rs->hdev, "HID output report failed: %d\n", ret);
    return ret;
  }
  if (ret != len) {
    hid_err(t500rs->hdev,
            "HID output report truncated: sent %d, expected %zu\n", ret, len);
    return -EIO;
  }
  return 0;
}

/*
 * Send STOP command for a specific hardware effect ID.
 * Used both for pre-upload clearing and explicit stop.
 * Per protocol: 0x41 effect_id command arg
 *   command = 0x00 for STOP, 0x41 for START
 */
static inline int t500rs_send_stop(struct t500rs_device_entry *t500rs,
                                   u8 hw_effect_id) {
  u8 *buf;
  struct t500rs_r41_cmd *r41;
  if (!t500rs)
    return -ENODEV;
  buf = t500rs->send_buffer;
  if (!buf)
    return -ENOMEM;
  r41 = (struct t500rs_r41_cmd *)buf;
  r41->id = 0x41;
  r41->effect_id = hw_effect_id;
  r41->command = 0x00; /* STOP */
  r41->arg = 0x01;
  return t500rs_send_hid(t500rs, buf, sizeof(*r41));
}

/*
 * Send START command for a specific hardware effect ID.
 */
static inline int t500rs_send_start(struct t500rs_device_entry *t500rs,
                                    u8 hw_effect_id) {
  u8 *buf;
  struct t500rs_r41_cmd *r41;
  if (!t500rs)
    return -ENODEV;

  buf = t500rs->send_buffer;
  r41 = (struct t500rs_r41_cmd *)buf;
  r41->id = 0x41;
  r41->effect_id = hw_effect_id;
  r41->command = 0x41; /* START */
  r41->arg = 0x01;
  return t500rs_send_hid(t500rs, buf, sizeof(*r41));
}

/* Upload constant force effect */
static int t500rs_upload_constant(struct t500rs_device_entry *t500rs,
                                  const struct tmff2_effect_state *state) {
  const struct ff_effect *effect = &state->effect;
  int ret;
  int hw_id;
  int level = effect->u.constant.level;

  /* Note: Gain is applied in play_effect, not here */
  T500RS_DBG(t500rs, "Upload constant: id=%d, level=%d, dir=%u\n", effect->id,
             level, effect->direction);

  hw_id = t500rs_logical_to_hw_id(effect->id);

  /* Send packet sequence for constant effect */
  ret = t500rs_send_packet_sequence(t500rs, state, hw_id, t500rs_seq_constant,
                                    sizeof(t500rs_seq_constant) /
                                        sizeof(t500rs_seq_constant[0]));
  if (ret) {
    hid_err(t500rs->hdev, "Failed to send constant effect sequence: %d\n", ret);
    return ret;
  }

  T500RS_DBG(t500rs, "Constant effect %d uploaded (hw_id=%d)\n", effect->id,
             hw_id);
  return 0;
}

/*
 * Upload spring/damper/friction/inertia effect.
 *
 * Per Windows captures (T500RS_USB_Protocol_Analysis.md):
 * - 0x01 packet: direction=0x4000, code1=0x002a, code2=0x0038
 * - Two 0x05 packets: X-axis (code 0x2a) and Y-axis (code 0x38)
 * - Saturation values 0x54 (84) for spring, 0x64 (100) for damper/friction
 */
static int t500rs_upload_condition(struct t500rs_device_entry *t500rs,
                                   const struct tmff2_effect_state *state) {
  const struct ff_effect *effect = &state->effect;
  int ret;
  int hw_id;
  u8 effect_gain;
  const char *type_name;
  /*
   * Determine effect type code and gain level.
   * Per Windows captures: Spring=0x40, Damper/Friction/Inertia=0x41
   */
  u8 effect_type;
  switch (effect->type) {
  case FF_SPRING:
    type_name = "spring";
    effect_gain = spring_level;
    effect_type = T500RS_EFFECT_SPRING;
    break;
  case FF_DAMPER:
    type_name = "damper";
    effect_gain = damper_level;
    effect_type = T500RS_EFFECT_DAMPER;
    break;
  case FF_FRICTION:
    type_name = "friction";
    effect_gain = friction_level;
    effect_type = T500RS_EFFECT_FRICTION;
    break;
  case FF_INERTIA:
    type_name = "inertia";
    effect_gain = 100;
    effect_type = T500RS_EFFECT_INERTIA;
    break;
  default:
    return -EINVAL;
  }

  hw_id = t500rs_logical_to_hw_id(effect->id);

  /* Send packet sequence for conditional effect */
  ret = t500rs_send_packet_sequence(t500rs, state, hw_id, t500rs_seq_condition,
                                    sizeof(t500rs_seq_condition) /
                                        sizeof(t500rs_seq_condition[0]));
  if (ret) {
    hid_err(t500rs->hdev, "Failed to send %s effect sequence: %d\n", type_name,
            ret);
    return ret;
  }

  return 0;
}

/*
 * Upload periodic effect (sine, square, triangle, saw).
 *
 * Per Windows captures (T500RS_USB_Protocol_Analysis.md):
 * - Waveform type is NOT encoded in USB packets; determined by Linux FFB subsystem
 * - 0x01 packet: direction, duration, delay, code1=0x000e, code2=0x001c
 * - 0x02 packet: envelope with subtype 0x1c
 * - 0x04 packet: code=0x2a (NOT 0x0e!), magnitude, offset, phase, period_ms
 * - Period is in MILLISECONDS (no Hz*100 conversion)
 *
 * NOTE: The current implementation only sends the simplified packet sequence
 * observed in Windows captures. The dual-0x01/0x02 sequence in the old code
 * may have been incorrect and is removed.
 */
static int t500rs_upload_periodic(struct t500rs_device_entry *t500rs,
                                  const struct tmff2_effect_state *state) {
  const struct ff_effect *effect = &state->effect;
  int ret;
  int hw_id;
  const char *type_name;
  u8 effect_type;

  /*
   * Determine waveform name and effect_type for 0x01 packet.
   *
   * Per Windows captures, waveform type IS encoded in the 0x01 packet's
   * effect_type field (byte 2). We only support the waveforms observed in
   * captures.
   *
   * FF_SQUARE is rejected because it's not in our supported effects list.
   *
   * Per Windows captures, effect_type values for periodic:
   * - 0x21 = Triangle (inferred from protocol pattern)
   * - 0x22 = Sine (confirmed from analysis.json)
   * - 0x23 = Sawtooth Up (inferred)
   * - 0x24 = Sawtooth Down (inferred)
   */
  switch (effect->u.periodic.waveform) {
  case FF_TRIANGLE:
    type_name = "triangle";
    effect_type = T500RS_EFFECT_TRIANGLE;
    break;
  case FF_SINE:
    type_name = "sine";
    effect_type = T500RS_EFFECT_SINE;
    break;
  case FF_SAW_UP:
    type_name = "sawtooth_up";
    effect_type = T500RS_EFFECT_SAW_UP;
    break;
  case FF_SAW_DOWN:
    type_name = "sawtooth_down";
    effect_type = T500RS_EFFECT_SAW_DOWN;
    break;
  default:
    /* FF_SQUARE and other unsupported waveforms */
    hid_err(t500rs->hdev, "Unsupported periodic waveform: %d\n",
            effect->u.periodic.waveform);
    return -EINVAL;
  }

  hw_id = t500rs_logical_to_hw_id(effect->id);

  /* Send packet sequence for periodic effect */
  ret = t500rs_send_packet_sequence(t500rs, state, hw_id, t500rs_seq_periodic,
                                    sizeof(t500rs_seq_periodic) /
                                        sizeof(t500rs_seq_periodic[0]));
  if (ret) {
    hid_err(t500rs->hdev, "Failed to send %s effect sequence: %d\n", type_name,
            ret);
    return ret;
  }

  T500RS_DBG(t500rs, "%s effect %d uploaded\n", type_name, effect->id);
  return 0;
}

/*
 * Upload ramp effect.
 *
 * Per Windows captures (T500RS_USB_Protocol_Analysis.md):
 * - Ramp uses same 0x04 packet structure as periodic (code 0x2a)
 * - Packet sequence: 0x01 + 0x02 + 0x04 + 0x41
 * - Start/end levels encoded in magnitude/offset fields
 * - Period field encodes ramp duration
 */
static int t500rs_upload_ramp(struct t500rs_device_entry *t500rs,
                              const struct tmff2_effect_state *state) {
  const struct ff_effect *effect = &state->effect;
  int ret;
  int hw_id;

  hw_id = t500rs_logical_to_hw_id(effect->id);

  /* Send packet sequence for ramp effect */
  ret = t500rs_send_packet_sequence(t500rs, state, hw_id, t500rs_seq_ramp,
                                    sizeof(t500rs_seq_ramp) /
                                        sizeof(t500rs_seq_ramp[0]));
  if (ret) {
    hid_err(t500rs->hdev, "Failed to send ramp effect sequence: %d\n", ret);
    return ret;
  }

  T500RS_DBG(t500rs, "Ramp effect %d uploaded\n", effect->id);
  return 0;
}

/* Upload effect */
static int t500rs_upload_effect(void *data,
                                const struct tmff2_effect_state *state) {
  struct t500rs_device_entry *t500rs = data;
  const struct ff_effect *effect;
  int ret;

  effect = &state->effect;

  /* Validate effect ID range */
  if (effect->id >= T500RS_MAX_EFFECTS) {
    hid_err(t500rs->hdev, "Effect ID %d exceeds maximum %d\n", effect->id,
            T500RS_MAX_EFFECTS);
    return -EINVAL;
  }

  /* Validate effect parameters based on type */
  switch (effect->type) {
  case FF_CONSTANT:
    /* Validate constant force level */
    if (effect->u.constant.level < -32767 || effect->u.constant.level > 32767) {
      hid_err(t500rs->hdev, "Constant level %d out of range [-32767, 32767]\n",
              effect->u.constant.level);
      return -EINVAL;
    }
    break;

  case FF_PERIODIC:
    /* Validate periodic effect parameters */
    if (effect->u.periodic.magnitude < 0 ||
        effect->u.periodic.magnitude > 32767) {
      hid_err(t500rs->hdev, "Periodic magnitude %d out of range [0, 32767]\n",
              effect->u.periodic.magnitude);
      return -EINVAL;
    }
    if (effect->u.periodic.offset < -32768 ||
        effect->u.periodic.offset > 32767) {
      hid_err(t500rs->hdev, "Periodic offset %d out of range [-32768, 32767]\n",
              effect->u.periodic.offset);
      return -EINVAL;
    }
    if (effect->u.periodic.phase > 35999) {
      hid_err(t500rs->hdev, "Periodic phase %u exceeds maximum 35999\n",
              effect->u.periodic.phase);
      return -EINVAL;
    }
    break;

  case FF_RAMP:
    /* Validate ramp effect parameters */
    if (effect->u.ramp.start_level < -32767 ||
        effect->u.ramp.start_level > 32767) {
      hid_err(t500rs->hdev,
              "Ramp start level %d out of range [-32767, 32767]\n",
              effect->u.ramp.start_level);
      return -EINVAL;
    }
    if (effect->u.ramp.end_level < -32767 || effect->u.ramp.end_level > 32767) {
      hid_err(t500rs->hdev, "Ramp end level %d out of range [-32767, 32767]\n",
              effect->u.ramp.end_level);
      return -EINVAL;
    }
    break;

  case FF_SPRING:
  case FF_DAMPER:
  case FF_FRICTION:
  case FF_INERTIA:
    break;

  default:
    hid_err(t500rs->hdev, "Unsupported effect type: %d\n", effect->type);
    return -EINVAL;
  }

  /* Validate common parameters */
  if (effect->direction > 35999) {
    hid_err(t500rs->hdev, "Direction %u exceeds maximum 35999\n",
            effect->direction);
    return -EINVAL;
  }
  if (effect->replay.delay > 65535) {
    hid_err(t500rs->hdev, "Delay %u exceeds maximum 65535\n",
            effect->replay.delay);
    return -EINVAL;
  }

  switch (effect->type) {
  case FF_CONSTANT:
    ret = t500rs_upload_constant(t500rs, state);
    break;
  case FF_SPRING:
  case FF_DAMPER:
  case FF_FRICTION:
  case FF_INERTIA:
    ret = t500rs_upload_condition(t500rs, state);
    break;
  case FF_PERIODIC:
  case FF_SINE:
  case FF_TRIANGLE:
  case FF_SAW_UP:
  case FF_SAW_DOWN:
    ret = t500rs_upload_periodic(t500rs, state);
    break;
  case FF_RAMP:
    ret = t500rs_upload_ramp(t500rs, state);
    break;
  default:
    hid_err(t500rs->hdev, "Unsupported effect type: %d\n", effect->type);
    return -EINVAL;
  }

  if (ret < 0) {
    hid_err(t500rs->hdev, "Failed to upload effect type %d, id %d: %d\n",
            effect->type, effect->id, ret);
  }
  return ret;
}

/*
 * Play effect - send START command (0x41) for the effect.
 */
static int t500rs_play_effect(void *data,
                              const struct tmff2_effect_state *state) {
  struct t500rs_device_entry *t500rs = data;
  const struct ff_effect *effect = &state->effect;
  int ret;
  int hw_id;

  /* Validate effect ID range */
  if (effect->id >= T500RS_MAX_EFFECTS) {
    hid_err(t500rs->hdev, "Effect ID %d exceeds maximum %d\n", effect->id,
            T500RS_MAX_EFFECTS);
    return -EINVAL;
  }

  /* Validate effect type is supported */
  switch (effect->type) {
  case FF_CONSTANT:
  case FF_PERIODIC:
  case FF_RAMP:
  case FF_SPRING:
  case FF_DAMPER:
  case FF_FRICTION:
  case FF_INERTIA:
    break;
  default:
    hid_err(t500rs->hdev, "Unsupported effect type for play: %d\n",
            effect->type);
    return -EINVAL;
  }

  hw_id = t500rs_logical_to_hw_id(effect->id);

  ret = t500rs_send_start(t500rs, (u8)hw_id);
  if (ret == 0) {
    T500RS_DBG(t500rs, "Started effect %d (hw_id=%d)\n", effect->id, hw_id);
  }
  return ret;
}

/*
 * Stop effect - send STOP command (0x41).
 * No slot freeing needed with simplified hw_id = logical_id + 1 mapping.
 */
static int t500rs_stop_effect(void *data,
                              const struct tmff2_effect_state *state) {
  struct t500rs_device_entry *t500rs = data;
  int hw_id;

  /* Validate effect ID range */
  if (state->effect.id >= T500RS_MAX_EFFECTS) {
    hid_err(t500rs->hdev, "Effect ID %d exceeds maximum %d\n", state->effect.id,
            T500RS_MAX_EFFECTS);
    return -EINVAL;
  }

  if (!t500rs->send_buffer) {
    hid_err(t500rs->hdev, "t500rs_stop_effect: NULL send buffer\n");
    return -ENOMEM;
  }

  hw_id = t500rs_logical_to_hw_id(state->effect.id);

  /* STOP command uses hw_id as effect_id to match 0x01 packet */
  return t500rs_send_stop(t500rs, (u8)hw_id);
}

/* Update effect - send parameter updates without re-uploading */
static int t500rs_update_effect(void *data,
                                const struct tmff2_effect_state *state) {
  struct t500rs_device_entry *t500rs = data;
  const struct ff_effect *effect = &state->effect;
  const struct ff_effect *old = &state->old;
  u8 *buf;
  int hw_id;

  if (!t500rs)
    return -ENODEV;

  buf = t500rs->send_buffer;
  if (!buf)
    return -ENOMEM;

  hw_id = t500rs_logical_to_hw_id(effect->id);

  switch (effect->type) {
  case FF_CONSTANT: {
    int level = effect->u.constant.level;
    u16 direction = effect->direction;

    if (level == old->u.constant.level && direction == old->direction)
      return 0;

    s8 signed_level = t500rs_scale_const_with_direction(level, direction);
    u16 param_sub, env_sub;
    t500rs_index_to_subtypes(hw_id, &param_sub, &env_sub);
    struct t500rs_r03_const *r3 = (struct t500rs_r03_const *)buf;
    t500rs_build_r03_constant(r3, (u8)(param_sub & 0xff), signed_level);
    return t500rs_send_hid(t500rs, (u8 *)r3, sizeof(*r3));
  }
  case FF_PERIODIC: {
    /* Apply direction projection to magnitude and adjust phase if needed */
    u16 phase_raw = effect->u.periodic.phase;
    u8 mag = t500rs_scale_periodic_with_direction(effect->u.periodic.magnitude,
                                                   effect->direction, &phase_raw);
    u8 phase = t500rs_scale_periodic_phase(phase_raw);
    u8 offset = t500rs_scale_periodic_offset(effect->u.periodic.offset);
    u16 period_ms = effect->u.periodic.period;
    u16 param_sub, env_sub;
    if (period_ms == 0) {
      hid_err(t500rs->hdev, "Periodic effect period cannot be zero\n");
      return -EINVAL;
    }

    t500rs_index_to_subtypes(hw_id, &param_sub, &env_sub);
    struct t500rs_pkt_r04_periodic_ramp *p =
        (struct t500rs_pkt_r04_periodic_ramp *)buf;
    t500rs_build_r04_periodic(p, (u8)(param_sub & 0xff), mag, offset, phase,
                              period_ms);
    return t500rs_send_hid(t500rs, buf, sizeof(*p));
  }
  case FF_RAMP: {
    u16 duration_ms = effect->replay.length;
    if (duration_ms == 0) {
      hid_err(t500rs->hdev, "Ramp effect duration cannot be zero\n");
      return -EINVAL;
    }
    u16 param_sub, env_sub;
    t500rs_index_to_subtypes(hw_id, &param_sub, &env_sub);
    struct t500rs_pkt_r04_periodic_ramp *p =
        (struct t500rs_pkt_r04_periodic_ramp *)buf;
    t500rs_build_r04_ramp(p, (u8)(param_sub & 0xff), effect->u.ramp.start_level,
                          effect->u.ramp.end_level, duration_ms);
    return t500rs_send_hid(t500rs, buf, sizeof(*p));
  }
  case FF_SPRING:
  case FF_DAMPER:
  case FF_FRICTION:
  case FF_INERTIA: {
    /*
     * Skip update if parameters unchanged - prevents micro-pulse/rumble
     * when games spam identical condition updates.
     */
    const struct ff_condition_effect *cond = &effect->u.condition[0];
    const struct ff_condition_effect *cond_old = &old->u.condition[0];
    u16 param_sub, env_sub;

    if (cond->right_coeff == cond_old->right_coeff &&
        cond->left_coeff == cond_old->left_coeff &&
        cond->right_saturation == cond_old->right_saturation &&
        cond->left_saturation == cond_old->left_saturation &&
        cond->deadband == cond_old->deadband &&
        cond->center == cond_old->center && effect->type == old->type)
      return 0;

    t500rs_index_to_subtypes(hw_id, &param_sub, &env_sub);
    /* Calculate saturation value based on effect type */
    u8 saturation;
    switch (effect->type) {
    case FF_SPRING:
      saturation = T500RS_SAT_SPRING;
      break;
    case FF_DAMPER:
      saturation = T500RS_SAT_DAMPER;
      break;
    case FF_FRICTION:
      saturation = T500RS_SAT_FRICTION;
      break;
    case FF_INERTIA:
      saturation = T500RS_SAT_INERTIA;
      break;
    default:
      saturation = T500RS_SAT_DAMPER; /* Default to damper level */
      break;
    }
    struct t500rs_pkt_r05_condition *p = (struct t500rs_pkt_r05_condition *)buf;
    t500rs_build_r05_condition(p, (u8)(param_sub & 0xff), saturation,
                               cond->deadband, cond->center);
    return t500rs_send_hid(t500rs, buf, sizeof(*p));
  }
  default:
    return 0;
  }
}

/* Set autocenter */
static int t500rs_set_autocenter(void *data, u16 autocenter) {
  struct t500rs_device_entry *t500rs = data;
  u8 *buf;
  int ret;
  u8 autocenter_percent;

  if (!t500rs)
    return -ENODEV;

  autocenter_percent = (u8)((autocenter * 100) / 65535);

  /* Wine compatibility: Some games (e.g., LFS under Wine) set autocenter to
   * 100%% at startup and never release it. That leaves a permanent strong
   * centering force which masks/overpowers other forces. To avoid this, ignore
   * requests that try to set maximum autocenter (100%). Disabling (0) is still
   * honored; lower values are allowed. */
  if (autocenter_percent >= 100) {
    hid_warn(t500rs->hdev,
             "Ignoring 100%% autocenter request (Wine/LFS compatibility)");
    return 0;
  }

  buf = t500rs->send_buffer;
  if (!buf)
    return -ENOMEM;

  /* Enable autocenter: Report 0x40 0x04 0x01 */
  buf[0] = 0x40;
  buf[1] = 0x04;
  buf[2] = 0x01; /* Enable */
  buf[3] = 0x00;
  ret = t500rs_send_hid(t500rs, buf, 4);
  if (ret)
    return ret;

  /* Set autocenter strength: Report 0x40 0x03 [value] */
  buf[0] = 0x40;
  buf[1] = 0x03;
  buf[2] = autocenter_percent; /* 0-100 percentage */
  buf[3] = 0x00;
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
static int t500rs_set_range(void *data, u16 range) {
  struct t500rs_device_entry *t500rs = data;
  u8 *buf;
  int ret;
  u16 range_value;

  /* Validate range - minimum 40 degrees, maximum 1080 degrees */
  if (range < T500RS_RANGE_MIN) {
    range = T500RS_RANGE_MIN;
  }
  if (range > T500RS_RANGE_MAX) {
    range = T500RS_RANGE_MAX;
  }

  /* Use DMA-safe preallocated buffer */
  buf = t500rs->send_buffer;

  T500RS_DBG(t500rs, "Setting wheel range to %u degrees\n", range);

  /* Device expects LITTLE-ENDIAN and value = range * 60. */
  range_value = range * 60;

  /* Send Report 0x40 0x11 [value_lo] [value_hi] to set range */
  buf[0] = 0x40;
  buf[1] = 0x11;
  buf[2] = range_value & 0xFF;        /* Low byte first (little-endian) */
  buf[3] = (range_value >> 8) & 0xFF; /* High byte second */

  ret = t500rs_send_hid(t500rs, buf, 4);
  if (ret) {
    hid_err(t500rs->hdev, "Failed to send range command: %d\n", ret);
    return ret;
  }

  /* Apply settings with Report 0x42 0x05 */
  buf[0] = 0x42;
  buf[1] = 0x05;
  ret = t500rs_send_hid(t500rs, buf, 2);
  if (ret) {
    hid_err(t500rs->hdev, "Failed to apply range settings: %d\n", ret);
    return ret;
  }

  T500RS_DBG(t500rs, "Range set to %u degrees (final value=0x%04x)\n", range,
             range_value);

  return 0;
}

/* Initialize T500RS device */
static int t500rs_wheel_init(struct tmff2_device_entry *tmff2, int open_mode) {
  struct t500rs_device_entry *t500rs = NULL;
  u8 *init_buf; /* Will use send_buffer for DMA-safe transfers */
  int ret;
  /* Sanity check protocol main-upload packet size against documentation */
  BUILD_BUG_ON(sizeof(struct t500rs_pkt_r01_main) != 15);

  /* Validate input parameters */
  if (!tmff2) {
    pr_err("t500rs_wheel_init: NULL tmff2 structure\n");
    return -EINVAL;
  }
  if (!tmff2->hdev || !tmff2->input_dev) {
    pr_err("t500rs_wheel_init: Invalid tmff2 structure (missing hdev or "
           "input_dev)\n");
    return -EINVAL;
  }

  hid_dbg(tmff2->hdev, "T500RS: Initializing HID mode\n");

  /* Allocate device data */
  t500rs = kzalloc(sizeof(*t500rs), GFP_KERNEL);
  if (!t500rs) {
    hid_err(tmff2->hdev, "Failed to allocate t500rs device structure\n");
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
    hid_err(tmff2->hdev, "Failed to allocate send buffer (%zu bytes)\n",
            t500rs->buffer_length);
    ret = -ENOMEM;
    goto err_buffer_alloc;
  }

  /* Store device data in tmff2 BEFORE any operations that might fail */
  tmff2->data = t500rs;

  /* Use send_buffer for all HID transfers */
  init_buf = t500rs->send_buffer;

  T500RS_DBG(t500rs, "Sending initialization sequence...\n");

  /* Report 0x42 - Init/status commands (2 bytes each)
   * Windows sends these at startup: 0x42 0x04, 0x42 0x05, 0x42 0x00
   * These appear to initialize the FFB subsystem state.
   */
  memset(init_buf, 0, 2);
  init_buf[0] = 0x42;
  init_buf[1] = 0x04;
  ret = t500rs_send_hid(t500rs, init_buf, 2);
  if (ret) {
    hid_warn(t500rs->hdev, "Init command 0x42 0x04 failed: %d\n", ret);
  }

  memset(init_buf, 0, 2);
  init_buf[0] = 0x42;
  init_buf[1] = 0x05;
  ret = t500rs_send_hid(t500rs, init_buf, 2);
  if (ret) {
    hid_warn(t500rs->hdev, "Init command 0x42 0x05 failed: %d\n", ret);
  }

  memset(init_buf, 0, 2);
  init_buf[0] = 0x42;
  init_buf[1] = 0x00;
  ret = t500rs_send_hid(t500rs, init_buf, 2);
  if (ret) {
    hid_warn(t500rs->hdev, "Init command 0x42 0x00 failed: %d\n", ret);
  }

  /* Report 0x40 - Enable FFB (4 bytes)
   * Magic value seen in captures that enables FFB on the base.
   */
  memset(init_buf, 0, 4);
  init_buf[0] = 0x40;
  init_buf[1] = 0x11;
  init_buf[2] = 0x42;
  init_buf[3] = 0x7b;
  ret = t500rs_send_hid(t500rs, init_buf, 4);
  if (ret) {
    hid_warn(t500rs->hdev, "Init command 2 (0x40 enable) failed: %d\n", ret);
  }

  /* Report 0x40 - Disable built-in autocenter (4 bytes) */
  memset(init_buf, 0, 4);
  init_buf[0] = 0x40;
  init_buf[1] = 0x04;
  /* b2..b3 = 0x0000 -> disable autocenter.
   * Keep explicit zeros even though memset() clears them, to document the
   * wire image.
   */
  init_buf[2] = 0x00;
  init_buf[3] = 0x00;
  ret = t500rs_send_hid(t500rs, init_buf, 4);
  if (ret) {
    hid_warn(t500rs->hdev, "Init command 3 (0x40 config) failed: %d\n", ret);
  }

  /* Report 0x43 - Set global gain (2 bytes)
   * Start at maximum device gain; the FFB gain callback will adjust later.
   */
  memset(init_buf, 0, 2);
  init_buf[0] = 0x43;
  init_buf[1] = 0xFF;
  ret = t500rs_send_hid(t500rs, init_buf, 2);
  if (ret) {
    hid_warn(t500rs->hdev, "Init command 4 (0x43) failed: %d\n", ret);
  }

  hid_info(t500rs->hdev, "T500RS initialized successfully (HID mode)\n");
  T500RS_DBG(t500rs, "Buffer: %zu bytes\n", t500rs->buffer_length);

  /* Advertise capabilities now that init succeeded */
  tmff2->params = t500rs_params;
  tmff2->max_effects = T500RS_MAX_EFFECTS;
  memcpy(tmff2->supported_effects, t500rs_effects, sizeof(t500rs_effects));

  return 0;

err_buffer_alloc:
  /* t500rs structure is allocated but not yet stored in tmff2->data */
  kfree(t500rs);
err_alloc:
  return ret;
}

/* Cleanup T500RS device */
static int t500rs_wheel_destroy(void *data) {
  struct t500rs_device_entry *t500rs = data;

  if (!t500rs) {
    pr_warn("t500rs_wheel_destroy: NULL data pointer\n");
    return 0;
  }

  T500RS_DBG(t500rs, "T500RS: Cleaning up\n");

  /* Free resources in reverse order of allocation */
  if (t500rs->send_buffer) {
    kfree(t500rs->send_buffer);
    t500rs->send_buffer = NULL;
  }

  kfree(t500rs);

  return 0;
}

/* Populate API callbacks */
int t500rs_populate_api(struct tmff2_device_entry *tmff2) {

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
