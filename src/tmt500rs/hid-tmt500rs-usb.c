// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Force feedback support for Thrustmaster T500RS
 *
 * USB INTERRUPT implementation
 * Uses endpoint 0x01 OUT for all communication
 */

#include <linux/hid.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <linux/usb.h>

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>

#include "../hid-tmff2.h"

/* T500RS Constants */
#define T500RS_MAX_EFFECTS 16
#define T500RS_BUFFER_LENGTH 32 /* USB endpoint max packet size */
#define T500RS_EP_OUT 0x01      /* INTERRUPT OUT endpoint */

/* USB timeout */
#define T500RS_USB_TIMEOUT 1000 /* 1 second */

/* Gain scaling */
#define GAIN_MAX 65535

/* Packet structs (packed) to reduce magic bytes on T500RS protocol */
struct t500rs_r02_envelope {
  u8 id;      /* 0x02 */
  u8 subtype; /* 0x1c */
  u8 zero;    /* 0x00 */
  __le16 attack_len;
  u8 attack_lvl; /* 0..255 */
  __le16 fade_len;
  u8 fade_lvl; /* 0..255 */
} __packed;

struct t500rs_r03_const {
  u8 id;    /* 0x03 */
  u8 code;  /* 0x0e */
  u8 zero;  /* 0x00 */
  s8 level; /* -127..127 */
} __packed;

struct t500rs_r04_periodic {
  u8 id;         /* 0x04 */
  u8 code;       /* 0x0e */
  u8 zero;       /* 0x00 */
  u8 magnitude;  /* 0..127 */
  u8 offset;     /* 0 */
  u8 phase;      /* 0 */
  __le16 period; /* frequency (Hz*100) */
} __packed;

struct t500rs_r04_ramp {
  u8 id;   /* 0x04 */
  u8 code; /* 0x0e */
  __le16 start;
  __le16 cur_val;  /* same as start */
  __le16 duration; /* ms */
  u8 zero;         /* 0 */
} __packed;

struct t500rs_r41_cmd {
  u8 id;        /* 0x41 */
  u8 effect_id; /* usually 0 on T500RS */
  u8 command;   /* 0x41 START, 0x00 STOP, 0x00 clear in init */
  u8 arg;       /* 0x01 */
} __packed;

/* Generic 0x01 main upload (15 bytes) — keep fields generic while unknown) */
struct t500rs_r01_main {
  u8 id;        /* 0x01 */
  u8 effect_id; /* see ID semantics note */
  u8 type;      /* 0x00 constant, 0x20..0x24 periodic/ramp */
  u8 b3;
  u8 b4;
  u8 b5;
  u8 b6;
  u8 b7;
  u8 b8;
  u8 b9;
  u8 b10;
  u8 b11;
  u8 b12;
  u8 b13;
  u8 b14;
} __packed;

/* Scale envelope level (0..32767) to device 8-bit (0..255) */
static inline u8 t500rs_scale_env_level(u16 level) {
  if (level > 32767)
    level = 32767;
  return (u8)((level * 255) / 32767);
}

/* Scale constant level (-32767..32767) to signed 8-bit (-127..127) */
static inline s8 t500rs_scale_const_level_s8(int level) {
  if (level > 32767)
    level = 32767;
  if (level < -32767)
    level = -32767;
  return (s8)((level * 127) / 32767);
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
  if (magnitude < 0)
    magnitude = -magnitude;
  if (magnitude > 32767)
    magnitude = 32767;
  return (u8)((magnitude * 127) / 32767);
}

/* Fill Report 0x02 (envelope) buffer for T500RS: 9 bytes total
 * buf[0]=0x02, buf[1]=0x1c, buf[2]=0x00,
 * buf[3..4]=attack_length (le16), buf[5]=attack_level (u8 0..255),
 * buf[6..7]=fade_length (le16),   buf[8]=fade_level (u8 0..255)
 */
static inline void
t500rs_fill_envelope_u02(u8 *buf, const struct ff_envelope *env, u8 subtype) {
  u16 a_len = env ? env->attack_length : 0;
  u16 f_len = env ? env->fade_length : 0;
  u8 a_lvl = env ? t500rs_scale_env_level(env->attack_level) : 0;
  u8 f_lvl = env ? t500rs_scale_env_level(env->fade_level) : 0;

  struct t500rs_r02_envelope *r = (struct t500rs_r02_envelope *)buf;
  memset(r, 0, sizeof(*r));
  r->id = 0x02;
  r->subtype = subtype;
  r->zero = 0x00;
  r->attack_len = cpu_to_le16(a_len);
  r->attack_lvl = a_lvl;
  r->fade_len = cpu_to_le16(f_len);
  r->fade_lvl = f_lvl;
}

/* Debug logging helper: pass struct t500rs_device_entry * explicitly */
#define T500RS_DBG(dev, fmt, ...) hid_dbg((dev)->hdev, fmt, ##__VA_ARGS__)

/* T500RS device data */
#define T500RS_MAX_EFFECTS 16
struct t500rs_device_entry {
  struct hid_device *hdev;
  struct input_dev *input_dev;
  struct usb_device *usbdev;
  struct usb_interface *usbif;

  int ep_out; /* INTERRUPT OUT endpoint address */

  u8 *send_buffer;
  size_t buffer_length;

  /* Current wheel range for smooth transitions */
  u16 current_range; /* Current rotation range in degrees */
};

/* Supported parameters */
static const unsigned long t500rs_params =
    PARAM_SPRING_LEVEL | PARAM_DAMPER_LEVEL | PARAM_FRICTION_LEVEL |
    PARAM_GAIN | PARAM_RANGE;

/* Supported effects */
static const signed short t500rs_effects[] = {
    FF_CONSTANT, FF_SPRING, FF_DAMPER,   FF_FRICTION,   FF_INERTIA,
    FF_PERIODIC, FF_SINE,   FF_TRIANGLE, FF_SQUARE,     FF_SAW_UP,
    FF_SAW_DOWN, FF_RAMP,   FF_GAIN,     FF_AUTOCENTER, -1};

/* Forward declarations to avoid implicit declarations before worker uses them
 */
static int t500rs_send_usb(struct t500rs_device_entry *t500rs, const u8 *data,
                           size_t len);
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

