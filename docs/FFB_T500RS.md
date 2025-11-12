## T500RS USB FFB Protocol (observed)

This page documents the packet formats and ordering used by the T500RS when driven via the USB interrupt endpoint (0x01 OUT). It mirrors the style of docs/FFBEFFECTS.md for other wheels.

Key rules
- Little‑endian for 16‑bit fields
- DMA‑safe buffer length 32 bytes; typical messages are 2, 4, 9 or 15 bytes
- EffectID semantics (CRITICAL):
  - All Report 0x01 main‑effect uploads MUST use EffectID = 0x00 on T500RS
  - Report 0x41 START/STOP also uses EffectID = 0x00, except the init‑time STOP for autocenter which targets a fixed ID (15)
  - Using per‑effect IDs with 0x01 breaks playback (constant force fails completely)

Report glossary
- 0x01 main upload (15 bytes)
  - Layout (unknown bytes kept for completeness):
    - b0 id = 0x01
    - b1 effect_id = 0x00 (MUST)
    - b2 type: 0x00 constant; 0x20..0x24 periodic/ramp
    - b3 0x40
    - b4 0xff (or duration L for ramp path)
    - b5 0xff (or duration H for ramp path)
    - b6 0x00
    - b7 0xff
    - b8 0xff
    - b9 0x0e (parameter subtype)
    - b10 0x00
    - b11 0x1c (envelope subtype)
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
  - b6..7 period (le16, ms)
- 0x04 ramp params (9 bytes)
  - b0 0x04, b1 0x0e
  - b2..3 start (le16), b4..5 cur_val (le16), b6..7 duration (le16), b8 0x00
- 0x05 condition params (11 bytes)
  - set 1 (coeff/saturation): b0 0x05, b1 0x0e, b2 0x00, b3 right_strength, b4 left_strength, b9/b10 subtype values vary by type (spring vs damper/friction)
  - set 2 (deadband/center): b0 0x05, b1 0x1c, b2 0x00, b3 deadband, b4 center, b9/b10 subtype values
- 0x40 config (4 bytes)
  - 0x40 0x04 enable/disable autocenter, 0x40 0x03 set autocenter strength, 0x40 0x11 set range (le16), etc.
- 0x41 start/stop/clear (4 bytes)
  - id=0x41, effect_id=0x00 (MUST), command=0x41 START or 0x00 STOP, arg=0x01
  - Exception: init‑time STOP for autocenter uses effect_id=15
- 0x42 apply/apply‑like, 0x43 gain

Effect upload and play ordering
- Constant (FF_CONSTANT)
  1) 0x02 envelope
  2) 0x01 main (type=0x00, effect_id=0x00)
  Play: 0x03 level, then 0x41 START (effect_id=0x00)
- Periodic (FF_SINE/FF_SQUARE/FF_TRIANGLE/FF_SAW)
  1) 0x02 envelope
  2) 0x01 main (type=0x20..0x24, effect_id=0x00)
  3) 0x04 periodic params (magnitude/period)
  4) 0x01 main (type repeated, effect_id=0x00)
  Play: 0x41 START (effect_id=0x00)
- Condition (FF_SPRING/FF_DAMPER/FF_FRICTION/FF_INERTIA)
  1) 0x05 coeff/saturation (0x0e)
  2) 0x05 deadband/center (0x1c)
  3) 0x01 main (type reflects subkind, effect_id=0x00)
  Play: 0x41 START (effect_id=0x00)
- Ramp (FF_RAMP)
  1) 0x02 envelope
  2) 0x04 ramp params (device behaves like hold of start level with duration)
  3) 0x01 main (type=0x24, effect_id=0x00)
  Play: 0x41 START (effect_id=0x00)

Notes
- All 0x01 uploads must use EffectID=0x00. This was validated on hardware; using per‑effect IDs causes constant force to fail completely and can break other effects.
- 16‑bit values are little‑endian (lo byte first).
- Magnitudes/levels are expected in device ranges (0..127 for periodic magnitude, s8 for constant level). Scaling helpers in code perform conversions from Linux ranges.
- Autocenter is disabled by zeroing spring coefficients via 0x05 messages and/or via 0x40 commands, then explicitly stopping autocenter (ID 15) during init.

