## T500RS USB FFB Protocol (observed from Windows captures)

This page documents the packet formats and ordering used by the T500RS when driven via the USB interrupt endpoint (0x01 OUT). It mirrors the style of docs/FFBEFFECTS.md for other wheels.


Quick reference (Windows parity, at-a-glance)
- EffectID rule: All 0x01 uploads and 0x41 START/STOP use EffectID=0x00
- Command type codes (first byte on interrupt OUT endpoint 0x01):
  - 0x01 = duration/control (main upload)
  - 0x02 = envelope (attack/fade)
  - 0x03 = constant force level
  - 0x04 = periodic/ramp parameters
  - 0x05 = condition (spring/damper/friction/inertia coefficients)
  - 0x41 = START/STOP
  - 0x42 = initialize/reset
  - 0x43 = global gain/autocenter
- Effect type codes in 0x01 byte 2:
  - 0x00 = constant
  - 0x20 = square, 0x21 = triangle, 0x22 = sine
  - 0x23 = sawtooth up, 0x24 = sawtooth down / ramp
  - 0x40 = spring, 0x41 = damper/friction/inertia
- Subtypes per effect index n: param = 0x0e + 0x1c * n; envelope = 0x1c + 0x1c * n; second envelope = first + 0x1c
- Periodic 0x04 uses frequency (Hz*100), not period (ms)
- Upload sequences (per effect):
  - Constant: STOP → 0x02 → 0x01 → 0x03 → START
  - Periodic (sine/square/triangle/saw): STOP → 0x02 → 0x01 → 0x02 → 0x04 → 0x01 → START
  - Ramp: STOP → 0x02 → 0x01 → 0x02 → 0x04 → 0x01 → START
  - Condition (spring/damper/friction/inertia): STOP → 0x05(set1) → 0x05(set2) → 0x01 → START

Key rules
- Little‑endian for 16‑bit fields
- DMA‑safe buffer length 32 bytes; typical messages are 2, 4, 9 or 15 bytes
- EffectID semantics (CRITICAL):
  - All Report 0x01 main‑effect uploads MUST use EffectID = 0x00 on T500RS.
  - Report 0x41 START/STOP also uses EffectID = 0x00, except the init‑time STOP for autocenter which targets a fixed ID (15).
  - If you send non‑zero `effect_id` values on 0x01/0x41 the wheel does **not** reset or crash, but constant force effects produce **no torque at all** and other effects become unreliable. This matches our hardware tests and is why the driver hard‑codes `EffectID=0`.

Why EffectID is always 0x00 on T500RS
-------------------------------------

On the T300/TX/T248 family the `effect id` field in USB reports selects a
hardware effect slot: different IDs can be uploaded and started
independently.

On T500RS, captures from the Windows driver look different:

- Every Report 0x01 upload uses `effect_id = 0x00`.
- Every Report 0x41 START/STOP uses `effect_id = 0x00` as well, except the
  init-time STOP that targets the built-in autocenter at a fixed ID (15).

The wheel still runs several logical effects at the same time, but their
"slots" are encoded in the parameter/envelope subtypes
(`0x0e + 0x1c * n` / `0x1c + 0x1c * n`) instead of in `effect_id`. Trying to
use per-effect IDs on 0x01 breaks constant force completely, which is why the
Linux driver hard-codes `EffectID=0` for all uploads and START/STOP commands.


Special case — constant force subtypes

- For FF_CONSTANT, the device uses fixed subtypes to link the 0x03 level update:
  - Report 0x02 subtype = 0x1c
  - Report 0x01 b9 = 0x0e (parameter), b11 = 0x1c (envelope)
  - Report 0x03 code = 0x0e
- Using per‑effect subtypes for constant breaks level updates (no force felt).

Report glossary
- 0x01 main upload (15 bytes)
  - Layout (unknown bytes kept for completeness):
    - b0 id = 0x01
    - b1 effect_id = 0x00 (MUST; see "EffectID semantics" above for rationale and failure modes)
    - b2 type: 0x00 constant; 0x20..0x24 periodic/ramp
    - b3 0x40
    - b4/b5:
      - constant & periodic: 0xffff (effect runs until an explicit 0x41 STOP; tmff2 enforces `replay.length` in software)
      - ramp: duration in ms (lo/hi), matching Windows captures
    - b6 0x00
    - b7 0xff
    - b8 0xff
    - b9 param subtype = 0x0e + 0x1c * index
    - b10 0x00
    - b11 envelope subtype = 0x1c + 0x1c * index
    - b12 0x00
    - b13 0x00
    - b14 0x00