static int t500rs_set_gain(void *data, u16 gain) {
  struct t500rs_device_entry *t500rs = data;
  u8 *buf;
  u8 device_gain_byte;
  if (!t500rs)
    return -ENODEV;
  buf = t500rs->send_buffer;
  if (!buf)
    return -ENOMEM;
  /* Scale 0..65535 to device 0..255 */
  device_gain_byte = (u8)((gain * 255) / GAIN_MAX);
  buf[0] = 0x43;
  buf[1] = device_gain_byte;
  return t500rs_send_usb(t500rs, buf, 2);
}

/* Send data via USB INTERRUPT transfer (blocking) */
static int t500rs_send_usb(struct t500rs_device_entry *t500rs, const u8 *data,
                           size_t len) {
  int ret, transferred;
  if (!t500rs || !data || len == 0 || len > T500RS_BUFFER_LENGTH)
    return -EINVAL;

  ret = usb_interrupt_msg(t500rs->usbdev,
                          usb_sndintpipe(t500rs->usbdev, t500rs->ep_out),
                          (void *)data, len, &transferred, T500RS_USB_TIMEOUT);
  if (ret < 0)
    return ret;
  return (transferred == len) ? 0 : -EIO;
}

/* Send pre-upload STOP (Report 0x41 with effect_id=0, command=0x00, arg=0x01)
 * Matches Windows behavior of clearing the slot before (re)uploading.
 */
static inline int t500rs_send_pre_stop(struct t500rs_device_entry *t500rs) {
  u8 *buf;
  struct t500rs_r41_cmd *r41;
  if (!t500rs)
    return -ENODEV;
  buf = t500rs->send_buffer;
  if (!buf)
    return -ENOMEM;
  r41 = (struct t500rs_r41_cmd *)buf;
  r41->id = 0x41;
  r41->effect_id = 0x00;
  r41->command = 0x00; /* STOP/CLEAR */
  r41->arg = 0x01;
  return t500rs_send_usb(t500rs, buf, sizeof(*r41));
}

/* Upload constant force effect */
static int t500rs_upload_constant(struct t500rs_device_entry *t500rs,
                                  const struct tmff2_effect_state *state) {
  const struct ff_effect *effect = &state->effect;
  u8 *buf = t500rs->send_buffer; /* Use DMA-safe buffer */
  int ret;
  int level = effect->u.constant.level;

  /* Note: Gain is applied in play_effect, not here */
  T500RS_DBG(t500rs, "Upload constant: id=%d, level=%d\n", effect->id, level);

  /* Pre-upload STOP to clear the slot (Windows parity) */
  ret = t500rs_send_pre_stop(t500rs);
  if (ret) {
    hid_err(t500rs->hdev, "Pre-upload STOP failed: %d\n", ret);
    return ret;
  }

  /* Report 0x02 - Envelope (attack/fade) */
  t500rs_fill_envelope_u02(buf, &effect->u.constant.envelope, 0x1c);
  ret = t500rs_send_usb(t500rs, buf, 9);
  if (ret) {
    hid_err(t500rs->hdev, "Failed to send Report 0x02: %d\n", ret);
    return ret;
  }

  /*
   * IMPORTANT: T500RS Effect ID (buf[1]) semantics
   * - All Report 0x01 "main effect upload" messages MUST use EffectID=0x00.
   *   Using per-effect IDs here breaks playback (e.g., constant force).
   * - Report 0x41 START/STOP also requires EffectID=0x00, except special
   *   cases like stopping autocenter by its fixed ID during init.
   * This matches behavior observed from the Windows driver and on-device tests.
   */
  /* Report 0x01 - Main effect upload (constant, fixed subtypes) */
  {
    struct t500rs_r01_main *m = (struct t500rs_r01_main *)buf;
    memset(m, 0, sizeof(*m));
    m->id = 0x01;
    m->effect_id = 0x00; /* Device expects Effect ID 0 for 0x01 on T500RS */
    m->type = 0x00;      /* Constant force type */
    m->b3 = 0x40;
    m->b4 = 0xff; /* Windows uses 0xff */
    m->b5 = 0xff; /* Windows uses 0xff */
    m->b6 = 0x00;
    m->b7 = 0xff;
    m->b8 = 0xff;
    m->b9 = 0x0e; /* Parameter subtype reference (fixed) */
    m->b10 = 0x00;
    m->b11 = 0x1c; /* Envelope subtype reference (fixed) */
    m->b12 = 0x00;
    m->b13 = 0x00;
    m->b14 = 0x00;
  }

  ret = t500rs_send_usb(t500rs, buf, sizeof(struct t500rs_r01_main));
  if (ret) {
    hid_err(t500rs->hdev, "Failed to send Report 0x01: %d\n", ret);
    return ret;
  }

  /* Report 0x03 - Constant force level (param subtype, honours direction) */
  {
    s8 signed_level = t500rs_scale_const_with_direction(level, effect->direction);
    struct t500rs_r03_const *r3 = (struct t500rs_r03_const *)buf;
    r3->id = 0x03;
    r3->code = 0x0e;
    r3->zero = 0x00;
    r3->level = signed_level;
  }
  ret = t500rs_send_usb(t500rs, buf, sizeof(struct t500rs_r03_const));
  if (ret) {
    hid_err(t500rs->hdev, "Failed to send Report 0x03 (const level): %d\n",
            ret);
    return ret;
  }

  T500RS_DBG(t500rs,
             "Constant effect %d uploaded (0x02 + 0x01 + 0x03 sequence)\n",
             effect->id);
  return 0;
}

