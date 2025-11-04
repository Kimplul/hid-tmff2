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

/* Build-time injected version (from Makefile/Kbuild) */
#ifndef T500RS_DRIVER_VERSION
#ifdef T500RS_DRIVER_VERSION_STR
#define __T500_STR_HELPER(x) #x
#define __T500_STR(x) __T500_STR_HELPER(x)
#define T500RS_DRIVER_VERSION __T500_STR(T500RS_DRIVER_VERSION_STR)
#else
#define T500RS_DRIVER_VERSION "0.1-local"
#endif
#endif

/* T500RS Constants */
#define T500RS_MAX_EFFECTS 16
#define T500RS_BUFFER_LENGTH 32 /* USB endpoint max packet size */
#define T500RS_EP_OUT 0x01      /* INTERRUPT OUT endpoint */

/* USB timeout */
#define T500RS_USB_TIMEOUT 1000 /* 1 second */

/* Gain scaling */
#define GAIN_MAX 65535

/* Logging verbosity (0=minimal, 1=verbose) */
int t500rs_log_level;
module_param(t500rs_log_level, int, 0644);
MODULE_PARM_DESC(t500rs_log_level,
                 "Log level: 0=minimal, 1=verbose, 2=usb-dump TX, 3=usb-dump "
                 "UNKNOWN only (TX+RX)");

/* Helper: classify whether a TX buffer is a known/managed report
 * Known first bytes (report IDs / opcodes) we intentionally emit:
 *   0x01,0x02,0x03,0x04,0x05,0x40,0x41,0x42,0x43,0x0a
 */
static inline int t500rs_is_known_tx(const unsigned char *data, size_t len) {
  unsigned char r, s;
  if (!data || !len)
    return 1;
  r = data[0];
  s = (len > 1) ? data[1] : 0;
  switch (r) {
  case 0x01:
    return len == 15; /* main effect upload */
  case 0x02:
    return len == 9 && s == 0x1c; /* envelope */
  case 0x03:
    return len == 4 && s == 0x0e &&
           ((len > 2) ? data[2] == 0x00 : 0); /* const force level */
  case 0x04:
    return (s == 0x0e) && (len == 8 || len == 9); /* periodic/ramp params */
  case 0x05:
    return (len == 11) && (s == 0x0e || s == 0x1c) &&
           ((len > 2) ? data[2] == 0x00 : 0);
  case 0x40:
    return (len == 4) && (s == 0x03 || s == 0x04 || s == 0x08 || s == 0x11);
  case 0x41:
    return len == 4; /* play/stop/clear */
  case 0x42:
    return ((len == 2) && (s == 0x05 || s == 0x04)) ||
           ((len == 15) && s == 0x01);
  case 0x43:
    return len == 2; /* global gain */
  case 0x0a:
    return (len == 15) && s == 0x04; /* config */
  default:
    return 0;
  }
}

/* Debug logging helper (requires local variable named 't500rs') */
#define T500RS_DBG(fmt, ...)                                                   \
  do {                                                                         \
    /* Suppress normal debug at level 3 (unknown-only mode) */                 \
    if (t500rs_log_level > 0 && t500rs_log_level < 3)                          \
      hid_info(t500rs->hdev, fmt, ##__VA_ARGS__);                              \
  } while (0)

/* T500RS device data */
struct t500rs_device_entry {
  struct hid_device *hdev;
  struct input_dev *input_dev;
  struct usb_device *usbdev;
  struct usb_interface *usbif;

  int ep_out; /* INTERRUPT OUT endpoint address */

  u8 *send_buffer;
  size_t buffer_length;

  /* Per-effect levels (0-100) from base driver's sysfs callbacks */
  u8 spring_gain;
  u8 damper_gain;
  u8 friction_gain;
  u8 inertia_gain;

  /* Current wheel range for smooth transitions */
  u16 current_range; /* Current rotation range in degrees */
};

/* Supported parameters */
static const unsigned long t500rs_params =
    PARAM_SPRING_LEVEL | PARAM_DAMPER_LEVEL | PARAM_FRICTION_LEVEL |
    PARAM_GAIN | PARAM_RANGE;

/* Supported effects */
static const signed short t500rs_effects[] = {
    FF_CONSTANT, FF_SPRING, FF_DAMPER, FF_FRICTION,   FF_INERTIA,
    FF_PERIODIC, FF_RAMP,   FF_GAIN,   FF_AUTOCENTER, -1};

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
  if (t500rs_log_level > 1) {
    char hex[3 * T500RS_BUFFER_LENGTH + 1];
    size_t i, off = 0;
    for (i = 0; i < len && off + 3 < sizeof(hex); i++)
      off += scnprintf(hex + off, sizeof(hex) - off, "%02x ", data[i]);
    if (t500rs_log_level == 2) {
      hid_info(t500rs->hdev, "USB TX [%zu]: %s\n", len, hex);
    } else if (t500rs_log_level >= 3) {
      if (!t500rs_is_known_tx(data, len))
        hid_info(t500rs->hdev, "USB TX UNKNOWN [%zu]: %s\n", len, hex);
    }
  }

  ret = usb_interrupt_msg(t500rs->usbdev,
                          usb_sndintpipe(t500rs->usbdev, t500rs->ep_out),
                          (void *)data, len, &transferred, T500RS_USB_TIMEOUT);
  if (ret < 0)
    return ret;
  return (transferred == len) ? 0 : -EIO;
}