- 0x02 envelope (9 bytes)
  - b0 0x02, b1 0x1c, b2 0x00
  - b3..4 attack_length (le16)
  - b5 attack_level (0..255)
  - b6..7 fade_length (le16)
  - b8 fade_level (0..255)
- 0x03 constant level (4 bytes)
  - b0 0x03, b1 0x0e, b2 0x00
  - b3 level s8 (−127..127)
- 0x04 periodic params (8 bytes)
  - b0 0x04, b1 0x0e, b2 0x00
  - b3 magnitude u7 (0..127)
  - b4 offset = 0, b5 phase = 0
  - b6..7 frequency (le16, Hz*100)  (e.g., 10 Hz → 0x03e8)
- 0x04 ramp params (9 bytes)
  - b0 0x04, b1 0x0e
  - b2..3 start (le16), b4..5 cur_val (le16), b6..7 duration (le16), b8 0x00
- 0x05 condition params (11 bytes)
  - set 1 (coeff/saturation): b0 0x05, b1 0x0e, b2 0x00, b3 right_strength, b4 left_strength, b9/b10 subtype values vary by type (spring vs damper/friction)
  - set 2 (deadband/center): b0 0x05, b1 0x1c, b2 0x00, b3 deadband, b4 center, b9/b10 subtype values
- 0x40 config (4 bytes)
  - 0x40 0x04 enable/disable autocenter, 0x40 0x03 set autocenter strength, 0x40 0x11 set range (le16), etc.
- 0x41 start/stop/clear (4 bytes)
  - id=0x41, effect_id=0x00 (MUST; same rule as above — non‑zero IDs leave constant force mute/misbehaving), command=0x41 START or 0x00 STOP, arg=0x01
  - Exception: init‑time STOP for autocenter uses effect_id=15
- 0x42 apply/apply‑like, 0x43 gain

Control and initialization commands (0x40/0x41/0x42/0x43)
--------------------------------------------------------

These small fixed-size packets control range, autocenter, gain and effect
lifecycle. Their exact values come from Windows USB captures but are stable
across games and control panel tests.

- 0x40 config (4 bytes)
  - General layout: `40 aa bb cc`
  - Observed patterns:
    - `40 04 00 00` — disable autocenter
    - `40 04 01 00` — enable autocenter
    - `40 03 xx 00` — set autocenter strength (`xx` = 0..100 in Linux; Windows uses 0..7f)
    - `40 11 vv vv` — range / enable:
      - On init: `40 11 42 7b` (magic value that enables FFB)
      - For range changes: `40 11 value_lo value_hi` with `value = range_degrees * 60` (le16),
        e.g. 360° → 0x5460
  - These commands do not involve `effect_id`; they configure the base.

- 0x41 start/stop (4 bytes)
  - Layout: `41 id cmd arg`
    - `id`  = effect slot (always 0 on T500RS, except autocenter stop which
      uses 15)
    - `cmd` = 0x00 STOP, 0x41 START
    - `arg` = 1 (normal use on T500RS)
  - Examples:
    - `41 00 00 01` — STOP current effect on slot 0 (used before uploads)
    - `41 00 41 01` — START current effect on slot 0
    - `41 0f 00 01` — STOP built-in autocenter at init time

- 0x42 initialize/apply (2 bytes)
  - Layout: `42 ss`
    - `42 05` — simple "apply settings" helper:
      - Sent once at init to bring the base into a known state
      - Sent after changing autocenter or steering range
  - No extra payload; `ss` selects the subcommand.

- 0x43 gain helper (2 bytes)
  - Layout: `43 gg`
  - In init, Linux sends `43 ff` once to set maximum device gain.
  - The FFB gain callback rescales Linux gain 0..65535 into one byte `gg` and
    sends `43 gg`.
  - Higher-level code still exposes the standard FFB gain control; this report
    is how the hardware receives the gain value.