/* Upload spring/damper/friction effect */
static int t500rs_upload_condition(struct t500rs_device_entry *t500rs,
                                   const struct tmff2_effect_state *state) {
  const struct ff_effect *effect = &state->effect;
  u8 *buf = t500rs->send_buffer; /* Use DMA-safe buffer */
  u8 effect_type;
  int ret;
  u8 effect_gain;
  int right_strength, left_strength;

  /* Subtype indices derived from effect->id to match Windows subtype system */
  unsigned int idx = (unsigned int)effect->id;
  u8 param_sub = (u8)(0x0e + (0x1c * idx));
  u8 env_sub_first = (u8)(0x1c + (0x1c * idx));

  /* Determine effect type and select appropriate gain */
  switch (effect->type) {
  case FF_SPRING:
    effect_type = 0x40;
    effect_gain = spring_level;
    break;
  case FF_DAMPER:
    effect_type = 0x41;
    effect_gain = damper_level;
    break;
  case FF_FRICTION:
    effect_type = 0x41;
    effect_gain = friction_level;
    break;
  case FF_INERTIA:
    effect_type = 0x41;
    effect_gain = 100;
    break;
  default:
    return -EINVAL;
  }

  /* Get effect parameters and apply per-effect gain */
  /* Condition effects use right_saturation and left_saturation (0-65535) */
  right_strength = effect->u.condition[0].right_saturation;
  left_strength = effect->u.condition[0].left_saturation;

  /* Apply per-effect level scaling (0-100) */
  right_strength = (right_strength * effect_gain) / 100;
  left_strength = (left_strength * effect_gain) / 100;

  /* Scale to device range (0-127) */
  right_strength = (right_strength * 127) / 65535;
  left_strength = (left_strength * 127) / 65535;

  T500RS_DBG(t500rs,
             "Upload condition: id=%d, type=0x%02x, gain=%u%%, R=%d, L=%d\n",
             effect->id, effect_type, effect_gain, right_strength,
             left_strength);

  /* Pre-upload STOP to clear the slot (Windows parity) */
  ret = t500rs_send_pre_stop(t500rs);
  if (ret) {
    hid_err(t500rs->hdev, "Pre-upload STOP failed: %d\n", ret);
    return ret;
  }

  /* Report 0x05 - Condition parameters (coefficients) */
  memset(buf, 0, 11);
  buf[0] = 0x05;
  buf[1] = param_sub;
  buf[2] = 0x00;
  buf[3] = (u8)right_strength;
  buf[4] = (u8)left_strength;
  buf[5] = 0x00;I
  buf[6] = 0x00;
  buf[7] = 0x00;
  buf[8] = 0x00;
  buf[9] = (effect->type == FF_SPRING) ? 0x54 : 0x64;
  buf[10] = (effect->type == FF_SPRING) ? 0x54 : 0x64;
  ret = t500rs_send_usb(t500rs, buf, 11);
  if (ret)
    return ret;

  /* Report 0x05 - Condition parameters (deadband/center) */
  memset(buf, 0, 11);
  buf[0] = 0x05;
  buf[1] = env_sub_first;
  buf[2] = 0x00;
  buf[3] = 0x00; /* Deadband */
  buf[4] = 0x00; /* Center */
  buf[5] = 0x00;
  buf[6] = 0x00;
  buf[7] = 0x00;
  buf[8] = 0x00;
  buf[9] = (effect->type == FF_SPRING) ? 0x46 : 0x64;
  buf[10] = (effect->type == FF_SPRING) ? 0x54 : 0x64;
  ret = t500rs_send_usb(t500rs, buf, 11);
  if (ret)
    return ret;

  /* NOTE: On T500RS, Report 0x01 MUST use EffectID=0x00; enforce it here. */
  /* Report 0x01 - Main effect upload */
  {
    struct t500rs_r01_main *m = (struct t500rs_r01_main *)buf;
    memset(m, 0, sizeof(*m));
    m->id = 0x01;
    m->effect_id = 0x00;
    m->type = effect_type;
    m->b3 = 0x40;
    m->b4 = 0x17;
    m->b5 = 0x25;
    m->b6 = 0x00;
    m->b7 = 0xff;
    m->b8 = 0xff;
    m->b9 = param_sub;
    m->b10 = 0x00;
    m->b11 = env_sub_first;
    m->b12 = 0x00;
    m->b13 = 0x00;
    m->b14 = 0x00;
  }
  ret = t500rs_send_usb(t500rs, buf, sizeof(struct t500rs_r01_main));
  if (ret)
    return ret;

  return 0;
}

