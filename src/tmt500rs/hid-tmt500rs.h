// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  T500RS Force Feedback Protocol Constants and Structures for
 *  Thrustmaster T500RS wheel base.
 *
 *  Protocol documentation: docs/T500RS_FFBEFFECTS.md
 *  This header defines all protocol-specific constants and packet structures
 *  for the Thrustmaster T500RS racing wheel force feedback implementation.
 *
 *  Copyright (c) 2025 Casimir Bonnet <casimir.bonnet@gmail.com>
 */

#ifndef __HID_TMT500RS_H
#define __HID_TMT500RS_H

#include <linux/types.h>

/* Packet type constants */
#define T500RS_PKT_MAIN 0x01
#define T500RS_PKT_CONSTANT 0x03
#define T500RS_PKT_PERIODIC 0x04
#define T500RS_PKT_CONDITIONAL 0x05
#define T500RS_PKT_GAIN 0x43

/* Control constants */
#define T500RS_CONTROL_DEFAULT 0x40

/* Per Windows USB captures of the official driver, the effect_id byte of
 * every 0x01 main-upload and every 0x41 START/STOP packet mirrors
 * the hardware slot index the effect was uploaded to:
 *
 *   effect_id == (param_sub - 0x000e) / 0x001c
 *
 * Slot 0 (param_sub=0x000e, env_sub=0x001c) is the constant-force slot; every other
 * slot is assigned sequentially to non-constant effects. The driver derives the hw
 * slot from the effect type and effect->id via t500rs_effect_to_hw_id() and threads
 * it through every 0x01/0x41 packet.
 *
 * The init-time autocenter teardown is the only exception, which targets a fixed
 * slot 15 via T500RS_AUTOCENTER_STOP_ID. */
#define T500RS_AUTOCENTER_STOP_ID 15

/* Fixed constant-force subtypes. These must NOT be per-effect: using
 * per-effect subtypes for constant force breaks level updates (no torque).
 * See docs/T500RS_FFBEFFECTS.md "Special case - constant force subtypes". */
#define T500RS_CONSTANT_PARAM_SUB 0x0e
#define T500RS_CONSTANT_ENV_SUB 0x1c

/* Effect type constants - codes this driver actually puts on the wire.
 *
 * Host-side synthesis model (docs/T500RS_FFBEFFECTS.md section 5.4): the
 * firmware has no periodic waveform engine. Windows declares periodic
 * effects as a sine (0x22) MAIN on slot 0 with the constant-force
 * channels and streams the synthesized waveform as 0x04 0x0e level
 * updates. Only these four type codes are known-good; any other value
 * in the 0x2x range (square 0x20, triangle 0x21, saw 0x23/0x24) is
 * unsourced and MUST NOT be sent.
 */
#define T500RS_EFFECT_CONSTANT 0x00
#define T500RS_EFFECT_SINE 0x22 /* the only periodic MAIN type; all waveforms are synthesized onto it */
#define T500RS_EFFECT_SPRING 0x40
#define T500RS_EFFECT_DAMPER 0x41
#define T500RS_EFFECT_FRICTION 0x41
#define T500RS_EFFECT_INERTIA 0x41

/* Hardware limits */
/* Advertise 15 logical effect slots to the framework (logical IDs 0..14).
 * The device/hardware ID space remains 0..15 (16 entries), but we avoid using
 * the hardware slot 0 (driver maps logical -> hw as logical+1). This prevents
 * producing invalid hw_id == 16 when logical IDs of 0..15 are allowed.
 */
#define T500RS_MAX_EFFECTS 15
#define T500RS_MAX_HW_EFFECTS 16
#define T500RS_BUFFER_LENGTH 32 /* HID report max packet size */

/* Gain scaling */
#define T500RS_GAIN_MAX 65535

/* Range limits */
#define T500RS_RANGE_MIN 40 /* Minimum range: 40 degrees */
#define T500RS_RANGE_MAX 1080 /* Maximum range: 1080 degrees */

/*
 * Packet Sequence Abstraction Enums
 *
 * These enums define the packet sequencing abstraction for effect uploads.
 * Used internally by the sequencing system to manage packet order.
 */
enum t500rs_seq_packet {
	T500RS_SEQ_ENVELOPE,
	T500RS_SEQ_CONSTANT,
	T500RS_SEQ_CONDITION_X,
	T500RS_SEQ_CONDITION_Y,
	T500RS_SEQ_MAIN,
};

/* Supported effects */
extern const signed short t500rs_effects[];

/*
 * T500RS USB Protocol Packet Structures
 *
 * These structures define the wire format for T500RS force feedback packets.
 * All structures are packed to match the exact USB protocol format.
 * Field-by-field reference: docs/T500RS_FFBEFFECTS.md
 */

/*
 * 0x01 - Main upload packet (15 bytes)
 *
 * This packet initiates effect upload and specifies packet sequence.
 *
 * Packet format:
 * - b0: packet type (0x01)
 * - b1: hardware effect slot ID (1-15, assigned by driver)
 * - b2: effect type (T500RS_EFFECT_* constants)
 * - b3: control flags (always 0x40)
 * - b4-b5: duration in milliseconds (LE)
 * - b6-b7: delay before start in milliseconds (LE)
 * - b8: reserved (0x00)
 * - b9-b10: parameter packet subtype (LE) - determines 0x03/0x04/0x05 codes
 * - b11-b12: envelope packet subtype (LE) - determines 0x02 code
 * - b13-b14: reserved (0x0000)
 */
