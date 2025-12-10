# T500RS USB Force Feedback Protocol Analysis
## Comprehensive Effect Implementation Reference
This is the result of the deep analysis of captures made using ffbsdl tool on windows and the implementation iterations to get a working driver supporting (hopefully) all effects on-par with windows official driver.

---

## USB Packet Types Overview

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

**Note:** Envelope support (0x02) is limited on T500RS hardware. Non-zero envelope values cause EPROTO errors on periodic and constant effects. Always send zeros for envelope parameters on these effect types.

---

## Subtype System and Effect Indexing

On this wheel the last six bytes of the 0x01 main upload (bytes 9–14) do **not** contain envelope timings/levels directly compared to the T300RS. Instead they carry two 16‑bit "subtype" values that act as per‑effect indices:

- Bytes 9–10  → `parameter_subtype`
- Bytes 11–12 → `envelope_subtype`
- Bytes 13–14 → padding (always 0x0000 in captures)

These subtype values are then copied into the "code" / "subtype" field of other packets (0x02, 0x03, 0x04, 0x05) so the device can associate parameter/envelope packets with a particular logical effect.

For effect index **n** (0‑based) the wheel uses a simple arithmetic progression:

- `parameter_subtype = 0x000e + 0x001c * n`
- `envelope_subtype  = 0x001c + 0x001c * n`

Observed pairs from captures:

| Effect index n | parameter_subtype | envelope_subtype |
|----------------|-------------------|------------------|
| 0              | 0x000e            | 0x001c           |
| 1              | 0x002a            | 0x0038           |
| 2              | 0x0046            | 0x0054           |
| 3              | 0x0062            | 0x0070           |
| 4              | 0x007e            | 0x008c           |
| 5              | 0x009a            | 0x00a8           |
| 6              | 0x00b6            | 0x00c4           |

Implications for the driver:

- `effect_id` (byte 1 of 0x01) stays **0x00** for normal uploads; logical effect slots are selected purely via these subtype pairs.
- The same subtype values appear in:
  - 0x02 envelope packets (`subtype = envelope_subtype`)
  - 0x03 constant packets (`code = parameter_subtype`)
  - 0x04 periodic/ramp packets (`code = parameter_subtype`)
  - 0x05 condition packets (`code = parameter_subtype` for the first, `code = envelope_subtype` for the second)

Envelope attack/fade length and level values themselves live **only** in the 0x02 packets; bytes 9–14 of 0x01 are *references* to those blocks, not the envelope parameters.

---

## Packet Structure Details

### 0x01 - Main Upload Packet (15 bytes)
```
Offset | Size | Field          | Description
-------|------|----------------|----------------------------------
 0      | 1    | packet_type    | 0x01
 1      | 1    | effect_id      | Hardware effect slot ID (0-15, assigned by driver)
 2      | 1    | effect_type    | Effect type (0x00=constant, 0x22=sine, 0x40=conditional)
 3      | 1    | control        | Always 0x40
 4      | 2    | duration_ms    | Duration in milliseconds, little-endian
 6      | 2    | delay_ms       | Delay before start, little-endian
 8      | 1    | reserved1      | 0x00
 9      | 2    | packet_code_1  | Code for subsequent packet type (variable!)
11      | 2    | packet_code_2  | Code for second subsequent packet (variable!)
13      | 2    | reserved2      | 0x0000
```

**Driver Implementation Note:** effect_id must be unique for concurrent effects to prevent slot collision. Use hardware ID allocation (0-15) instead of always 0x00.

**IMPORTANT:** Bytes 9-12 specify the packet codes used in subsequent packets. These are NOT fixed values!

**Common Code Combinations:**
- Constant effects: bytes 9-10 = 0x000e (for 0x03 packet), bytes 11-12 = 0x001c (envelope)
- Periodic effects: bytes 9-10 = 0x002a (for 0x04 packet), bytes 11-12 = 0x001c (envelope)
- Conditional effects: bytes 9-10 = 0x002a (for first 0x05 packet), bytes 11-12 = 0x0038 (for second 0x05 packet)
- Alternative codes observed: 0x00b6/0x00c4 (newer captures), 0x0046/0x0054, 0x0062/0x0070, 0x007e/0x008c, 0x009a/0x00a8