Effect upload and play ordering

- Subtype system (applies to 0x01/0x02/0x04/0x05 references)
  - Envelope subtype base = 0x1c; Parameter subtype base = 0x0e
  - For effect index n: envelope = 0x1c + 0x1c * n; parameter = 0x0e + 0x1c * n
  - In 0x01: b9 = parameter subtype; b11 = envelope subtype

- Global (Windows parity)
  - Precede uploads with 0x41 STOP (command=0x00, arg=0x01), effect_id=0x00, to clear state

- Constant (FF_CONSTANT)
  1) 0x41 STOP
  2) 0x02 envelope (subtype = 0x1c, fixed for constant)
  3) 0x01 main (b9=0x0e, b11=0x1c)
  4) 0x03 level
  Play: 0x41 START (effect_id=0x00)

  Windows captures show an extra 0x02 + 0x01 pair at the end of the
  sequence, but with constant's fixed subtypes the wheel behaves the same,
  so the Linux driver uses the simpler 0x02 -> 0x01 -> 0x03 sequence.

- Periodic (FF_SINE/FF_SQUARE/FF_TRIANGLE/FF_SAW)
  1) 0x41 STOP
  2) 0x02 envelope (first)
  3) 0x01 main (first; type=0x20..0x24, effect_id=0x00)
  4) 0x02 envelope (second; subtype += 0x1c)
  5) 0x04 periodic params (magnitude/frequency)
  6) 0x01 main (second; type repeated)
  Play: 0x41 START (effect_id=0x00)

- Condition (FF_SPRING/FF_DAMPER/FF_FRICTION/FF_INERTIA)
  1) 0x41 STOP
  2) 0x05 coeff/saturation (param subtype = 0x0e + 0x1c * index)
  3) 0x05 deadband/center (envelope subtype = 0x1c + 0x1c * index)
  4) 0x01 main (type reflects subkind, effect_id=0x00; b9/b11 reference above)
  Play: 0x41 START (effect_id=0x00)

- Ramp (FF_RAMP)
  1) 0x41 STOP
  2) 0x02 envelope (first)
  3) 0x01 main (first; type=0x24, effect_id=0x00)
  4) 0x02 envelope (second; subtype += 0x1c)
  5) 0x04 ramp params (device behaves like hold of start level with duration)
  6) 0x01 main (second)
  Play: 0x41 START (effect_id=0x00)

Worked examples (packet-level)
------------------------------

The following example shows the constant-force upload and play sequence in the
same "bytes + comments" style as docs/FFBEFFECTS.md. Values are illustrative,
not the only valid ones, but they match the layouts described above and the
Linux driver implementation.

### Example: FF_CONSTANT upload + START (effect index 0)

1) Pre-upload STOP (clear slot 0):

```
41 00 00 01
41 - report id (0x41)
00 - effect_id (slot 0, required on T500RS)
00 - command = STOP/CLEAR
01 - arg = 1
```

2) Envelope (Report 0x02, first):

```
02 1c 00 00 00 00 00 00 00
02 - report id
1c - envelope subtype for index 0
00 - reserved
00 00 - attack_length
00 - attack_level
00 00 - fade_length
00 - fade_level
```

3) Main upload (Report 0x01, first):

```
01 00 00 40 ff ff 00 ff ff 0e 00 1c 00 00 00
01 - id
00 - effect_id (slot 0)
00 - type = constant
40 - control byte (matches Windows driver)
ff ff 00 ff ff - duration/flags as observed on Windows
0e - parameter subtype (constant path)
00 - reserved
1c - envelope subtype
00 00 00 - padding / unused
```

4) Constant level (Report 0x03):

```
03 0e 00 40
03 - id (constant level)
0e - code (links to constant subtype)
00 - reserved
40 - level (+64 in s8)
```

On Windows you can see an extra 0x01 with the same layout at the end of
the sequence, but with constant's fixed subtypes the Linux driver omits
that final 0x01 and just uses `0x02 -> 0x01 -> 0x03`.

5) Play the effect (Report 0x41 START):

```
41 00 41 01
41 - report id
00 - effect_id (slot 0)
41 - command = START
01 - arg = 1
```

