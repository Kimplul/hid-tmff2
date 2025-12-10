/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * T500RS Force Feedback Protocol Constants and Structures
 *
 * This header defines all protocol-specific constants and packet structures
 * for the Thrustmaster T500RS racing wheel force feedback implementation.
 */

#ifndef __T500RS_H
#define __T500RS_H

#include <linux/types.h>

/* Packet type constants */
#define T500RS_PKT_MAIN 0x01
#define T500RS_PKT_ENVELOPE 0x02
#define T500RS_PKT_CONSTANT 0x03
#define T500RS_PKT_PERIODIC 0x04
#define T500RS_PKT_CONDITIONAL 0x05
#define T500RS_PKT_COMMAND 0x41
#define T500RS_PKT_STATUS 0x42
#define T500RS_PKT_GAIN 0x43

/* Packet code constants */
#define T500RS_CODE_CONSTANT 0x0e
#define T500RS_CODE_PERIODIC 0x2a
#define T500RS_CODE_ENVELOPE 0x1c
#define T500RS_CODE_CONDITIONAL_X 0x2a
#define T500RS_CODE_CONDITIONAL_Y 0x38

/* Control and command constants */
#define T500RS_CONTROL_DEFAULT 0x40
#define T500RS_CMD_START 0x41
#define T500RS_CMD_STOP 0x00
#define T500RS_CMD_ARG 0x01

/* Effect type constants */
#define T500RS_EFFECT_CONSTANT 0x00
#define T500RS_EFFECT_SINE 0x22
#define T500RS_EFFECT_TRIANGLE 0x21
#define T500RS_EFFECT_SAW_UP 0x23
#define T500RS_EFFECT_SAW_DOWN 0x24
#define T500RS_EFFECT_SPRING 0x40
#define T500RS_EFFECT_DAMPER 0x41
#define T500RS_EFFECT_FRICTION 0x41
#define T500RS_EFFECT_INERTIA 0x41

/* Saturation values for conditional effects */
#define T500RS_SAT_SPRING 84
#define T500RS_SAT_DAMPER 100
#define T500RS_SAT_FRICTION 100
#define T500RS_SAT_INERTIA 100

/* Hardware limits */
#define T500RS_MAX_EFFECTS 16
#define T500RS_MAX_HW_EFFECTS T500RS_MAX_EFFECTS
#define T500RS_BUFFER_LENGTH 32 /* HID report max packet size */
#define T500RS_HID_TIMEOUT 1000 /* 1 second */

/* Gain scaling */
#define T500RS_GAIN_MAX 65535

/* Range limits */
#define T500RS_RANGE_MIN 40   /* Minimum range: 40 degrees */
#define T500RS_RANGE_MAX 1080 /* Maximum range: 1080 degrees */

/*
 * Packet Sequence Abstraction Enums
 *
 * These enums define the packet sequencing abstraction for effect uploads.
 * Used internally by the sequencing system to manage packet order.
 */
enum t500rs_seq_packet {
  T500RS_SEQ_STOP,
  T500RS_SEQ_SYNC_42_05,
  T500RS_SEQ_SYNC_42_04,
  T500RS_SEQ_ENVELOPE,
  T500RS_SEQ_CONSTANT,
  T500RS_SEQ_PERIODIC_RAMP,
  T500RS_SEQ_CONDITION_X,
  T500RS_SEQ_CONDITION_Y,
  T500RS_SEQ_MAIN,
};

/*
 * Packet Sequence Templates
 *
 * These arrays define the packet sequences for different effect types.
 * Used by the packet sequencing abstraction system.
 */
/* Sequence templates are now static in the implementation file */

/* Supported effects */
extern const signed short t500rs_effects[];

/*
 * T500RS USB Protocol Packet Structures
 *
 * These structures define the wire format for T500RS force feedback packets.
 * All structures are packed to match the exact USB protocol format.
 *
 * Packet formats verified against Windows USB captures in:
 * docs/T500RS_USB_Protocol_Analysis.md
 */

/*
 * 0x01 - Main upload packet (15 bytes)
 *
 * This packet initiates effect upload and specifies packet sequence.
 * Verified against Windows USB captures - all fields match observed traffic.
 *
 * Packet format:
 * - b0: packet type (0x01)
 * - b1: hardware effect slot ID (0-15, assigned by driver)
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
   u8 id;                /* b0: T500RS_PKT_MAIN */
   u8 effect_id;         /* b1: hardware effect slot ID (0-15) */
   u8 effect_type;       /* b2: effect type (T500RS_EFFECT_*) */
   u8 control;           /* b3: always T500RS_CONTROL_DEFAULT (0x40) */
   __le16 duration_ms;   /* b4-b5: duration in ms (LE) */
   __le16 delay_ms;      /* b6-b7: delay before start in ms (LE) */
   u8 reserved1;         /* b8: 0x00 */
   __le16 packet_code_1; /* b9-b10: param subtype for 0x03/0x04/0x05 (LE) */
   __le16 packet_code_2; /* b11-b12: env subtype for 0x02 (LE) */
   __le16 reserved2;     /* b13-b14: 0x0000 */
} __packed;