**Examples:**
- `01 00 00 40 f4 01 00 00 0e 00 1c 00 00 00` - Constant effect with envelope
  - Effect ID: 0x00
  - Effect type: 0x00 (constant)
  - Control: 0x40
  - Duration: 0x01f4 = 500ms
  - Delay: 0x0000 = 0ms
  - Reserved1: 0x00
  - Packet codes: 0x000e (constant), 0x001c (envelope)
  - Reserved2: 0x0000

- `01 00 40 40 d0 07 00 00 2a 00 38 00 00 00` - Conditional effect
  - Effect ID: 0x00
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
1      | 1    | subtype        | 0x1c
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
1      | 1    | code           | 0x0e
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

**Code Values:** The code in byte 1 matches bytes 9-10 of the 0x01 packet (0x2a, 0xb6, 0x46, etc.)

**Period Encoding:** Period is in MILLISECONDS (not Hz×100). No conversion needed.

**Examples:**
- `04 2a 00 00 00 0a 00 00` - Code 0x2a, magnitude 0, period 10ms
- `04 2a 06 00 3f 0a 00 00` - Code 0x2a, magnitude 6, phase 63 (88.6°), period 10ms
- `04 2a 09 00 7f 64 00 00` - Code 0x2a, magnitude 9, phase 127 (178.6°), period 100ms
- `04 b6 00 00 7f 00 00 00` - Code 0xb6, magnitude 0, phase 127 (ramp effect)

### 0x05 - Conditional Effect Packet (11 bytes)

**IMPORTANT:** Conditional effects (spring, damper, inertia, friction) require TWO 0x05 packets!

**First Packet (X-axis parameters):**
```
Offset | Size | Field          | Description
-------|------|----------------|----------------------------------
0      | 1    | packet_type    | 0x05
1      | 1    | code           | Variable (from 0x01 packet bytes 9-10, e.g. 0x2a or 0xb6)
2      | 2    | right_coeff    | Right coefficient, little-endian
4      | 2    | left_coeff     | Left coefficient, little-endian
6      | 2    | deadband       | Deadband, little-endian
8      | 1    | center         | Center offset
9      | 1    | right_sat      | Right saturation
10     | 1    | left_sat       | Left saturation
```

**Second Packet (Y-axis parameters):**
```
Offset | Size | Field          | Description
-------|------|----------------|----------------------------------
0      | 1    | packet_type    | 0x05
1      | 1    | code           | Variable (from 0x01 packet bytes 11-12, e.g. 0x38 or 0xc4)
2-10   | 9    | parameters     | Same structure as first packet
```

**NOTE:** T500RS is single-axis, so the second packet typically contains zeros.

**⚠️ CRITICAL FINDING :** Windows sends **zero coefficients, deadband, and center** in ALL 0x05 packets!
- The device firmware appears to reject 0x05 packets with non-zero coefficients
- Only saturation values (bytes 9-10) should be non-zero
- Sending non-zero coefficients causes EPROTO (-71) errors on subsequent packets
- The conditional effect behavior is determined by saturation values, not coefficients

**Examples (correct - zeros for coefficients):**
- First packet: `05 2a 00 00 00 00 00 00 00 54 54` - Code 0x2a, all zeros, saturation 0x54
- Second packet: `05 38 00 00 00 00 00 00 00 54 54` - Code 0x38, all zeros, saturation 0x54

**Example (INCORRECT - will cause device rejection):**
- `05 2a 7f 00 7f 00 00 00 7f 64 64` - Non-zero coefficients cause Y-axis packet to fail

**Parameter Scaling:** (Based on limited data, needs verification)
- Coefficients: SDL2 value (0-32767) → device value (scaling TBD)
- Deadband: SDL2 value (0-65535) → device value (scaling TBD)
- Center: SDL2 value (-32767 to +32767) → device value (0-255?)
- Saturation: SDL2 value (0-32767) → device value (0-255, observed: 0x54, 0x64)

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