/* Upload periodic effect (sine, square, triangle, saw) */
static int t500rs_upload_periodic(struct t500rs_device_entry *t500rs,
                                  const struct tmff2_effect_state *state) {
  const struct ff_effect *effect = &state->effect;
  u8 *buf = t500rs->send_buffer; /* Use DMA-safe buffer */
  int ret;
  u8 effect_type;
  const char *type_name;
  int magnitude = effect->u.periodic.magnitude;
  u16 period = effect->u.periodic.period;
  u8 mag;

  /* Determine waveform type */
  switch (effect->u.periodic.waveform) {
  case FF_SQUARE:
    effect_type = 0x20;
    type_name = "square";
    break;
  case FF_TRIANGLE:
    effect_type = 0x21;
    type_name = "triangle";
    break;
  case FF_SINE:
    effect_type = 0x22;
    type_name = "sine";
    break;
  case FF_SAW_UP:
    effect_type = 0x23;
    type_name = "sawtooth_up";
    break;
  case FF_SAW_DOWN:
    effect_type = 0x24;
    type_name = "sawtooth_down";
    break;
  default:
    hid_err(t500rs->hdev, "Unknown periodic waveform: %d\n",
            effect->u.periodic.waveform);
    return -EINVAL;
  }

  /* Magnitude - scale to 0-127 with saturation */
  mag = t500rs_scale_mag_u7(magnitude);
  /* Subtype indices derived from effect->id to match Windows subtype system */
  unsigned int idx = (unsigned int)effect->id;
  u8 param_sub = (u8)(0x0e + (0x1c * idx));
  u8 env_sub_first = (u8)(0x1c + (0x1c * idx));
  u8 env_sub_second = (u8)(env_sub_first + 0x1c);

  /* Period (ms) -> device frequency (Hz*100). Default to 100ms = 10 Hz if set to 0 */
  if (period == 0) {
    period = 100;
  }
  {
    u32 freq_hz100 = 100000U / period;
    if (freq_hz100 < 1U)
      freq_hz100 = 1U;
    if (freq_hz100 > 65535U)
      freq_hz100 = 65535U;
    T500RS_DBG(t500rs,
               "Upload %s: id=%d, magnitude=%d (0x%02x), period=%dms -> "
               "freq=%u (Hz*100)\n",
               type_name, effect->id, magnitude, mag, period,
               (unsigned)freq_hz100);
    /* Reuse 'period' variable to carry converted frequency to the packet write below */
    period = (u16)freq_hz100;
  }

  /* Pre-upload STOP to clear the slot (Windows parity) */
  ret = t500rs_send_pre_stop(t500rs);
  if (ret) {
    hid_err(t500rs->hdev, "Pre-upload STOP failed: %d\n", ret);
    return ret;
  }

  /* Report 0x02 - Envelope (first) */
  t500rs_fill_envelope_u02(buf, &effect->u.periodic.envelope, env_sub_first);
  ret = t500rs_send_usb(t500rs, buf, 9);
  if (ret) {
    hid_err(t500rs->hdev, "Failed to send Report 0x02: %d\n", ret);
    return ret;
  }

  /* NOTE: Device requires EffectID=0 for 0x01 uploads; see ID semantics above. */
  /* Report 0x01 - Main effect upload for periodic (set waveform/type) */
  {
    struct t500rs_r01_main *m = (struct t500rs_r01_main *)buf;
    memset(m, 0, sizeof(*m));
    m->id = 0x01;
    m->effect_id = 0x00;   /* Effect ID 0 required for T500RS 0x01 reports */
    m->type = effect_type; /* Waveform type (0x20..0x24) */
    m->b3 = 0x40;
    m->b4 = 0xff;
    m->b5 = 0xff;
    m->b6 = 0x00;
    m->b7 = 0xff;
    m->b8 = 0xff;
    m->b9 = param_sub; /* Parameter subtype reference (per-effect) */
    m->b10 = 0x00;
    m->b11 = env_sub_first; /* Envelope subtype reference (first) */
    m->b12 = 0x00;
    m->b13 = 0x00;
    m->b14 = 0x00;
  }
  ret = t500rs_send_usb(t500rs, buf, sizeof(struct t500rs_r01_main));
  if (ret) {
    hid_err(t500rs->hdev, "Failed to send Report 0x01 (periodic main): %d\n",
            ret);
    return ret;
  }
  /* Report 0x02 - Envelope (second) */
  t500rs_fill_envelope_u02(buf, &effect->u.periodic.envelope, env_sub_second);
  ret = t500rs_send_usb(t500rs, buf, 9);
  if (ret) {
    hid_err(t500rs->hdev, "Failed to send Report 0x02 (second): %d\n", ret);
    return ret;
  }

  /* Report 0x04 - Periodic parameters */
  {
    struct t500rs_r04_periodic *p = (struct t500rs_r04_periodic *)buf;
    memset(p, 0, sizeof(*p));
    p->id = 0x04;
    p->code = param_sub;
    p->zero = 0x00;
    p->magnitude = mag;
    p->offset = 0x00;
    p->phase = 0x00;
    p->period = cpu_to_le16(period);
  }
  ret = t500rs_send_usb(t500rs, buf, sizeof(struct t500rs_r04_periodic));
  if (ret) {
    hid_err(t500rs->hdev, "Failed to send Report 0x04: %d\n", ret);
    return ret;
  }

  /* NOTE: On T500RS, all 0x01 uploads MUST use EffectID=0x00 */
  /* Report 0x01 - Main effect upload (second) */
  {
    struct t500rs_r01_main *m = (struct t500rs_r01_main *)buf;
    memset(m, 0, sizeof(*m));
    m->id = 0x01;
    m->effect_id = 0x00;
    m->type = effect_type; /* Waveform type */
    m->b3 = 0x40;
    m->b4 = 0x17;
    m->b5 = 0x25;
    m->b6 = 0x00;
    m->b7 = 0xff;
    m->b8 = 0xff;
    m->b9 = param_sub;
    m->b10 = 0x00;
    m->b11 = env_sub_second;
    m->b12 = 0x00;
    m->b13 = 0x00;
    m->b14 = 0x00;
  }
  ret = t500rs_send_usb(t500rs, buf, sizeof(struct t500rs_r01_main));
  if (ret) {
    hid_err(t500rs->hdev, "Failed to send Report 0x01: %d\n", ret);
    return ret;
  }

  T500RS_DBG(t500rs, "%s effect %d uploaded\n", type_name, effect->id);
  return 0;
}