/*
 * 0x04 - Periodic / Ramp parameters (8 bytes)
 *
 * Used for both periodic effects (sine, triangle, sawtooth) and ramp effects.
 * Code field must match the subtype specified in 0x01 packet bytes 9-10.
 *
 * Packet format (verified against Windows captures):
 * - b0: packet type (0x04)
 * - b1: subtype code (from 0x01 packet_code_1, typically 0x2a)
 * - b2: reserved (0x00)
 * - b3: magnitude (0-127, scaled from SDL 0-32767)
 * - b4: offset (signed -127 to +127, scaled from SDL -32768 to +32767)
 * - b5: phase (0-255 for 360°, scaled from SDL 0-35999)
 * - b6-b7: period in milliseconds (LE, no Hz conversion!)
 *
 * For ramp effects: phase=0, period=ramp duration, magnitude/offset encode start/end levels.
 */
struct t500rs_pkt_r04_periodic_ramp {
   u8 id;            /* b0: T500RS_PKT_PERIODIC */
   u8 code;          /* b1: subtype code (from 0x01 packet_code_1) */
   u8 reserved1;     /* b2: always 0x00 */
   u8 magnitude;     /* b3: 0..127 magnitude (scaled) */
   u8 offset;        /* b4: signed -127..+127 offset (scaled) */
   u8 phase;         /* b5: 0..255 phase (0-360 degrees) */
   __le16 period_ms; /* b6-b7: period in milliseconds (LE) */
} __packed;

/*
 * 0x05 - Conditional parameters (11 bytes)
 *
 * CRITICAL: Windows captures show that conditional effects require TWO 0x05 packets,
 * but coefficients/deadband/center MUST be zero. Only saturation values control behavior.
 *
 * Packet format (verified against Windows captures):
 * - b0: packet type (0x05)
 * - b1: subtype code (from 0x01 packet_code_1 or packet_code_2)
 * - b2-b3: right coefficient (LE) - MUST BE ZERO
 * - b4-b5: left coefficient (LE) - MUST BE ZERO
 * - b6-b7: deadband (LE) - MUST BE ZERO
 * - b8: center - MUST BE ZERO
 * - b9: right saturation (0-100, controls spring/damper strength)
 * - b10: left saturation (0-100, controls spring/damper strength)
 *
 * Firmware rejects packets with non-zero coefficients, causing EPROTO errors.
 * Effect behavior is controlled solely through saturation values.
 */
struct t500rs_pkt_r05_condition {
   u8 id;   /* T500RS_PKT_CONDITIONAL */
   u8 code; /* from 0x01 code1/code2 (T500RS_CODE_*) */
   __le16 right_coeff;  /* Currently zero - needs capture verification */
   __le16 left_coeff;   /* Currently zero - needs capture verification */
   __le16 deadband;     /* Experimental: scaled from ff_condition_effect.deadband */
   u8 center;           /* Experimental: scaled from ff_condition_effect.center */
   u8 right_sat;        /* 0-100: controls effect strength */
   u8 left_sat;         /* 0-100: controls effect strength */
} __packed;

/* 0x03 - Constant force level (4 bytes) */
struct t500rs_r03_const {
  u8 id;    /* T500RS_PKT_CONSTANT */
  u8 code;  /* T500RS_CODE_CONSTANT */
  u8 zero;  /* 0x00 */
  s8 level; /* -127..127 */
} __packed;

/* 0x41 - START/STOP command (4 bytes) */
struct t500rs_r41_cmd {
  u8 id;        /* 0x41 */
  u8 effect_id; /* usually 0 on T500RS */
  u8 command;   /* 0x41 START, 0x00 STOP, 0x00 clear in init */
  u8 arg;       /* 0x01 */
} __packed;

/* 0x02 - Envelope packet (9 bytes) */
struct t500rs_pkt_r02_envelope {
  u8 id;             /* 0x02 */
  u8 subtype;        /* from 0x01 code2 (env_sub low byte) */
  __le16 attack_len; /* attack duration in ms */
  u8 attack_level;   /* 0-255 */
  __le16 fade_len;   /* fade duration in ms */
  u8 fade_level;     /* 0-255 */
  u8 reserved;       /* 0x00 */
} __packed;

#endif /* __T500RS_PROTOCOL_H */