| Test Case | SDL2 Parameters | Packet Sequence | Packet Payloads |
|-----------|----------------|-----------------|-----------------|
| **Constant Zero** | level=0, dir=0°, len=500ms | 1. Upload<br>2. Envelope<br>3. Constant<br>4. START | `01 00 00 40 f4 01 00 00 0e 00 1c 00 00 00`<br>`02 1c 00 00 00 00 00 00 00`<br>`03 0e 00 00`<br>`41 00 41 01` |
| **Constant Low** | level=8000, dir=0°, len=2000ms | 1. Upload<br>2. Envelope<br>3. Constant<br>4. START | `01 00 00 40 d0 07 00 00 0e 00 1c 00 00 00`<br>`02 1c 00 00 06 00 00 06 00`<br>`03 0e 00 03`<br>`41 00 41 01` |
| **Constant Medium** | level=24000, dir=0°, len=2000ms | 1. Upload<br>2. Envelope<br>3. Constant<br>4. START | `01 00 00 40 d0 07 00 00 0e 00 1c 00 00 00`<br>`02 1c 00 00 12 00 00 12 00`<br>`03 0e 00 09`<br>`41 00 41 01` |
| **Constant High** | level=48000, dir=0°, len=5000ms | 1. Upload<br>2. Envelope<br>3. Constant<br>4. START | `01 00 00 40 88 13 00 00 0e 00 1c 00 00 00`<br>`02 1c 00 00 0d 00 00 0d 00`<br>`03 0e 00 f9`<br>`41 00 41 01` |
| **Constant Max** | level=65535, dir=180°, len=2000ms | 1. Upload<br>2. Envelope<br>3. Constant<br>4. START | `01 00 00 40 d0 07 00 00 0e 00 1c 00 00 00`<br>`02 1c 00 00 00 00 00 00 00`<br>`03 0e 00 00`<br>`41 00 41 01` |

**Scaling Notes:**
- SDL2 level (0-65535) → Device level (-127 to +127)
- Envelope attack/fade level (0-32767) → Device level (0-255)
- Direction (0-35999) in 0.01 degree units

### 2. PERIODIC EFFECTS - SINE WAVE

| Test Case | SDL2 Parameters | Packet Sequence | Packet Payloads |
|-----------|----------------|-----------------|-----------------|
| **Sine Zero** | mag=0, period=10ms, phase=0°, dir=0° | 1. Upload<br>2. Envelope<br>3. Periodic<br>4. START | `01 00 00 40 d0 07 00 00 0e 00 1c 00 00 00`<br>`02 1c 00 00 00 00 00 00 00`<br>`04 2a 00 00 00 0a 00 00`<br>`41 00 41 01` |
| **Sine Low** | mag=8000, period=10ms, phase=90°, dir=90° | 1. Upload<br>2. Envelope<br>3. Periodic<br>4. START | `01 00 00 40 d0 07 00 00 0e 00 1c 00 00 00`<br>`02 1c 00 00 06 00 00 06 00`<br>`04 2a 06 00 3f 0a 00 00`<br>`41 00 41 01` |
| **Sine Medium** | mag=24000, period=100ms, phase=180°, dir=180° | 1. Upload<br>2. Envelope<br>3. Periodic<br>4. START | `01 00 00 40 d0 07 00 00 0e 00 1c 00 00 00`<br>`02 1c 00 00 12 00 00 12 00`<br>`04 2a 09 00 7f 64 00 00`<br>`41 00 41 01` |
| **Sine with Envelope** | mag=24000, period=100ms, attack=500ms, fade=500ms | 1. Upload<br>2. Envelope<br>3. Periodic<br>4. START | `01 00 00 40 d0 07 00 00 0e 00 1c 00 00 00`<br>`02 1c f4 01 12 f4 01 12 00`<br>`04 2a 09 00 00 64 00 00`<br>`41 00 41 01` |

**Periodic Effect Notes:**
- Magnitude (0-32767) → Device magnitude (0-127)
- Phase (0-35999, 0.01° units) → Device phase (0-255, 256 steps for 360°)
- Period in milliseconds (no conversion needed)
- Code 0x2a is used (NOT 0x0e as in current driver!)

### 3. PERIODIC EFFECTS - TRIANGLE WAVE

| Test Case | SDL2 Parameters | Packet Sequence | Packet Payloads |
|-----------|----------------|-----------------|-----------------|
| **Triangle Medium** | mag=24000, period=100ms, phase=180°, dir=180° | 1. Upload<br>2. Envelope<br>3. Periodic<br>4. START | `01 00 00 40 d0 07 00 00 0e 00 1c 00 00 00`<br>`02 1c 00 00 12 00 00 12 00`<br>`04 2a 09 00 7f 64 00 00`<br>`41 00 41 01` |