/* Upload ramp effect */
static int t500rs_upload_ramp(struct t500rs_device_entry *t500rs,
                              const struct tmff2_effect_state *state) {
  const struct ff_effect *effect = &state->effect;
  u8 *buf = t500rs->send_buffer; /* Use DMA-safe buffer */
  int ret;
  int start_level = effect->u.ramp.start_level;
  int end_level = effect->u.ramp.end_level;
  u16 duration_ms = effect->replay.length;
  u16 start_scaled;

  /* Subtype indices derived from effect->id to match Windows subtype system */
  unsigned int idx = (unsigned int)effect->id;
  u8 param_sub = (u8)(0x0e + (0x1c * idx));
  u8 env_sub_first = (u8)(0x1c + (0x1c * idx));
  u8 env_sub_second = (u8)(env_sub_first + 0x1c);

  /* Scale to 0-255 */
  start_scaled = (abs(start_level) * 0xff) / 32767;

  T500RS_DBG(t500rs,
             "Upload ramp: id=%d, start=%d, end=%d, duration=%dms\n",
             effect->id, start_level, end_level, duration_ms);

  /* Pre-upload STOP to clear the slot (Windows parity) */
  ret = t500rs_send_pre_stop(t500rs);
  if (ret) {
    hid_err(t500rs->hdev, "Pre-upload STOP failed: %d\n", ret);
    return ret;
  }

  /* Report 0x02 - Envelope */
  t500rs_fill_envelope_u02(buf, &effect->u.ramp.envelope, env_sub_first);
  ret = t500rs_send_usb(t500rs, buf, 9);
  if (ret) {
    hid_err(t500rs->hdev, "Failed to send Report 0x02: %d\n", ret);
    return ret;
  }

  /* NOTE: On T500RS, Report 0x01 MUST use EffectID=0x00 */
  /* Report 0x01 - Main effect upload (first) */
  {
    struct t500rs_r01_main *m = (struct t500rs_r01_main *)buf;
    memset(m, 0, sizeof(*m));
    m->id = 0x01;
    m->effect_id = 0x00;
    m->type = 0x24; /* Ramp type (0x24 = sawtooth down / ramp) */
    m->b3 = 0x40;
    m->b4 = duration_ms & 0xff;        /* Duration low byte */
    m->b5 = (duration_ms >> 8) & 0xff; /* Duration high byte */
    m->b6 = 0x00;
    m->b7 = 0xff;
    m->b8 = 0xff;
    m->b9 = param_sub;
    m->b10 = 0x00;
    m->b11 = env_sub_first; /* first envelope subtype */
    m->b12 = 0x00;
    m->b13 = 0x00;
    m->b14 = 0x00;
  }
  ret = t500rs_send_usb(t500rs, buf, sizeof(struct t500rs_r01_main));
  if (ret) {
    hid_err(t500rs->hdev, "Failed to send Report 0x01 (ramp first): %d\n", ret);
    return ret;
  }

  /* Report 0x02 - Envelope (second) */
  t500rs_fill_envelope_u02(buf, &effect->u.ramp.envelope, env_sub_second);
  ret = t500rs_send_usb(t500rs, buf, 9);
  if (ret) {
    hid_err(t500rs->hdev, "Failed to send Report 0x02 (second): %d\n", ret);
    return ret;
  }

  /* Report 0x04 - Ramp parameters */
  /* NOTE: T500RS doesn't support native ramp - just holds start level */
  {
    struct t500rs_r04_ramp *rr = (struct t500rs_r04_ramp *)buf;
    memset(rr, 0, sizeof(*rr));
    rr->id = 0x04;
    rr->code = param_sub;
    rr->start = cpu_to_le16(start_scaled);
    rr->cur_val = cpu_to_le16(start_scaled);
    rr->duration = cpu_to_le16(duration_ms);
    rr->zero = 0x00;
  }
  ret = t500rs_send_usb(t500rs, buf, sizeof(struct t500rs_r04_ramp));
  if (ret) {
    hid_err(t500rs->hdev, "Failed to send Report 0x04: %d\n", ret);
    return ret;
  }

  /* Report 0x01 - Main effect upload (second) */
  {
    struct t500rs_r01_main *m = (struct t500rs_r01_main *)buf;
    memset(m, 0, sizeof(*m));
    m->id = 0x01;
    m->effect_id = 0x00;
    m->type = 0x24; /* Ramp type (0x24 = sawtooth down / ramp) */
    m->b3 = 0x40;
    m->b4 = duration_ms & 0xff;        /* Duration low byte */
    m->b5 = (duration_ms >> 8) & 0xff; /* Duration high byte */
    m->b6 = 0x00;
    m->b7 = 0xff;
    m->b8 = 0xff;
    m->b9 = param_sub;
    m->b10 = 0x00;
    m->b11 = env_sub_second; /* second envelope subtype */
    m->b12 = 0x00;
    m->b13 = 0x00;
    m->b14 = 0x00;
  }
  ret = t500rs_send_usb(t500rs, buf, sizeof(struct t500rs_r01_main));
  if (ret) {
    hid_err(t500rs->hdev, "Failed to send Report 0x01 (ramp second): %d\n",
            ret);
    return ret;
  }

  T500RS_DBG(t500rs,
             "Ramp effect %d uploaded (dual 0x02 + dual 0x01)\n",
             effect->id);
  return 0;
}

/* Upload effect */
static int t500rs_upload_effect(void *data,
                                const struct tmff2_effect_state *state) {
  struct t500rs_device_entry *t500rs = data;
  const struct ff_effect *effect = &state->effect;

  if (!t500rs)
    return -ENODEV;

  switch (effect->type) {
  case FF_CONSTANT:
    return t500rs_upload_constant(t500rs, state);
  case FF_SPRING:
  case FF_DAMPER:
  case FF_FRICTION:
  case FF_INERTIA:
    return t500rs_upload_condition(t500rs, state);
  case FF_PERIODIC:
    return t500rs_upload_periodic(t500rs, state);
  case FF_RAMP:
    return t500rs_upload_ramp(t500rs, state);
  default:
    return -EINVAL;
  }
}

/* Play effect */
static int t500rs_play_effect(void *data,
                              const struct tmff2_effect_state *state) {
  struct t500rs_device_entry *t500rs = data;
  const struct ff_effect *effect = &state->effect;
  u8 *buf = t500rs->send_buffer; /* Use DMA-safe buffer */
  int ret;

  if (!t500rs)
    return -ENODEV;

  T500RS_DBG(t500rs,
             "Play effect: id=%d, type=0x%02x (FF_CONSTANT=0x%02x)\n",
             effect->id, effect->type, FF_CONSTANT);

  /* For constant force: send one level update (0x03) then START (0x41).
   * Apply direction before scaling to s8.
   */
  if (effect->type == FF_CONSTANT) {
    int level = effect->u.constant.level;
    u16 direction = effect->direction;
    s8 signed_level;
    signed_level = t500rs_scale_const_with_direction(level, direction);

    T500RS_DBG(t500rs,
               "Constant force: level=%d dir=%u -> %d (0x%02x)\n", level,
               direction, signed_level, (u8)signed_level);

    /* Send Report 0x03 (force level) */
    {
      struct t500rs_r03_const *r3 = (struct t500rs_r03_const *)buf;
      r3->id = 0x03;
      r3->code = 0x0e;
      r3->zero = 0x00;
      r3->level = signed_level;
      ret = t500rs_send_usb(t500rs, (u8 *)r3, sizeof(*r3));
    }
    if (ret) {
      hid_err(t500rs->hdev, "Failed to send Report 0x03: %d\n", ret);
      return ret;
    }
  }

  /* Send start command - Report 0x41
   * T500RS expects EffectID=0 for 0x41 commands as well.
   */
  {
    struct t500rs_r41_cmd *r41 = (struct t500rs_r41_cmd *)buf;
    r41->id = 0x41;
    r41->effect_id = 0x00;
    r41->command = 0x41;
    r41->arg = 0x01;
  }

  T500RS_DBG(t500rs,
             "Sending START command (EffectID=0) for effect %d\n", effect->id);
  return t500rs_send_usb(t500rs, buf, sizeof(struct t500rs_r41_cmd));
}