struct t500rs_pkt_r01_main {
  u8 id; /* b0: T500RS_PKT_MAIN */
  u8 effect_id; /* b1: hardware slot index (0=constant, 1+=non-constant) */
	u8 effect_type; /* b2: effect type (T500RS_EFFECT_*) */
	u8 control; /* b3: always T500RS_CONTROL_DEFAULT (0x40) */
	__le16 duration_ms; /* b4-b5: duration in ms (LE) */
	__le16 delay_ms; /* b6-b7: delay before start in ms (LE) */
	u8 reserved1; /* b8: 0x00 */
	__le16 packet_code_1; /* b9-b10: param subtype for 0x03/0x04/0x05 (LE) */
	__le16 packet_code_2; /* b11-b12: env subtype for 0x02 (LE) */
	__le16 reserved2; /* b13-b14: 0x0000 */
} __packed;

/*
 * 0x04 - Constant-channel DC level stream (8 bytes)
 *
 * The only 0x04 form the firmware accepts. Windows drivers synthesize
 * periodic/ramp waveforms host-side and stream the combined signed level
 * on the constant-force channel (code 0x0e) using this packet. The
 * trailing 0x2710 (LE) is a constant magic marker.
 *
 * A per-slot periodic-parameters variant (code != 0x0e, e.g. '04 2a ...')
 * wedges the wheel until it drops off the bus. Do not reinvent it.
 */
struct t500rs_pkt_r04_stream {
	u8 id; /* b0: T500RS_PKT_PERIODIC */
	u8 code; /* b1: always 0x0e (constant-force channel) */
	u8 zero1; /* b2: 0x00 */
	u8 zero2; /* b3: 0x00 */
	s8 level; /* b4: signed force level, the synthesized signal */
	u8 zero3; /* b5: 0x00 */
	u8 magic_lo; /* b6-b7: 0x2710 LE magic (b6 = 0x10) */
	u8 magic_hi; /* b7 = 0x27 */
} __packed;

/*
 * 0x05 - Conditional Effect Packet (11 bytes)
 *
 * Packet format:
 * - b0: packet type (0x05)
 * - b1: code (from 0x01 packet_code_1 or packet_code_2)
 * - b2: reserved (always 0x00)
 * - b3: right coefficient (u8, 0-10 scale)
 * - b4: left coefficient (u8, 0-10 scale)
 * - b5-b6: center/offset (s16 LE, scaled from Linux +-32767 range)
 * - b7-b8: deadband (u16 LE, scaled from Linux 0-65535 range)
 * - b9: right saturation (0-100, controls effect strength)
 * - b10: left saturation (0-100, controls effect strength)
 *
 * Scaling (from Linux FFB to device):
 * - Coefficients: (value * level% * 10) / 32767 -> 0-10 u8, rounded
 * - Center: value / 20 -> s16 LE
 * - Deadband: value / 65 -> u16 LE (divisor unconfirmed)
 * - Saturation: 0-100 (no scaling)
 */
struct t500rs_pkt_r05_condition {
	u8 id; /* T500RS_PKT_CONDITIONAL */
	u8 code; /* from 0x01 code1/code2 */
	u8 reserved; /* Always 0x00 */
	u8 right_coeff; /* Right/positive coefficient (0-10 scale) */
	u8 left_coeff; /* Left/negative coefficient (0-10 scale) */
	__le16 center; /* Center offset (s16 LE, scaled by /20) */
	__le16 deadband; /* Deadband width (u16 LE, scaled by /65; divisor unconfirmed) */
	u8 right_sat; /* Right saturation (0-100) */
	u8 left_sat; /* Left saturation (0-100) */
} __packed;

/* 0x03 - Constant force level (4 bytes) */
struct t500rs_r03_const {
	u8 id; /* T500RS_PKT_CONSTANT */
	u8 code; /* param_subtype low byte (e.g. 0x0e for slot 0) */
	u8 zero; /* 0x00 */
	s8 level; /* -127..127 */
} __packed;

/* 0x41 - START/STOP command (4 bytes) */
struct t500rs_r41_cmd {
  u8 id; /* 0x41 */
  u8 effect_id; /* hardware slot index (0=constant, 1+=non-constant); init STOP uses 15 */
  u8 command; /* 0x41 START, 0x00 STOP, 0x00 clear in init */
  u8 arg; /* 0xff for START, 0x01 for STOP */
} __packed;

/* 0x02 - Envelope packet (9 bytes) */
struct t500rs_pkt_r02_envelope {
	u8 id; /* 0x02 */
	u8 subtype; /* from 0x01 code2 (env_sub low byte) */
	__le16 attack_len; /* attack duration in ms */
	u8 attack_level; /* 0-255 */
	__le16 fade_len; /* fade duration in ms */
	u8 fade_level; /* 0-255 */
	u8 reserved; /* 0x00 */
} __packed;

/* 0x40 - Configuration packet (4 bytes) */
struct t500rs_pkt_r40_config {
	u8 id; /* 0x40 */
	u8 subcmd; /* subcommand */
	u8 data1; /* first data byte */
	u8 data2; /* second data byte */
} __packed;

#endif /* __HID_TMT500RS_H */