**Note:** Triangle uses same packet structure as sine; waveform type is determined by effect type in SDL2 upload, not in USB packets.

### 4. PERIODIC EFFECTS - SAWTOOTH UP

| Test Case | SDL2 Parameters | Packet Sequence | Packet Payloads |
|-----------|----------------|-----------------|-----------------|
| **Sawtooth Up High** | mag=48000, period=100ms, phase=270°, dir=270° | 1. Upload<br>2. Envelope<br>3. Periodic<br>4. START | `01 00 00 40 d0 07 00 00 0e 00 1c 00 00 00`<br>`02 1c 00 00 0d 00 00 0d 00`<br>`04 2a 06 00 bf 64 00 00`<br>`41 00 41 01` |

### 5. PERIODIC EFFECTS - SAWTOOTH DOWN

| Test Case | SDL2 Parameters | Packet Sequence | Packet Payloads |
|-----------|----------------|-----------------|-----------------|
| **Sawtooth Down Max** | mag=65535, period=1000ms, phase=0°, dir=0°, offset=+16000 | 1. Upload<br>2. Envelope<br>3. Periodic<br>4. START | `01 00 00 40 d0 07 00 00 0e 00 1c 00 00 00`<br>`02 1c 00 00 00 00 00 00 00`<br>`04 2a 00 05 7f e8 03 00`<br>`41 00 41 01` |

**Note:** Offset field (byte 3 of 0x04 packet) allows DC bias on periodic effects.

### 6. RAMP EFFECTS

| Test Case | SDL2 Parameters | Packet Sequence | Packet Payloads |
|-----------|----------------|-----------------|-----------------|
| **Ramp Up Low→High** | start=8000, end=48000, len=1000ms, dir=0° | 1. Upload<br>2. Envelope<br>3. Periodic<br>4. START | `01 00 00 40 e8 03 00 00 0e 00 1c 00 00 00`<br>`02 1c 00 00 00 00 00 00 00`<br>`04 2a 03 00 00 e8 03 00`<br>`41 00 41 01` |
| **Ramp Down High→Low** | start=48000, end=8000, len=1000ms, dir=180° | 1. Upload<br>2. Envelope<br>3. Periodic<br>4. START | `01 00 00 40 e8 03 00 00 0e 00 1c 00 00 00`<br>`02 1c 00 00 00 00 00 00 00`<br>`04 2a 03 00 00 e8 03 00`<br>`41 00 41 01` |
| **Ramp with Envelope** | start=8000, end=65535, len=5000ms, attack=500ms, fade=500ms | 1. Upload<br>2. Envelope<br>3. Periodic<br>4. START | `01 00 00 40 88 13 00 00 0e 00 1c 00 00 00`<br>`02 1c f4 01 12 f4 01 12 00`<br>`04 2a 03 00 00 27 10 00`<br>`41 00 41 01` |

**Ramp Effect Notes:**
- Ramp effects use 0x04 packet type (same as periodic)
- Start/end levels encoded in magnitude and offset fields
- Period field may encode ramp duration or rate

### 7. CONDITIONAL EFFECTS - SPRING

| Test Case | SDL2 Parameters | Packet Sequence | Packet Payloads |
|-----------|----------------|-----------------|-----------------|
| **Spring Low/High** | right_coeff=low, left_coeff=low, right_sat=high, left_sat=high | 1. Upload<br>2. Cond Axis 1<br>3. Cond Axis 2<br>4. START | `01 00 40 40 d0 07 00 00 2a 00 38 00 00 00`<br>`05 2a 00 00 00 00 00 00 00 54 54`<br>`05 38 00 00 00 00 00 00 00 54 54`<br>`41 00 41 01` |
| **Spring Deadband** | right_coeff=medium, left_coeff=medium, deadband=500, center=0 | 1. Upload<br>2. Cond Axis 1<br>3. Cond Axis 2<br>4. START | `01 00 40 40 d0 07 00 00 2a 00 38 00 00 00`<br>`05 2a 00 00 00 00 00 07 00 54 54`<br>`05 38 00 00 00 00 00 00 00 54 54`<br>`41 00 41 01` |
| **Spring Asymmetric** | right_coeff=0, left_coeff=high, deadband=5000, center=0 | 1. Upload<br>2. Cond Axis 1<br>3. Cond Axis 2<br>4. START | `01 00 40 40 d0 07 00 00 2a 00 38 00 00 00`<br>`05 2a 00 00 00 99 00 4c 00 54 54`<br>`05 38 00 00 00 00 00 00 00 54 54`<br>`41 00 41 01` |