This demonstrates why the documentation insists on EffectID=0x00: the
per-effect index lives in the subtypes, while the effect slot itself is always
0 on T500RS.

### Example: FF_SINE periodic (Control Panel "boing")

Sequence taken from a Windows USB capture of the Control Panel "boing"
effect. It shows a sine-wave periodic effect with a slightly more complex
envelope and the dual-0x01 / dual-0x02 pattern.

1) Envelope (index 0):

```
02 1c 00 95 00 3f e5 01 00
02 - envelope command
1c - envelope subtype for index 0
00 95 00 - attack length (24-bit LE, milliseconds; default used by Windows)
3f      - attack level (~63/127)
e5 01 00 - fade length (24-bit LE, milliseconds)
00      - fade level
```

2) STOP before the main upload:

```
41 00 00 01
```

3) Main upload (index 0, first):

```
01 00 22 40 bc 02 00 2c 01 0e 00 1c 00 00 00
01 - main upload
00 - effect_id (slot 0)
22 - type = sine wave (see effect type table above)
40 - control/flags
bc 02 - duration (0x02bc ≈ 700 ms)
00 2c 01 - additional flags/fields as seen in captures
0e - parameter subtype for index 0
00 - reserved
1c - envelope subtype for index 0
00 00 00 - padding
```

4) Second envelope (index 1) and periodic params:

```
02 38 00 95 00 3f e5 01 00
02 - envelope command
38 - envelope subtype for index 1 (0x1c + 0x1c * 1)
...

04 2a 00 20 00 00 21 00
04 - periodic params
2a - parameter subtype for index 1 (0x0e + 0x1c * 1)
00 - reserved
20 00 - magnitude (0x0020 → medium strength)
00 - offset
21 00 - frequency (0x0021 → ≈ 0.33 Hz, since value = Hz*100)
```

5) Main upload (index 1, second) and START:

```
01 01 22 40 bc 02 00 2c 01 2a 00 38 00 00 00
...
41 01 41 01
```

In Linux we always use `effect_id = 0` for the 0x01/0x41 packets, but keep the
same subtype pattern `(param = 0x0e + 0x1c*n, envelope = 0x1c + 0x1c*n)`.

### Example: FF_RAMP (linear ramp)

From a Windows USB capture of a linear ramp effect:

1) Envelope:

```
02 1c 00 00 00 00 00 00 00
```

2) Ramp parameters (Report 0x04):

```
04 0e 00 00 00 00 69 23
04 - ramp/periodic command
0e - parameter subtype (index 0)
00 - reserved
00 00 - start level
00 00 - "current" level (same as start for a simple ramp)
69 23 - duration in ms (0x2369 ≈ 9.1 s) used by Windows
```

3) Main upload:

```
01 00 24 40 69 23 00 ff ff 0e 00 1c 00 00 00
01 - main upload
00 - effect_id
24 - type = sawtooth down / ramp
40 - control/flags
69 23 - duration in ms; Linux fills this from `replay.length` for ramp, but uses 0xffff for constant/periodic and relies on a software timer + 0x41 STOP.
...
```

### Example: Condition spring (FF_SPRING)

From a Windows USB capture of a spring effect (Control Panel test):

1) Condition coefficients (Report 0x05, first packet):

```
05 0e 00 64 64 00 00 00 00 54 54
05 - condition command
0e - parameter subtype (index 0)
00 - reserved
64 - positive coefficient (100)
64 - negative coefficient (100)
00 00 00 00 - reserved
54 54 - positive/negative saturation bytes (spring pattern)
```

2) Deadband/center (Report 0x05, second packet):

```
05 1c 00 00 00 00 00 00 00 46 54
05 - condition command
1c - envelope subtype (index 0)
00 - reserved
00 00 - center (0)
00 00 00 - reserved
00 - deadband
46 54 - tail bytes; differ between spring/damper/friction/inertia in captures
```

3) Main upload and START:

```
01 00 40 40 17 25 00 ff ff 0e 00 1c 00 00 00
01 - main upload, type 0x40 = spring
...
41 00 41 01
```

This matches the general condition sequence described earlier:
STOP → 0x05 (coeff/sat) → 0x05 (deadband/center) → 0x01 → START, with the
per-effect index encoded only in the subtypes, not in `effect_id`.