/* Upload constant force effect */
static int t500rs_upload_constant(struct t500rs_device_entry *t500rs,
                                  const struct tmff2_effect_state *state) {
  const struct ff_effect *effect = &state->effect;
  u8 *buf = t500rs->send_buffer; /* Use DMA-safe buffer */
  int ret;
  int level = effect->u.constant.level;

  /* Note: Gain is applied in play_effect, not here */

  T500RS_DBG("Upload constant: id=%d, level=%d\n", effect->id, level);

  /* NO DEADZONE - Send all forces exactly as requested, matching Windows
   * behavior */

  /* Report 0x02 - Envelope (attack/fade) */
  memset(buf, 0, 15);
  buf[0] = 0x02;
  buf[1] = 0x1c; /* Subtype 0x1c */
  buf[2] = 0x00;
  T500RS_DBG("Sending Report 0x02 (envelope)...\n");
  ret = t500rs_send_usb(t500rs, buf, 9);
  if (ret) {
    hid_err(t500rs->hdev, "Failed to send Report 0x02: %d\n", ret);
    return ret;
  }

  /* Report 0x01 - Main effect upload - MATCH WINDOWS DRIVER EXACTLY! */
  memset(buf, 0, 15);
  buf[0] = 0x01;
  buf[1] = 0x00; /* Effect ID 0 (Windows uses 0, not effect->id) */
  buf[2] = 0x00; /* Constant force type */
  buf[3] = 0x40;
  buf[4] = 0xff; /* Windows uses 0xff (was 0x69) */
  buf[5] = 0xff; /* Windows uses 0xff (was 0x23) */
  buf[6] = 0x00;
  buf[7] = 0xff;
  buf[8] = 0xff;
  buf[9] = 0x0e; /* Parameter subtype reference */
  buf[10] = 0x00;
  buf[11] = 0x1c; /* Envelope subtype reference */
  buf[12] = 0x00;
  buf[13] = 0x00;
  buf[14] = 0x00;
  T500RS_DBG("Sending Report 0x01 (duration/control)...\n");
  ret = t500rs_send_usb(t500rs, buf, 15);
  if (ret) {
    hid_err(t500rs->hdev, "Failed to send Report 0x01: %d\n", ret);
    return ret;
  }

  T500RS_DBG("Constant effect %d uploaded (simple sequence)\n", effect->id);

  /* CRITICAL FIX : Always update the force level when uploading.
   * Game calls stop/upload/play in rapid succession, so the timer might be
   * stopped when upload is called. We update the force level here so that
   * when play_effect starts the timer, it will use the correct force value.
   *
   * MATCH WINDOWS: Send forces exactly as requested - no amplification!
   * Windows sends weak forces (4-27 out of 127) and they work fine.
   */
  {
    s8 signed_level;
    s32 scaled;

    /* Simple linear scaling from -32767..32767 to -127..127 */
    scaled = (level * 127) / 32767;
    signed_level = (s8)scaled;

    T500RS_DBG("Upload constant: id=%d, level=%d -> %d (0x%02x)\n", effect->id,
               level, signed_level, (u8)signed_level);
  }

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

  /* Determine effect type and select appropriate gain */
  switch (effect->type) {
  case FF_SPRING:
    effect_type = 0x40;
    effect_gain = t500rs->spring_gain;
    break;
  case FF_DAMPER:
    effect_type = 0x41;
    effect_gain = t500rs->damper_gain;
    break;
  case FF_FRICTION:
    effect_type = 0x41;
    effect_gain = t500rs->friction_gain;
    break;
  case FF_INERTIA:
    effect_type = 0x41;
    effect_gain = t500rs->inertia_gain;
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

  T500RS_DBG("Upload condition: id=%d, type=0x%02x, gain=%u%%, R=%d, L=%d\n",
             effect->id, effect_type, effect_gain, right_strength,
             left_strength);

  /* Report 0x05 - Condition parameters (coefficients) */
  memset(buf, 0, 15);
  buf[0] = 0x05;
  buf[1] = 0x0e;
  buf[2] = 0x00;
  buf[3] = (u8)right_strength;
  buf[4] = (u8)left_strength;
  buf[5] = 0x00;
  buf[6] = 0x00;
  buf[7] = 0x00;
  buf[8] = 0x00;
  buf[9] = (effect->type == FF_SPRING) ? 0x54 : 0x64;
  buf[10] = (effect->type == FF_SPRING) ? 0x54 : 0x64;
  ret = t500rs_send_usb(t500rs, buf, 11);
  if (ret)
    return ret;

  /* Report 0x05 - Condition parameters (deadband/center) */
  memset(buf, 0, 15);
  buf[0] = 0x05;
  buf[1] = 0x1c;
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

  /* Report 0x01 - Main effect upload */
  memset(buf, 0, 15);
  buf[0] = 0x01;
  buf[1] = effect->id;
  buf[2] = effect_type;
  buf[3] = 0x40;
  buf[4] = 0x17;
  buf[5] = 0x25;
  buf[6] = 0x00;
  buf[7] = 0xff;
  buf[8] = 0xff;
  buf[9] = 0x0e;
  buf[10] = 0x00;
  buf[11] = 0x1c;
  buf[12] = 0x00;
  buf[13] = 0x00;
  buf[14] = 0x00;
  ret = t500rs_send_usb(t500rs, buf, 15);
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

  /* Use game-provided magnitude directly; base gain is applied via set_gain()
   */

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

  /* Magnitude - scale to 0-127 */
  mag = (abs(magnitude) * 127) / 32767;

  /* Period (frequency) - default to 100ms = 10 Hz if not set */
  if (period == 0) {
    period = 100;
  }

  T500RS_DBG("Upload %s: id=%d, magnitude=%d (0x%02x), period=%dms\n",
             type_name, effect->id, magnitude, mag, period);

  /* Report 0x02 - Envelope */
  memset(buf, 0, 15);
  buf[0] = 0x02;
  buf[1] = 0x1c;
  buf[2] = 0x00;
  ret = t500rs_send_usb(t500rs, buf, 9);
  if (ret) {
    hid_err(t500rs->hdev, "Failed to send Report 0x02: %d\n", ret);
    return ret;
  }

  /* Report 0x01 - Main effect upload for periodic (set waveform/type) */
  memset(buf, 0, 15);
  buf[0] = 0x01;
  buf[1] = 0x00;        /* Effect ID 0 */
  buf[2] = effect_type; /* Waveform type (0x20..0x24) */
  buf[3] = 0x40;
  buf[4] = 0xff;
  buf[5] = 0xff;
  buf[6] = 0x00;
  buf[7] = 0xff;
  buf[8] = 0xff;
  buf[9] = 0x0e; /* Parameter subtype reference */
  buf[10] = 0x00;
  buf[11] = 0x1c; /* Envelope subtype reference */
  buf[12] = 0x00;
  buf[13] = 0x00;
  buf[14] = 0x00;
  ret = t500rs_send_usb(t500rs, buf, 15);
  if (ret) {
    hid_err(t500rs->hdev, "Failed to send Report 0x01 (periodic main): %d\n",
            ret);
    return ret;
  }

  /* Report 0x04 - Periodic parameters */
  memset(buf, 0, 15);
  buf[0] = 0x04;
  buf[1] = 0x0e;
  buf[2] = 0x00;
  buf[3] = mag;                  /* Magnitude */
  buf[4] = 0x00;                 /* Offset */
  buf[5] = 0x00;                 /* Phase */
  buf[6] = period & 0xff;        /* Period low byte */
  buf[7] = (period >> 8) & 0xff; /* Period high byte */
  ret = t500rs_send_usb(t500rs, buf, 8);
  if (ret) {
    hid_err(t500rs->hdev, "Failed to send Report 0x04: %d\n", ret);
    return ret;
  }

  /* Report 0x01 - Main effect upload */
  memset(buf, 0, 15);
  buf[0] = 0x01;
  buf[1] = effect->id;
  buf[2] = effect_type; /* Waveform type */
  buf[3] = 0x40;
  buf[4] = 0x17;
  buf[5] = 0x25;
  buf[6] = 0x00;
  buf[7] = 0xff;
  buf[8] = 0xff;
  buf[9] = 0x0e;
  buf[10] = 0x00;
  buf[11] = 0x1c;
  buf[12] = 0x00;
  buf[13] = 0x00;
  buf[14] = 0x00;
  ret = t500rs_send_usb(t500rs, buf, 15);
  if (ret) {
    hid_err(t500rs->hdev, "Failed to send Report 0x01: %d\n", ret);
    return ret;
  }

  T500RS_DBG("%s effect %d uploaded\n", type_name, effect->id);
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

  /* Scale to 0-255 */
  start_scaled = (abs(start_level) * 0xff) / 32767;

  T500RS_DBG("Upload ramp: id=%d, start=%d, end=%d, duration=%dms\n",
             effect->id, start_level, end_level, duration_ms);

  /* Report 0x02 - Envelope */
  memset(buf, 0, 15);
  buf[0] = 0x02;
  buf[1] = 0x1c;
  buf[2] = 0x00;
  ret = t500rs_send_usb(t500rs, buf, 9);
  if (ret) {
    hid_err(t500rs->hdev, "Failed to send Report 0x02: %d\n", ret);
    return ret;
  }

  /* Report 0x04 - Ramp parameters */
  /* NOTE: T500RS doesn't support native ramp - just holds start level */
  memset(buf, 0, 15);
  buf[0] = 0x04;
  buf[1] = 0x0e;
  buf[2] = start_scaled & 0xff;        /* Start level low byte */
  buf[3] = (start_scaled >> 8) & 0xff; /* Start level high byte */
  buf[4] = start_scaled & 0xff;        /* Current level (same as start) */
  buf[5] = (start_scaled >> 8) & 0xff; /* Current level high byte */
  buf[6] = duration_ms & 0xff;         /* Duration low byte */
  buf[7] = (duration_ms >> 8) & 0xff;  /* Duration high byte */
  buf[8] = 0x00;
  ret = t500rs_send_usb(t500rs, buf, 9);
  if (ret) {
    hid_err(t500rs->hdev, "Failed to send Report 0x04: %d\n", ret);
    return ret;
  }

  /* Report 0x01 - Main effect upload */
  memset(buf, 0, 15);
  buf[0] = 0x01;
  buf[1] = effect->id;
  buf[2] = 0x24; /* Ramp type (0x24 = sawtooth down / ramp) */
  buf[3] = 0x40;
  buf[4] = duration_ms & 0xff;        /* Duration low byte */
  buf[5] = (duration_ms >> 8) & 0xff; /* Duration high byte */
  buf[6] = 0x00;
  buf[7] = 0xff;
  buf[8] = 0xff;
  buf[9] = 0x0e;
  buf[10] = 0x00;
  buf[11] = 0x1c;
  buf[12] = 0x00;
  buf[13] = 0x00;
  buf[14] = 0x00;
  ret = t500rs_send_usb(t500rs, buf, 15);
  if (ret) {
    hid_err(t500rs->hdev, "Failed to send Report 0x01: %d\n", ret);
    return ret;
  }

  T500RS_DBG("Ramp effect %d uploaded (simple mode)\n", effect->id);
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

  T500RS_DBG("Play effect: id=%d, type=0x%02x (FF_CONSTANT=0x%02x)\n",
             effect->id, effect->type, FF_CONSTANT);

  /* For constant force: send one level update (0x03) then START (0x41) */
  if (effect->type == FF_CONSTANT) {
    int level = effect->u.constant.level;
    s8 signed_level;
    s32 scaled;

    /* Simple linear scaling from -32767..32767 to -127..127 */
    scaled = (level * 127) / 32767;
    signed_level = (s8)scaled;

    T500RS_DBG("Constant force: level=%d -> %d (0x%02x)\n", level, signed_level,
               (u8)signed_level);

    /* Send Report 0x03 (force level) */
    buf[0] = 0x03;
    buf[1] = 0x0e;
    buf[2] = 0x00;
    buf[3] = (u8)signed_level;
    ret = t500rs_send_usb(t500rs, buf, 4);
    if (ret) {
      hid_err(t500rs->hdev, "Failed to send Report 0x03: %d\n", ret);
      return ret;
    }

    /* Send Report 0x41 START */
    buf[0] = 0x41;
    buf[1] = 0x00; /* Effect ID 0 */
    buf[2] = 0x41; /* START */
    buf[3] = 0x01;
    return t500rs_send_usb(t500rs, buf, 4);
  }

  /* For other effect types, send start command - Report 0x41
   * T500RS expects EffectID=0 for 0x41 commands as well.
   */
  buf[0] = 0x41;
  buf[1] = 0x00; /* Effect ID 0 to match device expectations */
  buf[2] = 0x41; /* START command */
  buf[3] = 0x01;

  T500RS_DBG("Sending START command (EffectID=0) for effect %d\n", effect->id);
  return t500rs_send_usb(t500rs, buf, 4);
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

  T500RS_DBG("Stop effect: id=%d, type=%d\n", state->effect.id,
             state->effect.type);

  /* For constant force: Windows-style STOP (0x41 00 00 01) */
  if (state->effect.type == FF_CONSTANT) {
    buf[0] = 0x41;
    buf[1] = 0x00;
    buf[2] = 0x00; /* STOP */
    buf[3] = 0x01;
    return t500rs_send_usb(t500rs, buf, 4);
  }

  /* For other effect types, send stop command - Report 0x41
   * Use EffectID=0 to match device expectations for 0x41.
   */
  buf[0] = 0x41;
  buf[1] = 0x00; /* Effect ID 0 */
  buf[2] = 0x00; /* STOP command */
  buf[3] = 0x01;

  ret = t500rs_send_usb(t500rs, buf, 4);
  T500RS_DBG("Stop effect (non-constant) returned: %d\n", ret);
  return ret;
}

/* Update effect - re-upload and update force level if constant force */
static int t500rs_update_effect(void *data,
                                const struct tmff2_effect_state *state) {
  struct t500rs_device_entry *t500rs = data;
  const struct ff_effect *effect = &state->effect;

  if (!t500rs)
    return -ENODEV;

  /* Do NOT re-upload here; Windows keeps the effect and only updates force
   * level */
  /* This avoids redundant USB traffic and state churn */

  /* Update constant force: send single 0x03 with new level */
  if (effect->type == FF_CONSTANT) {
    int level = effect->u.constant.level;
    s8 signed_level;
    s32 scaled;
    u8 *buf3;

    buf3 = t500rs->send_buffer;
    if (!buf3)
      return -ENOMEM;

    scaled = (level * 127) / 32767;
    signed_level = (s8)scaled;

    buf3[0] = 0x03;
    buf3[1] = 0x0e;
    buf3[2] = 0x00;
    buf3[3] = (u8)signed_level;
    return t500rs_send_usb(t500rs, buf3, 4);
  }

  return 0;
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
  u16 range_value, current_value, target_value;
  int step, i, num_steps;

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

  T500RS_DBG("Setting wheel range to %u degrees\n", range);

  /* Based on testing with actual hardware:
   * The T500RS uses Report 0x40 0x11 [value_lo] [value_hi] to set rotation
   * range
   *
   * Hardware testing showed:
   * - Byte order is LITTLE-ENDIAN (low byte first)
   * - Formula: value = range * 60
   * - Smooth transitions prevent hard mechanical ticking
   *
   * To smooth the transition, we send multiple intermediate values
   * when the range change is large.
   */
  target_value = range * 60;
  current_value = t500rs->current_range * 60;

  /* Calculate number of steps based on the change magnitude
   * Larger changes need more steps for smooth transition
   * Use many small steps to prevent hard mechanical ticking */
  range_value = (target_value > current_value) ? (target_value - current_value)
                                               : (current_value - target_value);

  /* Use approximately 1 step per 500 units of change, minimum 1, maximum 50 */
  num_steps = range_value / 500;
  if (num_steps < 1)
    num_steps = 1;
  if (num_steps > 50)
    num_steps = 50;

  step = (target_value - current_value) / num_steps;

  /* Send gradual range changes */
  for (i = 1; i <= num_steps; i++) {
    if (i == num_steps) {
      range_value = target_value; /* Ensure we hit exact target */
    } else {
      range_value = current_value + (step * i);
    }

    /* Send Report 0x40 0x11 [value_lo] [value_hi] to set range
     * NOTE: This uses LITTLE-ENDIAN byte order (low byte first)! */
    buf[0] = 0x40;
    buf[1] = 0x11;
    buf[2] = range_value & 0xFF;        /* Low byte first (little-endian) */
    buf[3] = (range_value >> 8) & 0xFF; /* High byte second */

    ret = t500rs_send_usb(t500rs, buf, 4);
    if (ret) {
      hid_err(t500rs->hdev, "Failed to send range command: %d\n", ret);
      return ret;
    }

    T500RS_DBG("Range step %d/%d: value=0x%04x\n", i, num_steps, range_value);

    /* Very small delay between steps for smooth transition
     * (only if not the last step) */
    /* Avoid explicit delays; USB stack handles pacing. */
  }

  /* Store current range for next transition */
  t500rs->current_range = range;

  /* Apply settings with Report 0x42 0x05 */
  buf[0] = 0x42;
  buf[1] = 0x05;
  ret = t500rs_send_usb(t500rs, buf, 2);
  if (ret) {
    hid_err(t500rs->hdev, "Failed to apply range settings: %d\n", ret);
    return ret;
  }

  T500RS_DBG("Range set to %u degrees (final value=0x%04x)\n", range,
             target_value);

  return 0;
}

/* Set spring level (0-100) - called from sysfs */
static int t500rs_set_spring_level(void *data, u8 level) {
  struct t500rs_device_entry *t500rs = data;

  if (!t500rs)
    return -ENODEV;

  /* Clamp to valid range */
  if (level > 100)
    level = 100;

  /* Update the spring gain setting */
  t500rs->spring_gain = level;

  T500RS_DBG("Spring level set to %u%%\n", level);

  /* Note: The new level will be applied when the next spring effect is
   * uploaded/updated */
  return 0;
}

/* Set damper level (0-100) - called from sysfs */
static int t500rs_set_damper_level(void *data, u8 level) {
  struct t500rs_device_entry *t500rs = data;

  if (!t500rs)
    return -ENODEV;

  /* Clamp to valid range */
  if (level > 100)
    level = 100;

  /* Update the damper gain setting */
  t500rs->damper_gain = level;

  T500RS_DBG("Damper level set to %u%%\n", level);

  /* Note: The new level will be applied when the next damper effect is
   * uploaded/updated */
  return 0;
}

/* Set friction level (0-100) - called from sysfs */
static int t500rs_set_friction_level(void *data, u8 level) {
  struct t500rs_device_entry *t500rs = data;

  if (!t500rs)
    return -ENODEV;

  /* Clamp to valid range */
  if (level > 100)
    level = 100;

  /* Update the friction gain setting */
  t500rs->friction_gain = level;

  T500RS_DBG("Friction level set to %u%%\n", level);

  /* Note: The new level will be applied when the next friction effect is
   * uploaded/updated */
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
  hid_info(tmff2->hdev, "T500RS driver version %s\n", T500RS_DRIVER_VERSION);

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

  T500RS_DBG("Found INTERRUPT OUT endpoint: 0x%02x\n", t500rs->ep_out);

  /* Allocate send buffer */
  t500rs->buffer_length = T500RS_BUFFER_LENGTH;
  t500rs->send_buffer = kzalloc(t500rs->buffer_length, GFP_KERNEL);
  if (!t500rs->send_buffer) {
    ret = -ENOMEM;
    goto err_buffer;
  }

  /* Initialize per-effect levels to 100% (0-100 range) */
  t500rs->spring_gain = 100;
  t500rs->damper_gain = 100;
  t500rs->friction_gain = 100;
  t500rs->inertia_gain = 100;

  /* Initialize current range to default (900°) for smooth transitions */
  t500rs->current_range = 900;

  /* Store device data in tmff2 */
  tmff2->data = t500rs;

  /* Use send_buffer for all USB transfers (DMA-safe) */
  init_buf = t500rs->send_buffer;

  T500RS_DBG("Sending initialization sequence...\n");

  /* Report 0x42 - Init (15 bytes) */
  memset(init_buf, 0, 15);
  init_buf[0] = 0x42;
  init_buf[1] = 0x01;
  ret = t500rs_send_usb(t500rs, init_buf, 15);
  if (ret) {
    hid_warn(t500rs->hdev, "Init command 1 (0x42) failed: %d\n", ret);
  }

  /* Report 0x0a - Config 1 (15 bytes) */
  memset(init_buf, 0, 15);
  init_buf[0] = 0x0a;
  init_buf[1] = 0x04;
  init_buf[2] = 0x90;
  init_buf[3] = 0x03;
  ret = t500rs_send_usb(t500rs, init_buf, 15);
  if (ret) {
    hid_warn(t500rs->hdev, "Init command 2 (0x0a config1) failed: %d\n", ret);
  }

  /* Report 0x0a - Config 2 (15 bytes) */
  memset(init_buf, 0, 15);
  init_buf[0] = 0x0a;
  init_buf[1] = 0x04;
  init_buf[2] = 0x12;
  init_buf[3] = 0x10;
  ret = t500rs_send_usb(t500rs, init_buf, 15);
  if (ret) {
    hid_warn(t500rs->hdev, "Init command 3 (0x0a config2) failed: %d\n", ret);
  }

  /* Report 0x0a - Config 3 (15 bytes) */
  memset(init_buf, 0, 15);
  init_buf[0] = 0x0a;
  init_buf[1] = 0x04;
  init_buf[2] = 0x00;
  init_buf[3] = 0x06;
  ret = t500rs_send_usb(t500rs, init_buf, 15);
  if (ret) {
    hid_warn(t500rs->hdev, "Init command 4 (0x0a config3) failed: %d\n", ret);
  }

  /* Report 0x40 - Enable FFB (4 bytes) */
  /* CRITICAL FIX: Use Windows parameters 42 7b instead of 55 d5 */
  memset(init_buf, 0, 4);
  init_buf[0] = 0x40;
  init_buf[1] = 0x11;
  init_buf[2] = 0x42; /* Changed from 0x55 to match Windows! */
  init_buf[3] = 0x7b; /* Changed from 0xd5 to match Windows! */
  ret = t500rs_send_usb(t500rs, init_buf, 4);
  if (ret) {
    hid_warn(t500rs->hdev, "Init command 5 (0x40 enable) failed: %d\n", ret);
  }

  /* Report 0x42 - Additional init (2 bytes) */
  memset(init_buf, 0, 4);
  init_buf[0] = 0x42;
  init_buf[1] = 0x04;
  ret = t500rs_send_usb(t500rs, init_buf, 2);
  if (ret) {
    hid_warn(t500rs->hdev, "Init command 6 (0x42) failed: %d\n", ret);
  }

  /* Report 0x40 - Config (4 bytes) */
  memset(init_buf, 0, 4);
  init_buf[0] = 0x40;
  init_buf[1] = 0x04;
  init_buf[2] = 0x00;
  init_buf[3] = 0x00;
  ret = t500rs_send_usb(t500rs, init_buf, 4);
  if (ret) {
    hid_warn(t500rs->hdev, "Init command 7 (0x40 config) failed: %d\n", ret);
  }

  /* Report 0x43 - Set global gain (2 bytes) */
  /* CRITICAL FIX: Set gain to maximum (0xFF = 100%), not 0x00! */
  memset(init_buf, 0, 4);
  init_buf[0] = 0x43;
  init_buf[1] = 0xFF; /* Maximum gain - was 0x00 which DISABLED all forces! */
  ret = t500rs_send_usb(t500rs, init_buf, 2);
  if (ret) {
    hid_warn(t500rs->hdev, "Init command 8 (0x43) failed: %d\n", ret);
  }

  /* Report 0x41 - Clear effects (4 bytes) */
  memset(init_buf, 0, 4);
  init_buf[0] = 0x41;
  init_buf[1] = 0x00;
  init_buf[2] = 0x00;
  init_buf[3] = 0x00;
  ret = t500rs_send_usb(t500rs, init_buf, 4);
  if (ret) {
    hid_warn(t500rs->hdev, "Init command 9 (0x41 clear) failed: %d\n", ret);
  }

  /* Report 0x40 - Final setup (4 bytes) */
  memset(init_buf, 0, 4);
  init_buf[0] = 0x40;
  init_buf[1] = 0x08;
  init_buf[2] = 0x00;
  init_buf[3] = 0x00;
  ret = t500rs_send_usb(t500rs, init_buf, 4);
  if (ret) {
    hid_warn(t500rs->hdev, "Init command 10 (0x40 final) failed: %d\n", ret);
  }

  /* Report 0x40 - Set mode (4 bytes) */
  memset(init_buf, 0, 4);
  init_buf[0] = 0x40;
  init_buf[1] = 0x03;
  init_buf[2] = 0x0d;
  init_buf[3] = 0x00;
  ret = t500rs_send_usb(t500rs, init_buf, 4);
  if (ret) {
    hid_warn(t500rs->hdev, "Init command 11 (0x40 mode) failed: %d\n", ret);
  }

  /* Disable autocenter spring properly */
  /* Report 0x05 - Set spring coefficients to 0 */
  memset(init_buf, 0, 15);
  init_buf[0] = 0x05;
  init_buf[1] = 0x0e;
  init_buf[2] = 0x00;
  init_buf[3] = 0x00;  /* Right coefficient = 0 */
  init_buf[4] = 0x00;  /* Left coefficient = 0 */
  init_buf[9] = 0x00;  /* Right saturation = 0 */
  init_buf[10] = 0x00; /* Left saturation = 0 */
  ret = t500rs_send_usb(t500rs, init_buf, 11);
  if (ret) {
    hid_warn(t500rs->hdev, "Disable autocenter (0x05 0x0e) failed: %d\n", ret);
  }

  /* Report 0x05 - Set deadband and center */
  memset(init_buf, 0, 15);
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
  memset(init_buf, 0, 4);
  init_buf[0] = 0x41;
  init_buf[1] = 15;   /* Autocenter effect ID */
  init_buf[2] = 0x00; /* STOP */
  init_buf[3] = 0x01;
  ret = t500rs_send_usb(t500rs, init_buf, 4);
  if (ret) {
    hid_warn(t500rs->hdev, "Stop autocenter effect failed: %d\n", ret);
  } else {
    T500RS_DBG("Autocenter fully disabled\n");
  }

  hid_info(t500rs->hdev,
           "T500RS initialized successfully (USB INTERRUPT mode)\n");
  T500RS_DBG("Endpoint: 0x%02x, Buffer: %zu bytes\n", t500rs->ep_out,
             t500rs->buffer_length);

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

  T500RS_DBG("T500RS: Cleaning up\n");

  /* Free resources */
  kfree(t500rs->send_buffer);
  kfree(t500rs);

  return 0;
}

/* Populate API callbacks */
int t500rs_populate_api(struct tmff2_device_entry *tmff2) {
  int i;

  tmff2->play_effect = t500rs_play_effect;
  tmff2->upload_effect = t500rs_upload_effect;
  tmff2->update_effect = t500rs_update_effect;
  tmff2->stop_effect = t500rs_stop_effect;

  tmff2->set_gain = t500rs_set_gain;
  tmff2->set_autocenter = t500rs_set_autocenter;
  tmff2->set_range = t500rs_set_range;
  tmff2->set_spring_level = t500rs_set_spring_level;
  tmff2->set_damper_level = t500rs_set_damper_level;
  tmff2->set_friction_level = t500rs_set_friction_level;

  tmff2->wheel_init = t500rs_wheel_init;
  tmff2->wheel_destroy = t500rs_wheel_destroy;

  tmff2->params = t500rs_params;
  tmff2->max_effects = T500RS_MAX_EFFECTS;

  /* Copy supported effects array */
  for (i = 0; t500rs_effects[i] != -1 && i < FF_CNT; i++)
    tmff2->supported_effects[i] = t500rs_effects[i];
  tmff2->supported_effects[i] = -1;

  return 0;
}