**Conditional Packet Structure (0x05):**
- Two packets sent per conditional effect (one per axis)
- First packet: code 0x2a (X-axis parameters)
- Second packet: code 0x38 (Y-axis parameters)
- Bytes 2-3: Right coefficient (little-endian, 0-65535 scaled)
- Bytes 4-5: Left coefficient (little-endian, 0-65535 scaled)
- Bytes 6-7: Deadband (little-endian, scaled)
- Byte 8: Center offset (signed)
- Bytes 9-10: Right/Left saturation (0x5454 = 84,84)

### 8. CONDITIONAL EFFECTS - DAMPER

| Test Case | SDL2 Parameters | Packet Sequence | Packet Payloads |
|-----------|----------------|-----------------|-----------------|
| **Damper Low** | right_coeff=low, left_coeff=low, right_sat=max, left_sat=max | 1. Upload<br>2. Cond Axis 1<br>3. Cond Axis 2<br>4. START | `01 00 40 40 d0 07 00 00 2a 00 38 00 00 00`<br>`05 2a 00 00 00 00 00 00 00 64 64`<br>`05 38 00 00 00 00 00 00 00 64 64`<br>`41 00 41 01` |
| **Damper High** | right_coeff=high, left_coeff=high | 1. Upload<br>2. Cond Axis 1<br>3. Cond Axis 2<br>4. START | `01 00 40 40 d0 07 00 00 2a 00 38 00 00 00`<br>`05 2a 00 00 00 00 00 00 00 64 64`<br>`05 38 00 00 00 00 00 00 00 64 64`<br>`41 00 41 01` |

**Note:** Damper uses same 0x05 packet structure as spring. Saturation values differ (0x6464 for damper vs 0x5454 for spring).

### 10. CONDITIONAL EFFECTS - INERTIA

**Status:** Limited capture data available. Assumed to use same 0x05 packet structure as spring/damper.

**Expected Structure:**
- Two 0x05 packets with codes from 0x01 bytes 9-12
- Same parameter layout: right_coeff, left_coeff, deadband, center, saturation
- Saturation value may differ from spring/damper

### 11. CONDITIONAL EFFECTS - FRICTION

**Status:** Limited capture data available. Assumed to use same 0x05 packet structure as spring/damper.

**Expected Structure:**
- Two 0x05 packets with codes from 0x01 bytes 9-12
- Same parameter layout: right_coeff, left_coeff, deadband, center, saturation
- Saturation value may differ from spring/damper

### 12. MULTI-EFFECT SCENARIOS

#### Sequential Effects (No Overlap)

| Scenario | Description | Effect IDs Used | Packet Sequence |
|----------|-------------|-----------------|-----------------|
| **Constant → Sine** | Constant 1500ms, then Sine 1500ms | 0x00, then 0x01 | Effect 1 (ID=0x00): Upload→Envelope→Constant→START→[wait]→STOP<br>Effect 2 (ID=0x01): Upload→Envelope→Periodic→START→[wait]→STOP |
| **Sine → Triangle** | Sine 3000ms, then Triangle 3000ms | 0x00, then 0x01 | Effect 1 (ID=0x00): Upload→Envelope→Periodic→START→[wait]→STOP<br>Effect 2 (ID=0x01): Upload→Envelope→Periodic→START→[wait]→STOP |

**Sequential Effect Notes:**
- Each effect gets full upload sequence with unique effect ID
- Effect IDs are hardware slot numbers in the range 0-15 (0x00-0x0F)
- Previous effect must be stopped before starting next
- Gap between effects depends on timing in test

#### Overlapping Effects

| Scenario | Description | Effect IDs Used | Packet Sequence |
|----------|-------------|-----------------|-----------------|
| **Sine + Triangle Overlap** | Sine starts, Triangle joins after 1s, both run for 2s | 0x00, 0x01 | Effect 1 (ID=0x00): Upload→Envelope→Periodic→START<br>[wait 1s]<br>Effect 2 (ID=0x01): Upload→Envelope→Periodic→START<br>[wait 2s]<br>Effect 1 (ID=0x00): STOP<br>Effect 2 (ID=0x01): STOP |