/* Stop effect */
static int t500rs_stop_effect(void *data,
                              const struct tmff2_effect_state *state) {
  struct t500rs_device_entry *t500rs = data;
  u8 *buf;
  int ret;

  if (!t500rs) {
    pr_err("t500rs_stop_effect: t500rs is NULL!\n");
    return -ENODEV;
  }

  buf = t500rs->send_buffer; /* Use DMA-safe buffer */
  if (!buf) {
    hid_err(t500rs->hdev, "Stop effect: send_buffer is NULL!\n");
    return -ENOMEM;
  }

  T500RS_DBG(t500rs, "Stop effect: id=%d, type=%d\n", state->effect.id,
             state->effect.type);

  /* For constant force: Windows-style STOP (0x41 00 00 01) */
  if (state->effect.type == FF_CONSTANT) {
    {
      struct t500rs_r41_cmd *r41 = (struct t500rs_r41_cmd *)buf;
      r41->id = 0x41;
      r41->effect_id = 0x00;
      r41->command = 0x00;
      r41->arg = 0x01;
    }
    return t500rs_send_usb(t500rs, buf, sizeof(struct t500rs_r41_cmd));
  }

  /* For other effect types, send stop command - Report 0x41
   * Use EffectID=0 to match device expectations for 0x41.
   */
  {
    struct t500rs_r41_cmd *r41 = (struct t500rs_r41_cmd *)buf;
    r41->id = 0x41;
    r41->effect_id = 0x00;
    r41->command = 0x00;
    r41->arg = 0x01;
  }

  ret = t500rs_send_usb(t500rs, buf, sizeof(struct t500rs_r41_cmd));
  T500RS_DBG(t500rs, "Stop effect (non-constant) returned: %d\n", ret);
  return ret;
}

