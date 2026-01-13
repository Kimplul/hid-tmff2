# T500RS USB Force Feedback Protocol Analysis

## Comprehensive Effect Implementation Reference
This document provides a detailed analysis of the T500RS force feedback protocol, based on USB captures using the ffbsdl tool on Windows and implementation iterations to create a Linux driver that supports all effects on par with the official Windows driver.

All values are little-endian unless specified otherwise.

> **NOTE:** All values documented here are examples of actual commands captured on the USB interface, not the only possible values.

---

## GENERAL CONCEPTS

### Device Overview
The T500RS is a single-axis force feedback wheel with a rotating range that can be configured (typically 900 degrees or 1080 degrees). It uses a proprietary USB protocol for force feedback effects, distinct from the T300RS and other Thrustmaster wheels.

### Effect Playing and Stopping

#### Playing an Effect
```
41 00 41 01 - START command
```
- **Packet Type:** 0x41 (Command)
- **Effect ID:** 0x00 (always 0x00 for T500RS)
- **Command:** 0x41 (START)
- **Argument:** 0x01 (play count, 0x01 = play once)

#### Stopping an Effect
```
41 00 00 01 - STOP command
```
- **Packet Type:** 0x41 (Command)
- **Effect ID:** 0x00 (always 0x00 for T500RS)
- **Command:** 0x00 (STOP)
- **Argument:** 0x01 (stop parameter)

### USB Packet Types Overview

| Packet ID | Name | Size | Purpose |
|-----------|------|------|---------|
| 0x01 | Main Upload | 15 bytes | Effect type, direction, duration, packet codes |
| 0x02 | Envelope | 9 bytes | Attack/fade parameters (limited support) |
| 0x03 | Constant Force | 4 bytes | Force level for constant effects |
| 0x04 | Periodic/Ramp | 8 bytes | Magnitude, offset, phase, period |
| 0x05 | Conditional | 11 bytes | Spring/damper/inertia/friction parameters |
| 0x41 | Command | 4 bytes | START (0x41) / STOP (0x00) |
| 0x42 | Status/Control | 2-16 bytes | Device status queries (0x00, 0x04, 0x05) |
| 0x43 | Init/Config | 64 bytes | Device initialization |
| 0x49 | Polling | 7-16 bytes | Status polling (high frequency) |
| 0x07 | Telemetry | 15 bytes | Position feedback (high frequency) |