**Overlapping Effect Notes:**
- T500RS supports up to 16 simultaneous effects (hardware capability)
- Effect IDs are hardware slot numbers (0x00-0x0F, i.e., 0-15)
- Effects are uploaded and started independently with different IDs
- Device mixes/sums the forces internally
- Driver uses sequential assignment (0x00, 0x01, 0x02, etc.) for simplicity

#### Rapid Sequential Effects

| Scenario | Description | Timing |
|----------|-------------|--------|
| **Short Rapid** | 3 effects × 200ms back-to-back | Constant→Sine→Spring, no gaps |
| **Short with Gaps** | 3 effects × 300ms with 100ms gaps | Sine→[100ms]→Constant→[100ms]→Damper |

**Rapid Effect Notes:**
- Device handles rapid effect changes (200ms duration)
- No special packet sequence needed for rapid changes
- Standard upload→start→stop sequence for each effect

---

## Parameter Encoding Reference

### Direction Encoding
- **SDL2 Format:** 0-35999 (0.01 degree units)
- **Device Format:** 16-bit little-endian in 0x01 packet
- **Conversion:** Direct copy, no scaling
- **Examples:**
  - 0° = 0x0000
  - 90° = 0x2328 (9000 decimal)
  - 180° = 0x4650 (18000 decimal)
  - 270° = 0x6978 (27000 decimal)

### Duration Encoding
- **SDL2 Format:** Milliseconds
- **Device Format:** 16-bit little-endian in 0x01 packet
- **Conversion:** Direct copy
- **Examples:**
  - 500ms = 0x01f4
  - 1000ms = 0x03e8
  - 2000ms = 0x07d0
  - 5000ms = 0x1388

### Force Level Encoding (Constant)
- **SDL2 Format:** 0-65535 (unsigned)
- **Device Format:** -127 to +127 (signed 8-bit)
- **Conversion:** Scale and sign
- **Formula:** `device_level = (sdl_level * 255 / 65535) - 127`
- **Examples:**
  - SDL 0 → Device 0
  - SDL 8000 → Device 3
  - SDL 24000 → Device 9
  - SDL 48000 → Device -7 (0xf9)
  - SDL 65535 → Device 127 (max positive)

### Magnitude Encoding (Periodic)
- **SDL2 Format:** 0-32767 (unsigned)
- **Device Format:** 0-127 (unsigned 8-bit)
- **Conversion:** Scale down
- **Formula:** `device_mag = sdl_mag * 127 / 32767`
- **Examples:**
  - SDL 0 → Device 0
  - SDL 8000 → Device 6
  - SDL 24000 → Device 9
  - SDL 32767 → Device 127

### Phase Encoding (Periodic)
- **SDL2 Format:** 0-35999 (0.01 degree units, 0-359.99°)
- **Device Format:** 0-255 (256 steps for 360°)
- **Conversion:** Scale to 256 steps
- **Formula:** `device_phase = (sdl_phase * 256 / 36000) & 0xFF`
- **Examples:**
  - 0° (0) → 0x00
  - 90° (9000) → 0x40 (64)
  - 180° (18000) → 0x80 (128)
  - 270° (27000) → 0xC0 (192)

### Period Encoding (Periodic)
- **SDL2 Format:** Milliseconds
- **Device Format:** 16-bit little-endian in 0x04 packet
- **Conversion:** Direct copy (keep in milliseconds, NOT Hz×100!)
- **Examples:**
  - 10ms = 0x000a
  - 50ms = 0x0032
  - 100ms = 0x0064
  - 1000ms = 0x03e8

### Envelope Level Encoding
- **SDL2 Format:** 0-32767 (unsigned)
- **Device Format:** 0-255 (unsigned 8-bit)
- **Conversion:** Scale down
- **Formula:** `device_env = sdl_env * 255 / 32767`
- **Examples:**
  - SDL 0 → Device 0
  - SDL 8000 → Device 6
  - SDL 16000 → Device 12
  - SDL 24000 → Device 18
  - SDL 32767 → Device 255