/* Update effect - re-upload and update force level if constant force */
static int t500rs_update_effect(void *data,
                                const struct tmff2_effect_state *state) {
  struct t500rs_device_entry *t500rs = data;
  const struct ff_effect *effect = &state->effect;
  const struct ff_effect *old = &state->old;
  u8 *buf;

  if (!t500rs)
    return -ENODEV;

  buf = t500rs->send_buffer;
  if (!buf)
    return -ENOMEM;

  /* Do NOT re-upload here; Windows keeps the effect and we only update parameters */
  switch (effect->type) {
  case FF_CONSTANT: {
      int level = effect->u.constant.level;
      u16 direction = effect->direction;
      s8 signed_level = t500rs_scale_const_with_direction(level, direction);
      struct t500rs_r03_const *r3 = (struct t500rs_r03_const *)buf;
      r3->id = 0x03;
      r3->code = 0x0e;
      r3->zero = 0x00;
      r3->level = signed_level;
      return t500rs_send_usb(t500rs, (u8 *)r3, sizeof(*r3));
    }
  case FF_PERIODIC: {
      u8 mag = t500rs_scale_mag_u7(effect->u.periodic.magnitude);
      u16 period = effect->u.periodic.period;
      unsigned int idx = (unsigned int)effect->id;
      u8 param_sub = (u8)(0x0e + (0x1c * idx));

      if (period == 0)
        period = 100;
      {
        u32 freq_hz100 = 100000U / period;
        if (freq_hz100 < 1U) freq_hz100 = 1U;
        if (freq_hz100 > 65535U) freq_hz100 = 65535U;
        period = (u16)freq_hz100;
      }

      {
        struct t500rs_r04_periodic *p = (struct t500rs_r04_periodic *)buf;
        memset(p, 0, sizeof(*p));
        p->id = 0x04;
        p->code = param_sub;
        p->zero = 0x00;
        p->magnitude = mag;
        p->offset = 0x00;
        p->phase = 0x00;
        p->period = cpu_to_le16(period);
      }
      return t500rs_send_usb(t500rs, buf, sizeof(struct t500rs_r04_periodic));
    }
  case FF_RAMP: {
      int start_level = effect->u.ramp.start_level;
      u16 duration_ms = effect->replay.length;
      unsigned int idx = (unsigned int)effect->id;
      u8 param_sub = (u8)(0x0e + (0x1c * idx));
      u16 start_scaled = (abs(start_level) * 0xff) / 32767;
      struct t500rs_r04_ramp *rr = (struct t500rs_r04_ramp *)buf;
      memset(rr, 0, sizeof(*rr));
      rr->id = 0x04;
      rr->code = param_sub;
      rr->start = cpu_to_le16(start_scaled);
      rr->cur_val = cpu_to_le16(start_scaled);
      rr->duration = cpu_to_le16(duration_ms);
      rr->zero = 0x00;
      return t500rs_send_usb(t500rs, buf, sizeof(struct t500rs_r04_ramp));
    }
  case FF_SPRING:
  case FF_DAMPER:
  case FF_FRICTION:
  case FF_INERTIA: {
      unsigned int idx = (unsigned int)effect->id;
      u8 param_sub = (u8)(0x0e + (0x1c * idx));
      const struct ff_condition_effect *cond = &effect->u.condition[0];
      const struct ff_condition_effect *cond_old = &old->u.condition[0];
      u8 effect_gain, effect_gain_old;
      int right_strength, left_strength;
      int right_strength_old, left_strength_old;
      u8 rcoef, lcoef, rcoef_old, lcoef_old;
      u8 b9, b10, b9_old, b10_old;

      switch (effect->type) {
      case FF_SPRING:   effect_gain = spring_level; break;
      case FF_DAMPER:   effect_gain = damper_level; break;
      case FF_FRICTION: effect_gain = friction_level; break;
      case FF_INERTIA:  effect_gain = 100; break;
      default:          effect_gain = 100; break;
      }

      switch (old->type) {
      case FF_SPRING:   effect_gain_old = spring_level; break;
      case FF_DAMPER:   effect_gain_old = damper_level; break;
      case FF_FRICTION: effect_gain_old = friction_level; break;
      case FF_INERTIA:  effect_gain_old = 100; break;
      default:          effect_gain_old = 100; break;
      }

      right_strength = (cond->right_saturation * effect_gain) / 100;
      left_strength  = (cond->left_saturation  * effect_gain) / 100;
      right_strength_old = (cond_old->right_saturation * effect_gain_old) / 100;
      left_strength_old  = (cond_old->left_saturation  * effect_gain_old) / 100;

      right_strength     = (right_strength     * 127) / 65535;
      left_strength      = (left_strength      * 127) / 65535;
      right_strength_old = (right_strength_old * 127) / 65535;
      left_strength_old  = (left_strength_old  * 127) / 65535;

      rcoef     = (u8)right_strength;
      lcoef     = (u8)left_strength;
      rcoef_old = (u8)right_strength_old;
      lcoef_old = (u8)left_strength_old;

      b9      = (effect->type == FF_SPRING) ? 0x54 : 0x64;
      b10     = (effect->type == FF_SPRING) ? 0x54 : 0x64;
      b9_old  = (old->type == FF_SPRING) ? 0x54 : 0x64;
      b10_old = (old->type == FF_SPRING) ? 0x54 : 0x64;

      /*
       * Rationale: ACC (and similar) may spam condition updates at low speed with
       * the exact same parameters. Re-sending 0x05 (sub=0x0e) at high cadence
       * makes T500RS micro-pulse/rumble. Also, sending 0x05 (sub=0x1c) on update
       * is unnecessary when deadband/center haven't changed and can exacerbate
       * the issue. Therefore we derive device coefficients from state->effect and
       * state->old and only send 0x05 (sub=0x0e) when they differ, skipping
       * 0x05 (sub=0x1c) entirely on updates.
       */
      if (rcoef == rcoef_old &&
          lcoef == lcoef_old &&
          b9    == b9_old &&
          b10   == b10_old)
        return 0;

      memset(buf, 0, 11);
      buf[0] = 0x05;
      buf[1] = param_sub;
      buf[2] = 0x00;
      buf[3] = rcoef;
      buf[4] = lcoef;
      buf[5] = buf[6] = buf[7] = buf[8] = 0x00;
      buf[9]  = b9;
      buf[10] = b10;
      if (t500rs_send_usb(t500rs, buf, 11))
        return -EIO;

      /* Skip 0x05 (sub=0x1c) on updates by design; see rationale above. */
      return 0;
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

  /* Wine compatibility: Some games (e.g., LFS under Wine) set autocenter to 100%%
   * at startup and never release it. That leaves a permanent strong centering force
   * which masks/overpowers other forces. To avoid this, ignore requests that try to
   * set maximum autocenter (100%%). Disabling (0) is still honored; lower values are
   * allowed. */
  if (autocenter_percent >= 100) {
    hid_warn(t500rs->hdev,
             "Ignoring 100%% autocenter request (Wine/LFS compatibility)");
    return 0;
  }

  buf = t500rs->send_buffer;
  if (!buf)
    return -ENOMEM;

  if (autocenter == 0) {
    /* Disable autocenter: Report 0x40 0x04 0x00 */
    buf[0] = 0x40;
    buf[1] = 0x04;
    buf[2] = 0x00; /* Disable */
    buf[3] = 0x00;
    ret = t500rs_send_usb(t500rs, buf, 4);
    if (ret)
      return ret;
  } else {
    /* Enable autocenter: Report 0x40 0x04 0x01 */
    buf[0] = 0x40;
    buf[1] = 0x04;
    buf[2] = 0x01; /* Enable */
    buf[3] = 0x00;
    ret = t500rs_send_usb(t500rs, buf, 4);
    if (ret)
      return ret;

    /* Set autocenter strength: Report 0x40 0x03 [value] */
    buf[0] = 0x40;
    buf[1] = 0x03;
    buf[2] = autocenter_percent; /* 0-100 percentage */
    buf[3] = 0x00;
    ret = t500rs_send_usb(t500rs, buf, 4);
    if (ret)
      return ret;
  }

  /* Apply settings: Report 0x42 0x05 */
  buf[0] = 0x42;
  buf[1] = 0x05;
  ret = t500rs_send_usb(t500rs, buf, 2);
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

  if (!t500rs)
    return -ENODEV;

  /* Clamp range to maximum value only
   * Allow testing values below 270° to find hardware minimum */
  if (range > 1080) {
    hid_warn(t500rs->hdev, "Range %u too large, clamping to 1080\n", range);
    range = 1080;
  }

  /* Use DMA-safe preallocated buffer */
  buf = t500rs->send_buffer;
  if (!buf)
    return -ENOMEM;

  T500RS_DBG(t500rs, "Setting wheel range to %u degrees\n", range);

  /* Device expects LITTLE-ENDIAN and value = range * 60. */
  range_value = range * 60;

  /* Send Report 0x40 0x11 [value_lo] [value_hi] to set range */
  buf[0] = 0x40;
  buf[1] = 0x11;
  buf[2] = range_value & 0xFF;        /* Low byte first (little-endian) */
  buf[3] = (range_value >> 8) & 0xFF; /* High byte second */

  ret = t500rs_send_usb(t500rs, buf, 4);
  if (ret) {
    hid_err(t500rs->hdev, "Failed to send range command: %d\n", ret);
    return ret;
  }

  /* Store current range */
  t500rs->current_range = range;

  /* Apply settings with Report 0x42 0x05 */
  buf[0] = 0x42;
  buf[1] = 0x05;
  ret = t500rs_send_usb(t500rs, buf, 2);
  if (ret) {
    hid_err(t500rs->hdev, "Failed to apply range settings: %d\n", ret);
    return ret;
  }

  T500RS_DBG(t500rs,
             "Range set to %u degrees (final value=0x%04x)\n", range,
             range_value);

  return 0;
}

/* Initialize T500RS device */
static int t500rs_wheel_init(struct tmff2_device_entry *tmff2, int open_mode) {
  struct t500rs_device_entry *t500rs;
  struct usb_host_endpoint *ep;
  u8 *init_buf; /* Will use send_buffer for DMA-safe transfers */
  int ret;

  /* Validate input parameters */
  if (!tmff2 || !tmff2->hdev || !tmff2->input_dev) {

    pr_err("t500rs: Invalid tmff2 structure\n");
    return -EINVAL;
  }

  hid_dbg(tmff2->hdev, "T500RS: Initializing USB INTERRUPT mode\n");

  /* Allocate device data */
  t500rs = kzalloc(sizeof(*t500rs), GFP_KERNEL);
  if (!t500rs) {
    ret = -ENOMEM;
    goto err_alloc;
  }

  t500rs->hdev = tmff2->hdev;
  t500rs->input_dev = tmff2->input_dev;

  /* Get USB device */
  if (!t500rs->hdev->dev.parent) {
    hid_err(t500rs->hdev, "No parent device\n");
    ret = -ENODEV;
    goto err_endpoint;
  }

  t500rs->usbif = to_usb_interface(t500rs->hdev->dev.parent);
  if (!t500rs->usbif) {
    hid_err(t500rs->hdev, "Failed to get USB interface\n");
    ret = -ENODEV;
    goto err_endpoint;
  }

  t500rs->usbdev = interface_to_usbdev(t500rs->usbif);
  if (!t500rs->usbdev) {
    hid_err(t500rs->hdev, "Failed to get USB device\n");
    ret = -ENODEV;
    goto err_endpoint;
  }

  /* Find INTERRUPT OUT endpoint (should be endpoint 1) */
  if (t500rs->usbif->cur_altsetting->desc.bNumEndpoints < 2) {
    hid_err(t500rs->hdev, "Not enough USB endpoints\n");
    ret = -ENODEV;
    goto err_endpoint;
  }

  ep = &t500rs->usbif->cur_altsetting->endpoint[1];
  t500rs->ep_out = ep->desc.bEndpointAddress;

  T500RS_DBG(t500rs, "Found INTERRUPT OUT endpoint: 0x%02x\n", t500rs->ep_out);

  /* Allocate send buffer */
  t500rs->buffer_length = T500RS_BUFFER_LENGTH;
  t500rs->send_buffer = kzalloc(t500rs->buffer_length, GFP_KERNEL);
  if (!t500rs->send_buffer) {
    ret = -ENOMEM;
    goto err_buffer;
  }

  /* Initialize current range to default (900°) */
  t500rs->current_range = 900;

  /* Store device data in tmff2 */
  tmff2->data = t500rs;

  /* Use send_buffer for all USB transfers (DMA-safe) */
  init_buf = t500rs->send_buffer;

  T500RS_DBG(t500rs, "Sending initialization sequence...\n");

  /* Report 0x42 - Apply/init (2 bytes)
   * Minimal "initialize/apply" command observed as 0x42 0x05 in Windows
   * captures. Send once at startup to bring the base into a known state
   * before FFB uploads.
   */
  memset(init_buf, 0, 2);
  init_buf[0] = 0x42;
  init_buf[1] = 0x05;
  ret = t500rs_send_usb(t500rs, init_buf, 2);
  if (ret) {
    hid_warn(t500rs->hdev, "Init command 1 (0x42 0x05) failed: %d\n", ret);
  }

  /* Report 0x40 - Enable FFB (4 bytes)
   * Magic value seen in captures that enables FFB on the base.
   */
  memset(init_buf, 0, 4);
  init_buf[0] = 0x40;
  init_buf[1] = 0x11;
  init_buf[2] = 0x42;
  init_buf[3] = 0x7b;
  ret = t500rs_send_usb(t500rs, init_buf, 4);
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
  ret = t500rs_send_usb(t500rs, init_buf, 4);
  if (ret) {
    hid_warn(t500rs->hdev, "Init command 3 (0x40 config) failed: %d\n", ret);
  }

  /* Report 0x43 - Set global gain (2 bytes)
   * Start at maximum device gain; the FFB gain callback will adjust later.
   */
  memset(init_buf, 0, 2);
  init_buf[0] = 0x43;
  init_buf[1] = 0xFF;
  ret = t500rs_send_usb(t500rs, init_buf, 2);
  if (ret) {
    hid_warn(t500rs->hdev, "Init command 4 (0x43) failed: %d\n", ret);
  }

  /* The remaining initialization (0x05 spring zeroing and 0x41 STOP for
   * autocenter ID 15) is handled below.
   */

  /* Report 0x05 - Set deadband and center */
  memset(init_buf, 0, 11);
  init_buf[0] = 0x05;
  init_buf[1] = 0x1c;
  init_buf[2] = 0x00;
  init_buf[3] = 0x00;  /* Deadband = 0 */
  init_buf[4] = 0x00;  /* Center = 0 */
  init_buf[9] = 0x00;  /* Right saturation = 0 */
  init_buf[10] = 0x00; /* Left saturation = 0 */
  ret = t500rs_send_usb(t500rs, init_buf, 11);
  if (ret) {
    hid_warn(t500rs->hdev, "Disable autocenter (0x05 0x1c) failed: %d\n", ret);
  }

  /* Stop autocenter effect (effect ID 15) */
  {
    struct t500rs_r41_cmd *r41 = (struct t500rs_r41_cmd *)init_buf;
    r41->id = 0x41;
    r41->effect_id = 15; /* Autocenter effect ID */
    r41->command = 0x00; /* STOP */
    r41->arg = 0x01;
  }
  ret = t500rs_send_usb(t500rs, init_buf, sizeof(struct t500rs_r41_cmd));
  if (ret) {
    hid_warn(t500rs->hdev, "Stop autocenter effect failed: %d\n", ret);
  } else {
    T500RS_DBG(t500rs, "Autocenter fully disabled\n");
  }

  hid_info(t500rs->hdev,
           "T500RS initialized successfully (USB INTERRUPT mode)\n");
  T500RS_DBG(t500rs, "Endpoint: 0x%02x, Buffer: %zu bytes\n", t500rs->ep_out,
             t500rs->buffer_length);

  /* Advertise capabilities now that init succeeded */
  tmff2->params = t500rs_params;
  tmff2->max_effects = T500RS_MAX_EFFECTS;
  memcpy(tmff2->supported_effects, t500rs_effects, sizeof(t500rs_effects));

  return 0;

err_buffer:
  kfree(t500rs->send_buffer);
err_endpoint:
  kfree(t500rs);
err_alloc:
  return ret;
}

/* Cleanup T500RS device */
static int t500rs_wheel_destroy(void *data) {
  struct t500rs_device_entry *t500rs = data;

  if (!t500rs)
    return 0;

  T500RS_DBG(t500rs, "T500RS: Cleaning up\n");

  /* Free resources */
  kfree(t500rs->send_buffer);
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