**Important Notes:**
- **Envelope Support:** Envelope packets (0x02) have limited support. Non-zero envelope values cause EPROTO errors on periodic and constant effects. Always send zeros for envelope parameters on these effect types.
- **Runtime Updates:** Effect updates (via `update_effect` callback) only modify parameter-specific packets (0x03, 0x04, 0x05). Duration and delay changes require re-uploading the entire effect.
- **Effect Indexing:** The T500RS uses a unique subtype system for effect indexing. See the [Subtype System and Effect Indexing](#subtype-system-and-effect-indexing) section for details.

---

## Packet Structure Details

### 0x01 - Main Upload Packet (15 bytes)
```
Offset | Size | Field          | Description
-------|------|----------------|----------------------------------
 0      | 1    | packet_type    | 0x01
 1      | 1    | effect_id      | Hardware effect slot ID (0-15, assigned by driver)
 2      | 1    | effect_type    | Effect type (see table below)
 3      | 1    | control        | Always 0x40
 4      | 2    | duration_ms    | Duration in milliseconds, little-endian
 6      | 2    | delay_ms       | Delay before start, little-endian
 8      | 1    | reserved1      | 0x00
 9      | 2    | packet_code_1  | Code for subsequent packet type (variable!)
11      | 2    | packet_code_2  | Code for second subsequent packet (variable!)
13      | 2    | reserved2      | 0x0000
```

**Driver Implementation Note:** effect_id must be unique for concurrent effects to prevent slot collision. Use hardware ID allocation (0-15) instead of always 0x00.

**Effect Type Codes (byte 2):**
| Code | Effect Type | Source |
|------|-------------|--------|
| 0x00 | Constant | Windows driver captures |
| 0x20 | Square | FFEdit captures (December 2025) |
| 0x21 | Triangle | Windows driver captures |
| 0x22 | Sine | Windows driver captures |
| 0x23 | Sawtooth Up | Inferred from pattern |
| 0x24 | Sawtooth Down | Inferred from pattern |
| 0x40 | Spring | Windows driver captures |
| 0x41 | Damper/Friction/Inertia | Windows driver + FFEdit captures |

**Note:** Square wave (0x20) was discovered in FFEdit captures. The Windows driver may not expose this effect type through the standard API.

**IMPORTANT:** Bytes 9-12 specify the packet codes used in subsequent packets. These are NOT fixed values!

**Common Code Combinations:**
- Constant effects: bytes 9-10 = 0x000e (for 0x03 packet), bytes 11-12 = 0x001c (envelope)
- Periodic effects: bytes 9-10 = 0x002a (for 0x04 packet), bytes 11-12 = 0x001c (envelope)
- Conditional effects: bytes 9-10 = 0x002a (for first 0x05 packet), bytes 11-12 = 0x0038 (for second 0x05 packet)
- Alternative codes observed: 0x00b6/0x00c4 (newer captures), 0x0046/0x0054, 0x0062/0x0070, 0x007e/0x008c, 0x009a/0x00a8

**Examples:**
- `01 01 00 40 f4 01 00 00 0e 00 1c 00 00 00` - Constant effect with envelope
  - Effect ID: 0x01 (hardware slot 1, logical 0)
  - Effect type: 0x00 (constant)
  - Control: 0x40
  - Duration: 0x01f4 = 500ms
  - Delay: 0x0000 = 0ms
  - Reserved1: 0x00
  - Packet codes: 0x000e (constant), 0x001c (envelope)
  - Reserved2: 0x0000

- `01 01 40 40 d0 07 00 00 2a 00 38 00 00 00` - Conditional effect
  - Effect ID: 0x01 (hardware slot 1, logical 0)
  - Effect type: 0x40 (conditional)
  - Control: 0x40
  - Duration: 0x07d0 = 2000ms
  - Delay: 0x0000 = 0ms
  - Reserved1: 0x00
  - Packet codes: 0x002a (first conditional), 0x0038 (second conditional)
  - Reserved2: 0x0000

### 0x02 - Envelope Packet (9 bytes)
```
Offset | Size | Field          | Description
-------|------|----------------|----------------------------------
0      | 1    | packet_type    | 0x02
1      | 1    | subtype        | Low byte of envelope_subtype from 0x01 packet (dynamic)
2      | 2    | attack_len_ms  | Attack duration in ms, little-endian
4      | 1    | attack_level   | Attack level 0-255
5      | 2    | fade_len_ms    | Fade duration in ms, little-endian
7      | 1    | fade_level     | Fade level 0-255
8      | 1    | reserved       | 0x00
```

**Example:** `02 1c 00 00 12 00 00 12 00`
- Attack: 0ms, level 18
- Fade: 0ms, level 18

**IMPORTANT FIRMWARE LIMITATION:**
Windows driver always sends zeros for envelope on periodic and constant effects:
`02 38 00 00 00 00 00 00 00`

Non-zero envelope values cause EPROTO (-71) on subsequent packets. This appears
to be a firmware bug - the device does not properly support envelope parameters
for these effect types. The Linux driver must also send zeros to avoid crashes.

### 0x03 - Constant Force Packet (4 bytes)
```
Offset | Size | Field          | Description
-------|------|----------------|----------------------------------
0      | 1    | packet_type    | 0x03
1      | 1    | code           | Low byte of parameter_subtype from 0x01 packet (dynamic)
2      | 1    | reserved       | 0x00
3      | 1    | level          | Signed -127 to +127
```

**Examples:**
- `03 0e 00 00` - Level 0 (no force)
- `03 0e 00 09` - Level 9 (weak positive)
- `03 0e 00 f9` - Level -7 (0xf9 = -7 signed, weak negative)

### 0x04 - Periodic/Ramp Packet (8 bytes)
```
Offset | Size | Field          | Description
-------|------|----------------|----------------------------------
0      | 1    | packet_type    | 0x04
1      | 1    | code           | Variable (from 0x01 packet bytes 9-10)
2      | 1    | magnitude      | 0-127 (effect strength)
3      | 1    | offset         | Signed -127 to +127 (DC offset)
4      | 1    | phase          | 0-255 (0-360 degrees, 256 steps)
5      | 2    | period_ms      | Period in milliseconds, little-endian
7      | 1    | reserved       | 0x00
```

**Code Values:** The code in byte 1 is the low byte of the parameter_subtype from the 0x01 packet (dynamically calculated based on effect index)

**Period Encoding:** Period is in MILLISECONDS (not Hz*100). No conversion needed.

**Examples:**
- `04 2a 00 00 00 0a 00 00` - Code 0x2a, magnitude 0, period 10ms
- `04 2a 06 00 3f 0a 00 00` - Code 0x2a, magnitude 6, phase 63 (88.6degrees), period 10ms
- `04 2a 09 00 7f 64 00 00` - Code 0x2a, magnitude 9, phase 127 (178.6degrees), period 100ms
- `04 b6 00 00 7f 00 00 00` - Code 0xb6, magnitude 0, phase 127 (ramp effect)

### 0x05 - Conditional Effect Packet (11 bytes)

**IMPORTANT:** Conditional effects (spring, damper, inertia, friction) require TWO 0x05 packets!

**Packet Structure:**
```
Offset | Size | Field          | Description
-------|------|----------------|----------------------------------
0      | 1    | packet_type    | 0x05
1      | 1    | code           | Low byte of parameter_subtype or envelope_subtype from 0x01 packet (dynamic)
2      | 1    | reserved       | Always 0x00
3      | 1    | right_coeff    | Right/positive coefficient (0-10 scale, u8)
4      | 1    | left_coeff     | Left/negative coefficient (0-10 scale, u8)
5-6    | 2    | center         | Center offset (s16 LE, scaled: device = input/20)
7-8    | 2    | deadband       | Deadband width (u16 LE, scaled: device = input/10)
9      | 1    | right_sat      | Right saturation (0-100)
10     | 1    | left_sat       | Left saturation (0-100)
```

**Second Packet (Y-axis):** Same structure with second code from 0x01 packet.

**NOTE:** T500RS is single-axis, so Y-axis packet typically contains zeros.

**Parameter Scaling (Linux FFB -> Device):**
- **Coefficients:** 0-32767 -> Device 0-10 (multiply by 10/32767)
- **Center/Offset:** -32767 to +32767 -> Device s16 LE (divide by 65)
- **Deadband:** 0-65535 -> Device u16 LE (divide by 65)
- **Saturation:** 0-65535 -> Device 0-100 (multiply by 100/65535)

**Examples from Captures:**
- `05 0e 00 0a 0a 00 00 00 00 64 64` - Coeffs=10,10, center=0, deadband=0, sat=100
- `05 0e 00 06 04 fa 00 00 00 64 64` - Coeffs=6,4, center=250 (5000/20), deadband=0
- `05 0e 00 0a 0a 8c fe c2 01 64 64` - Coeffs=10,10, center=-372 (-7439/20), deadband=450

### 0x41 - Command Packet (4 bytes)
```
Offset | Size | Field          | Description
-------|------|----------------|----------------------------------
0      | 1    | packet_type    | 0x41
1      | 1    | effect_id      | Always 0x00 for T500RS
2      | 1    | command        | 0x41 = START, 0x00 = STOP
3      | 1    | argument       | 0x01 for START, varies for STOP
```

**Examples:**
- `41 00 41 01` - START effect
- `41 00 00 01` - STOP effect

---

## Effect Type Implementation Table

### 1. CONSTANT FORCE EFFECTS

**Capture Examples:**
- Zero force: `01 00 00 40 f4 01 00 00 0e 00 1c 00 00 00` `02 1c 00 00 00 00 00 00 00` `03 0e 00 00`
- Low positive force: `01 00 00 40 d0 07 00 00 0e 00 1c 00 00 00` `02 1c 00 00 06 00 00 06 00` `03 0e 00 03`
- Medium force: `01 00 00 40 d0 07 00 00 0e 00 1c 00 00 00` `02 1c 00 00 12 00 00 12 00` `03 0e 00 09`
- High negative force: `01 00 00 40 88 13 00 00 0e 00 1c 00 00 00` `02 1c 00 00 0d 00 00 0d 00` `03 0e 00 f9`
- Maximum force with direction: `01 00 00 40 d0 07 00 00 0e 00 1c 00 00 00` `02 1c 00 00 00 00 00 00 00` `03 0e 00 00`

**Packet Structure:**
- Main packet: `01 [effect_id] 00 40 [duration] [delay] 00 0e 00 1c 00 00 00`
  - effect_type = 0x00 (constant)
  - codes: 0x000e (constant parameter), 0x001c (envelope)
- Envelope packet: `02 1c [attack_len] [attack_lvl] [fade_len] [fade_lvl] 00`
- Constant packet: `03 0e 00 [level]`

**Parameter Details:**
- Force level: s8 (-127 to +127, scaled from Linux 0-65535 range)
- Direction: Applied during level scaling (projection onto wheel axis)
- Envelope: Attack/fade levels scaled 0-255 from Linux 0-32767
- Duration/Delay: Direct milliseconds in main packet

### 2. PERIODIC EFFECTS - SINE WAVE

**Capture Examples:**
- Zero magnitude: `01 00 22 40 d0 07 00 00 2a 00 1c 00 00 00` `02 1c 00 00 00 00 00 00 00` `04 2a 00 00 00 0a 00 00`
- Low magnitude with phase: `01 00 22 40 d0 07 00 00 2a 00 1c 00 00 00` `02 1c 00 00 06 00 00 06 00` `04 2a 06 00 3f 0a 00 00`
- Medium magnitude: `01 00 22 40 d0 07 00 00 2a 00 1c 00 00 00` `02 1c 00 00 12 00 00 12 00` `04 2a 09 00 7f 64 00 00`
- With envelope: `01 00 22 40 d0 07 00 00 2a 00 1c 00 00 00` `02 1c f4 01 12 f4 01 12 00` `04 2a 09 00 00 64 00 00`

**Packet Structure:**
- Main packet: `01 [effect_id] 22 40 [duration] [delay] 00 2a 00 1c 00 00 00`
  - effect_type = 0x22 (sine wave)
  - codes: 0x002a (periodic parameters), 0x001c (envelope)
- Envelope packet: `02 1c [attack_len] [attack_lvl] [fade_len] [fade_lvl] 00`
- Periodic packet: `04 2a [magnitude] [offset] [phase] [period_ms] 00`

**Parameter Details:**
- Magnitude: 0-127 (scaled from Linux 0-32767)
- Offset: s8 (-128 to +127, DC bias (Direct Current bias - a constant force offset), scaled from Linux -32768 to +32767)
- Phase: 0-255 (256 steps for 360 degrees, scaled from Linux 0-35999)
- Period: Direct milliseconds (no Hz conversion!)
- Direction: Applied during magnitude scaling (projection onto wheel axis)

### 3. PERIODIC EFFECTS - TRIANGLE WAVE

**Capture Examples:**
- Triangle wave: `01 00 21 40 d0 07 00 00 2a 00 1c 00 00 00` `02 1c 00 00 12 00 00 12 00` `04 2a 09 00 7f 64 00 00`

**Packet Structure:**
- Main packet: `01 [effect_id] 21 40 [duration] [delay] 00 2a 00 1c 00 00 00`
  - effect_type = 0x21 (triangle wave)
- Same envelope and periodic packet structure as sine wave

**Note:** Waveform type determined by effect_type in main packet, not in periodic packet parameters.

### 4. PERIODIC EFFECTS - SAWTOOTH UP

**Capture Examples:**
- Sawtooth up: `01 00 23 40 d0 07 00 00 2a 00 1c 00 00 00` `02 1c 00 00 0d 00 00 0d 00` `04 2a 06 00 bf 64 00 00`

**Packet Structure:**
- Main packet: `01 [effect_id] 23 40 [duration] [delay] 00 2a 00 1c 00 00 00`
  - effect_type = 0x23 (sawtooth up)
- Same envelope and periodic packet structure as sine wave

### 5. PERIODIC EFFECTS - SAWTOOTH DOWN

**Capture Examples:**
- Sawtooth down with offset: `01 00 24 40 d0 07 00 00 2a 00 1c 00 00 00` `02 1c 00 00 00 00 00 00 00` `04 2a 00 05 7f e8 03 00`

**Packet Structure:**
- Main packet: `01 [effect_id] 24 40 [duration] [delay] 00 2a 00 1c 00 00 00`
  - effect_type = 0x24 (sawtooth down)
- Same envelope and periodic packet structure as sine wave

**Note:** Offset field allows DC bias (Direct Current bias - a constant force offset) - useful for asymmetric waveforms like sawtooth where you want to shift the entire waveform up or down. This creates a net force in one direction over time.

### 6. RAMP EFFECTS

**Capture Examples:**
- Ramp up: `01 00 24 40 e8 03 00 00 2a 00 1c 00 00 00` `02 1c 00 00 00 00 00 00 00` `04 2a 03 00 00 e8 03 00`
- Ramp down: `01 00 24 40 e8 03 00 00 2a 00 1c 00 00 00` `02 1c 00 00 00 00 00 00 00` `04 2a 03 00 00 e8 03 00`
- Ramp with envelope: `01 00 24 40 88 13 00 00 2a 00 1c 00 00 00` `02 1c f4 01 12 f4 01 12 00` `04 2a 03 00 00 27 10 00`

**Packet Structure:**
- Main packet: `01 [effect_id] 24 40 [duration] [delay] 00 2a 00 1c 00 00 00`
  - effect_type = 0x24 (sawtooth down - used for ramps)
  - codes: 0x002a (ramp parameters), 0x001c (envelope)
- Envelope packet: `02 1c [attack_len] [attack_lvl] [fade_len] [fade_lvl] 00`
- Ramp packet: `04 2a [magnitude] [offset] [phase] [period_ms] 00`

**Parameter Details:**
- Magnitude: Average of start/end levels (0-127 scale)
- Offset: Difference between start/end levels (direction encoding)
- Phase: 0x7f for positive ramp (start<end), 0x00 for negative ramp (start>end)
- Period: Ramp duration in milliseconds
- Direction: Applied during magnitude calculation (projection onto wheel axis)

### 7. CONDITIONAL EFFECTS - SPRING

**Capture Examples:**
- Basic spring with low coefficients: `01 00 40 40 d0 07 00 00 2a 00 38 00 00 00` `05 2a 00 00 00 00 00 00 00 54 54` `05 38 00 00 00 00 00 00 00 54 54`
- Spring with deadband: `01 00 40 40 d0 07 00 00 2a 00 38 00 00 00` `05 2a 00 00 00 00 00 07 00 54 54` `05 38 00 00 00 00 00 00 00 54 54`
- Asymmetric spring: `01 00 40 40 d0 07 00 00 2a 00 38 00 00 00` `05 2a 00 00 00 99 00 4c 00 54 54` `05 38 00 00 00 00 00 00 00 54 54`

**Packet Structure:**
- Main packet: `01 [effect_id] 40 40 [duration] [delay] 00 2a 00 38 00 00 00`
  - effect_type = 0x40 (spring)
  - codes: 0x002a (X-axis), 0x0038 (Y-axis)
- First 0x05 packet (X-axis): `05 2a [right_coeff] [left_coeff] [center] [deadband] [right_sat] [left_sat]`
- Second 0x05 packet (Y-axis): `05 38 [right_coeff] [left_coeff] [center] [deadband] [right_sat] [left_sat]`

**Parameter Details:**
- Coefficients: 0-10 scale (Linux 0-32767 range)
- Center: s16 LE (+-500 range from Linux +-32767)
- Deadband: u16 LE (0-1008 from Linux 0-65535)
- Saturation: Dynamic right/left saturation (0-100 scale from Linux 0-65535 range)
- Y-axis typically uses zeros for single-axis wheel

### 8. CONDITIONAL EFFECTS - DAMPER

**Capture Examples:**
- Basic damper: `01 00 41 40 d0 07 00 00 2a 00 38 00 00 00` `05 2a 00 00 00 00 00 00 00 64 64` `05 38 00 00 00 00 00 00 00 64 64`
- Damper with coefficients: `01 00 41 40 d0 07 00 00 2a 00 38 00 00 00` `05 2a 00 0a 0a 00 00 00 00 64 64` `05 38 00 00 00 00 00 00 00 64 64`

**Packet Structure:**
- Main packet: `01 [effect_id] 41 40 [duration] [delay] 00 2a 00 38 00 00 00`
  - effect_type = 0x41 (damper/friction/inertia)
  - codes: 0x002a (X-axis), 0x0038 (Y-axis)
- First 0x05 packet (X-axis): `05 2a [right_coeff] [left_coeff] [center] [deadband] [right_sat] [left_sat]`
- Second 0x05 packet (Y-axis): `05 38 [right_coeff] [left_coeff] [center] [deadband] [right_sat] [left_sat]`

**Parameter Details:**
- Same structure as spring effects
- Saturation: Dynamic right/left saturation (0-100 scale from Linux 0-65535 range)
- Windows driver typically sends zero coefficients, relying on saturation
- FFEdit captures show non-zero coefficients may provide finer control

### 10. CONDITIONAL EFFECTS - INERTIA

**Implementation Note:** The current driver implementation for inertia effects matches the behavior of the Windows driver, which uses:
- Effect type: 0x41 (same as damper/friction)
- Two 0x05 packets with subtype codes from 0x01 bytes 9-12
- Right/left coefficients: Scaled from Linux 0-32767 range to device 0-10 scale
- Saturation: Dynamic right/left saturation (0-100 scale from Linux 0-65535 range)

**Driver Behavior:**
The driver will send non-zero coefficients for inertia effects if they are provided by the Linux FFB subsystem. However, based on Windows captures, the device may work with zero coefficients and rely solely on saturation values for effect strength.

**Parameter Details:**
- Same structure as damper effects
- Saturation: Dynamic right/left saturation (0-100 scale from Linux 0-65535 range)
- Coefficients: May be non-zero for fine-tuning inertia feel
- Windows driver typically uses saturation values around 100% for strong inertia effects

### 9. CONDITIONAL EFFECTS - FRICTION

**Status:** Limited capture data available. Uses same 0x05 packet structure as spring/damper.

**Capture Examples:**
- Basic friction: `01 00 41 40 d0 07 00 00 2a 00 38 00 00 00` `05 2a 00 00 00 00 00 00 00 64 64` `05 38 00 00 00 00 00 00 00 64 64`
- Friction with asymmetric coefficients: `01 00 41 40 d0 07 00 00 2a 00 38 00 00 00` `05 2a 00 08 05 00 00 00 00 64 64` `05 38 00 00 00 00 00 00 00 64 64`

**Packet Structure:**
- Main packet: `01 [effect_id] 41 40 [duration] [delay] 00 2a 00 38 00 00 00`
  - effect_type = 0x41 (same as damper/inertia)
  - codes: 0x002a (X-axis), 0x0038 (Y-axis)
- First 0x05 packet (X-axis): `05 2a [right_coeff] [left_coeff] [center] [deadband] [right_sat] [left_sat]`
- Second 0x05 packet (Y-axis): `05 38 [right_coeff] [left_coeff] [center] [deadband] [right_sat] [left_sat]`

**Parameter Details:**
- Same structure as damper effects
- Saturation: Dynamic right/left saturation (0-100 scale from Linux 0-65535 range)
- May require non-zero coefficients for proper friction feel
- FFEdit captures suggest asymmetric coefficients (stronger in one direction)

---

## COMMON PITFALLS AND IMPLEMENTATION TIPS

### Effect Indexing
- **Hardware ID Allocation:** The driver intentionally avoids hardware index 0, which has quirky behavior (only valid for constant effects). Instead, it maps logical IDs 0-14 to hardware IDs 1-15.
- **Subtype Calculation:** For hardware effect ID `n`, use:
  - `parameter_subtype = 0x000e + 0x001c * n`
  - `envelope_subtype  = 0x001c + 0x001c * n`

### Envelope Limitations
- **Periodic/Constant Effects:** Non-zero envelope values cause EPROTO errors. Always send zero envelope parameters for these effect types.
- **Ramp Effects:** Only ramp effects support envelopes. Send actual envelope values for ramp effects.

### Runtime Updates
- Only parameter-specific packets (0x03, 0x04, 0x05) can be updated at runtime. Duration and delay changes require re-uploading the entire effect.

### Conditional Effects
- **Saturation:** Use dynamic saturation values from effect parameters instead of hardcoded values. The device supports 0-100 range for both right and left saturation.
- **Coefficients:** Coefficients are scaled to 0-10 range. Non-zero coefficients may provide finer control, but Windows driver typically sends zeros.

### Direction Handling
- **Periodic Effects:** Direction affects the phase. Negative projections are handled by taking absolute value and adding 180 degrees to phase.

---

## Subtype System and Effect Indexing

The T500RS uses a unique subtype system for effect indexing. The last six bytes of the 0x01 main upload (bytes 9-14) carry two 16-bit "subtype" values that act as per-effect indices:

- Bytes 9-10  -> `parameter_subtype` (for 0x03, 0x04, and first 0x05 packets)
- Bytes 11-12 -> `envelope_subtype` (for 0x02 and second 0x05 packets)
- Bytes 13-14 -> padding (always 0x0000 in captures)

These subtype values are then copied into the "code" or "subtype" field of other packets so the device can associate parameter/envelope packets with a particular logical effect.

### Subtype Calculation
For hardware effect ID **n** (1-15), the wheel uses a simple arithmetic progression:

```c
parameter_subtype = 0x000e + 0x001c * n;
envelope_subtype  = 0x001c + 0x001c * n;
```

### Observed Subtype Pairs
| Hardware ID (n) | parameter_subtype | envelope_subtype |
|-----------------|-------------------|------------------|
| 1               | 0x002a            | 0x0038           |
| 2               | 0x0046            | 0x0054           |
| 3               | 0x0062            | 0x0070           |
| 4               | 0x007e            | 0x008c           |
| 5               | 0x009a            | 0x00a8           |
| 6               | 0x00b6            | 0x00c4           |

### Driver Implementation Notes
- **Effect ID Handling:** The driver uses hardware IDs 1-15 to avoid quirky behavior with hardware index 0 (only valid for constant effects).
- **Logical to Hardware ID Mapping:** `hw_id = logical_id + 1` (logical 0-14 -> hardware 1-15)
- **Subtype Usage in Packets:**
  - 0x02 envelope packets: `subtype = envelope_subtype & 0xff`
  - 0x03 constant packets: `code = parameter_subtype & 0xff`
  - 0x04 periodic/ramp packets: `code = parameter_subtype & 0xff`
  - 0x05 condition packets: First uses `parameter_subtype & 0xff`, second uses `envelope_subtype & 0xff`

### Envelope Parameters
Envelope attack/fade length and level values live **only** in the 0x02 packets; bytes 9-14 of 0x01 are *references* to those blocks, not the envelope parameters.

---

## Parameter Encoding Reference

### Direction Encoding
- **Linux FFB Format:** 0-65535 (0 = forward, 16384 = right, 32768 = back, 49152 = left)
- **Device Format:** 16-bit little-endian (0-35999 in 0.01 degree units)
- **Conversion:** `device_dir = (os_ffb_dir * 36000) / 65536`
- **Examples:**
  - 0degrees = 0x0000
  - 90degrees = 0x2328 (9000 decimal)
  - 180degrees = 0x4650 (18000 decimal)
  - 270degrees = 0x6978 (27000 decimal)

### Duration Encoding
- **Linux FFB Format:** Milliseconds
- **Device Format:** 16-bit little-endian in 0x01 packet
- **Conversion:** Direct copy (0xffff for infinite duration)
- **Examples:**
  - 500ms = 0x01f4
  - 1000ms = 0x03e8
  - 2000ms = 0x07d0
  - 5000ms = 0x1388

### Force Level Encoding (Constant)
- **Linux FFB Format:** -32767 to +32767 (signed)
- **Device Format:** -127 to +127 (signed 8-bit)
- **Conversion:** `device_level = (os_ffb_level * 127LL) / 32767`
- **Examples:**
  - Linux -32767 -> Device -127 (max negative)
  - Linux 0 -> Device 0 (neutral)
  - Linux 16384 -> Device 63 (medium positive)
  - Linux 32767 -> Device 127 (max positive)

### Magnitude Encoding (Periodic)
- **Linux FFB Format:** 0-32767 (unsigned)
- **Device Format:** 0-127 (unsigned 8-bit)
- **Conversion:** `device_mag = (os_ffb_mag * 127LL) / 32767`
- **Examples:**
  - Linux 0 -> Device 0
  - Linux 8000 -> Device 6
  - Linux 24000 -> Device 9
  - Linux 32767 -> Device 127

### Phase Encoding (Periodic)
- **Linux FFB Format:** 0-35999 (0.01 degree units, 0-359.99degrees)
- **Device Format:** 0-255 (256 steps for 360degrees)
- **Conversion:** `device_phase = (os_ffb_phase * 256) / 36000`
- **Examples:**
  - 0degrees (0) -> 0x00
  - 90degrees (9000) -> 0x40 (64)
  - 180degrees (18000) -> 0x80 (128)
  - 270degrees (27000) -> 0xC0 (192)

### Period Encoding (Periodic)
- **Linux FFB Format:** Milliseconds
- **Device Format:** 16-bit little-endian in 0x04 packet
- **Conversion:** Direct copy (keep in milliseconds, NOT Hz*100!)
- **Examples:**
  - 10ms = 0x000a
  - 50ms = 0x0032
  - 100ms = 0x0064
  - 1000ms = 0x03e8

### Envelope Level Encoding
- **Linux FFB Format:** 0-32767 (unsigned)
- **Device Format:** 0-255 (unsigned 8-bit)
- **Conversion:** `device_env = (os_ffb_env * 255LL) / 32767`
- **Examples:**
  - Linux 0 -> Device 0
  - Linux 8000 -> Device 6
  - Linux 16000 -> Device 12
  - Linux 24000 -> Device 18
  - Linux 32767 -> Device 255

### Conditional Effect Parameter Encoding
- **Coefficients (Right/Left):** Linux 0-32767 -> Device 0-10 (u8)
  - Formula: `device_coeff = (os_ffb_coeff * 10) / 32767`
- **Center Offset:** Linux -32767 to +32767 -> Device s16 LE (approx +-500)
  - Formula: `device_center = (os_ffb_center / 65)`
- **Deadband:** Linux 0-65535 -> Device u16 LE (0-1008)
  - Formula: `device_deadband = (os_ffb_deadband / 65)`
- **Saturation (Right/Left):** Linux 0-65535 -> Device 0-100 (u8)
  - Formula: `device_sat = (os_ffb_sat * 100) / 65535`
  