Live update behavior (parameter‑only; Windows parity)

- Overview
  - Parameter updates do not re‑upload effects or send STOP; they modify parameters in place.
  - This reduces USB traffic and avoids momentary dropouts during gameplay.
  - 0x01/0x41 still follow the EffectID=0 rule; 0x04/0x05 carry subtypes, not effect IDs.

- Constant (FF_CONSTANT)
  - Send only 0x03 with new level; code must be 0x0e (fixed linkage to constant path).
  - No 0x41 STOP and no 0x01 re‑upload for updates.

- Periodic (FF_SINE/FF_SQUARE/FF_TRIANGLE/FF_SAW)
  - Send only 0x04 with new magnitude and frequency.
  - Frequency field is Hz*100 (e.g., 10 Hz → 1000); offset/phase remain 0 unless changed.
  - Use per‑effect param_sub = 0x0e + 0x1c * index.

- Condition (FF_SPRING/FF_DAMPER/FF_FRICTION/FF_INERTIA)
  - Send 0x05 (coeff/saturation) with param_sub, then 0x05 (deadband/center) with env_sub_first.
  - Subtypes are the same as in the upload path; spring uses subtype bytes 0x54, others 0x64 for the tail fields.
  - No 0x01 re‑upload.

- Ramp (FF_RAMP)
  - Send only 0x04 (start/cur_val/duration, last byte 0x00) using param_sub.
  - Device behavior approximates ramp via holding/stepping the start value over duration; matching observed Windows behavior.

Failure modes and common pitfalls
---------------------------------

These are the places where ignoring the rules above does not simply "do nothing" but leads to confusing behaviour on real hardware.

- EffectID ≠ 0 on 0x01/0x41
  - Symptom: constant force produces **no torque at all** and other effects become unreliable.
  - What still works: the wheel keeps enumerating and does not reset/crash; packets are accepted.
  - Mitigation: always use `effect_id = 0x00` on T500RS for 0x01 uploads and 0x41 START/STOP. The driver hard‑codes this.

- Changing constant subtypes (0x02/0x01/0x03)
  - Symptom: 0x03 level updates stop affecting the rim; games appear to upload constants but you never feel them.
  - Cause: T500RS uses a fixed linkage for constant (`0x02 subtype = 0x1c`, `0x01 b9 = 0x0e`, `0x01 b11 = 0x1c`, `0x03 code = 0x0e`). Using per‑effect subtypes breaks that linkage.
  - Mitigation: treat the constant path as single‑slot and keep those subtype bytes exactly as documented.

- Relying on hardware duration for constant/periodic
  - Symptom: if you program a finite duration into 0x01 b4/b5 and **do not** send a 0x41 STOP from software, effects can keep running much longer than expected.
  - Cause: captures show Windows using `0xffff` for constant/periodic and managing lifetimes via explicit STOP; non‑0xffff semantics are not documented and appear to be ignored or device‑specific.
  - Mitigation: for constant/periodic, use `b4/b5 = 0xffff` and enforce `replay.length` in software with a STOP when your timer expires (the Linux driver follows this model).

- Forgetting 0x42 05 after range/autocenter changes
  - Symptom: new range or autocenter values sometimes do not take full effect until another operation happens.
  - Cause: `0x42 05` acts as a generic "apply settings" trigger for previous 0x40 commands.
  - Mitigation: after sending `0x40 11` (range) or `0x40 03/04` (autocenter), follow up with a `42 05` packet, as the driver does.



Notes
- All 0x01 uploads must use EffectID=0x00. This was validated on hardware; using per‑effect IDs causes constant force to fail completely and can break other effects.
- 16‑bit values are little‑endian (lo byte first).
- Magnitudes/levels are expected in device ranges (0..127 for periodic magnitude, s8 for constant level). Scaling helpers in code perform conversions from Linux ranges; for FF_CONSTANT the helper also folds `ff_effect.direction` into the signed level (projection onto the wheel axis) before sending 0x03.
- Autocenter is disabled by zeroing spring coefficients via 0x05 messages and/or via 0x40 commands, then explicitly stopping autocenter (ID 15) during init.

