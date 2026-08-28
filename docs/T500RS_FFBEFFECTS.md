# T500RS Force Feedback Protocol

This document explains, how the driver talks to the Thrustmaster
T500RS racing wheel to make it push, shake, and resist through its force-feedback
(FFB) motor.

The facts here come from USB captures of the official Windows driver.

---

## 1. What force feedback actually is

The T500RS is a steering wheel that can physically push back against your hands.
Instead of *telling* the wheel "push with 40% force", the computer describes a
behaviour - an **effect** - and then asks the wheel to start or stop it.

Different effects feel like different real-world sensations:

| Effect            | What it feels like                                            |
|-------------------|---------------------------------------------------------------|
| Constant force    | A steady push or pull in one direction                       |
| Periodic (sine)   | A smooth, repeating vibration (engine rumble, road texture)   |
| Square / triangle | Sharper, more mechanical vibrations                          |
| Sawtooth / ramp   | A force that rises then drops, or slides one way             |
| Spring            | The wheel is pulled back toward the centre                    |
| Damper            | The wheel gets "thick" and resists being moved                |
| Friction          | A constant drag as you turn                                   |
| Inertia           | Resistance to *changing* direction, as if the wheel were heavy|

In a game, these combine to let you feel the road, a collision, or the weight of
the car.

---

## 2. How the computer talks to the wheel

Communication happens over USB as a stream of tiny **packets**. Each packet is
just a list of bytes (numbers from 0 to 255, written in hexadecimal like `0x01`).
The very first byte of every packet says what *kind* of packet it is.

A few simple conventions used throughout:

- **Hexadecimal** (`0x` prefix) is just another way to write numbers. `0x0e` is 14.
- **Little-endian**: when a value is bigger than 255 and needs two bytes, the
  *small* byte is sent first. So a duration of 500 ms is written `f4 01`
  (because 500 = `0x01f4`, and we send `f4` then `01`).
- **Reserved bytes** are always `0x00`; the wheel ignores them, but they must be
  present to keep the packet the right length.

---

## 3. The life of an effect (the big picture)

Playing any effect follows the same three steps:

1. **Declare it** - send a *main upload* packet (`0x01`) describing what kind of
   effect it is and how long it should last.
2. **Configure it** - send one or more *parameter* packets (`0x02`/`0x03`/`0x04`/`0x05`)
   that set the strength, speed, or shape.
3. **Start it** - send a *command* packet (`0x41`) with START.

To make it stop, send the same `0x41` command with STOP.

### The one rule that trips everyone up

The `effect_id` byte mirrors the **hardware slot** the effect runs on - it is
not always `0x00`:

- Constant force (and every periodic/ramp effect, see paragraph 5.4) runs on
  **slot 0** -> `effect_id = 0x00`.
- Condition effects (spring/damper/friction/inertia) get slots `1, 2, 3, ...`
  assigned sequentially -> their `0x01` uploads and `0x41` START/STOP packets
  carry that slot number (`41 01 41 ff` starts slot 1).

### Why the wheel never stops on its own

Unlike some other wheels, the T500RS has no built-in timer: once an effect is
started, it plays **forever** until an explicit STOP arrives. So the Linux driver
keeps its own software timer and sends the STOP at the right moment. When you read
"the driver enforces the duration", that is what is happening - the wheel itself
won't do it.

---

## 4. How the wheel tells effects apart (slots and subtypes)

The `effect_id` byte names the **hardware slot** an effect runs on, and each
slot owns a pair of **subtypes** (a "parameter subtype" and an "envelope
subtype") that route its parameter packets.

Think of a subtype as a **channel number**:

- **Slot 0** (constant force, and every periodic/ramp effect - see 5.4) uses
  the fixed channels:
  - parameter subtype = `0x0e`
  - envelope subtype  = `0x1c`
- **Condition effects** get slot numbers `n = 1, 2, 3, ...` with channels
  computed by a simple formula:

  ```
  parameter subtype = 0x0e + 0x1c x n
  envelope subtype  = 0x1c + 0x1c x n
  ```

  For example, slot 1 gets `0x2a` and `0x38`. The wheel only cares about these
  numbers matching between the `0x01` packet and the later parameter packets.

These two subtype values are written into bytes 9-12 of the `0x01` packet, and
the parameter packets echo back the same numbers so the wheel knows which effect
they belong to.

---

## 5. The packets

Each section below describes one packet type: its length, what every byte means,
and any gotchas. You do not need to memorise the hex - this is a reference to come
back to.

### 5.1 Main upload - `0x01` (15 bytes)

This declares an effect. Sent first.

| Offset | Size | Field          | Meaning                                                        |
|--------|------|----------------|----------------------------------------------------------------|
| 0      | 1    | packet type    | `0x01`                                                         |
| 1      | 1    | effect_id      | Hardware slot: 0 for constant/periodic, n for conditions (see paragraph 3) |
| 2      | 1    | effect type    | What kind of effect (see table below)                          |
| 3      | 1    | control        | Always `0x40`                                                  |
| 4-5    | 2    | duration       | How long it should run, in milliseconds                        |
| 6-7    | 2    | delay          | Pause before it starts, in milliseconds                        |
| 8      | 1    | reserved       | `0x00`                                                         |
| 9-10   | 2    | parameter sub  | The channel for this effect (see paragraph 4)                           |
| 11-12  | 2    | envelope sub   | The second channel for this effect (see paragraph 4)                    |
| 13-14  | 2    | reserved       | `0x0000`                                                       |

**Effect type codes (byte 2) - the only values ever put on the wire:**

| Code | Effect                        |
|------|-------------------------------|
| 0x00 | Constant force                |
| 0x22 | Sine (host-synthesis carrier, see 5.4) |
| 0x40 | Spring                        |
| 0x41 | Damper / friction / inertia   |

Codes 0x20-0x24 (square/triangle/saw) were once guessed to be waveform
selectors; they are unsourced and per-slot periodic declarations wedge the
firmware. Waveforms other than sine are produced entirely in software and
never appear in a MAIN packet.

**Duration note:** for constant and periodic effects the wheel ignores the
duration and runs until stopped, so the driver sends `0xffff` ("infinite") and
relies on its own timer (paragraph 3). Ramp duration is likewise enforced in
software by the synthesis engine.

### 5.2 Envelope - `0x02` (9 bytes)

An envelope shapes the *edges* of a force: how quickly it fades in (attack) and
out (fade).

| Offset | Size | Field          | Meaning                                  |
|--------|------|----------------|------------------------------------------|
| 0      | 1    | packet type    | `0x02`                                   |
| 1      | 1    | subtype        | The envelope channel from the `0x01` packet |
| 2-3    | 2    | attack length  | Fade-in time, milliseconds               |
| 4      | 1    | attack level   | Fade-in strength, 0-255                  |
| 5-6    | 2    | fade length    | Fade-out time, milliseconds              |
| 7      | 1    | fade level     | Fade-out strength, 0-255                 |
| 8      | 1    | reserved       | `0x00`                                   |

**Note:** the Linux driver applies envelopes to periodic/ramp effects entirely
in software (the synthesis engine shapes the streamed level). For effects the
firmware runs natively (constant, condition), all-zero envelopes are sent -
non-zero values for those types have never been observed on the wire, and the
game's envelope is warned about and dropped.

### 5.3 Constant force - `0x03` (4 bytes)

Sets the actual push/pull of a constant effect.

| Offset | Size | Field       | Meaning                                            |
|--------|------|-------------|----------------------------------------------------|
| 0      | 1    | packet type | `0x03`                                             |
| 1      | 1    | code        | Low byte of the parameter subtype (`0x0e` for constant) |
| 2      | 1    | reserved    | `0x00`                                             |
| 3      | 1    | level       | Force, signed -127 to +127 (positive = rightward pull) |

### 5.4 Level stream - `0x04` (8 bytes)

The firmware has **no periodic waveform engine**. Windows drivers synthesize
periodic/ramp waveforms in software and stream the resulting force level on
the constant-force channel:

```
04 0e 00 00 <level> 00 10 27
```

| Offset | Size | Field      | Meaning                                                  |
|--------|------|------------|----------------------------------------------------------|
| 0      | 1    | packet type| `0x04`                                                   |
| 1      | 1    | code       | Always `0x0e` (the constant-force channel)               |
| 2-4    | 3    | reserved   | `0x00`                                                   |
| 5      | 1    | level      | Signed force, -128 to +127 - the synthesized signal      |
| 6      | 1    | reserved   | `0x00`                                                   |
| 7-8    | 2    | magic      | `0x2710` LE, constant marker                             |

Windows drivers stream these packets continuously (dozens per second) while
synthesized effects play; the level byte is the live signed force signal.

**How this driver plays periodic effects:** the first periodic/ramp upload
declares slot 0 as a sine (`0x22`) MAIN with the constant channels and an
infinite duration, and from then on a software engine computes the waveform
(square/triangle/saw included), applies attack/fade envelopes, sums in any
playing constant force, and streams the combined level with this packet.
Nothing per-effect is ever declared on the wire.

> **Warning:** A per-slot periodic-parameters variant (`04 2a ...`, code != `0x0e`)
> STALLs on this firmware and leaves the wheel wedged until
> re-enumeration. Do not reinvent per-slot periodic packets.

### 5.5 Condition - `0x05` (11 bytes, sent twice)

Used for spring, damper, friction, and inertia. It is sent **twice**: once for the
x axis and once for the Y axis. The T500RS is a single-axis wheel, so the Y packet
is normally all zeros.

| Offset | Size | Field          | Meaning                                     |
|--------|------|----------------|---------------------------------------------|
| 0      | 1    | packet type    | `0x05`                                      |
| 1      | 1    | code           | Subtype (first packet uses parameter sub, second uses envelope sub) |
| 2      | 1    | reserved       | `0x00`                                      |
| 3      | 1    | right coeff    | Stiffness to the right, 0-10                 |
| 4      | 1    | left coeff     | Stiffness to the left, 0-10                  |
| 5-6    | 2    | center         | Where "centre" sits (offset)                 |
| 7-8    | 2    | deadband       | A zone around centre with no force           |
| 9      | 1    | right sat      | Max force to the right, 0-100                |
| 10     | 1    | left sat       | Max force to the left, 0-100                 |

In plain terms: *coefficients* control how strongly the effect responds, *center*
and *deadband* define where the neutral point is, and *saturation* caps the
maximum force so it never gets violent.

### 5.6 Command - `0x41` (4 bytes)

Starts or stops an effect.

| Offset | Size | Field       | Meaning                              |
|--------|------|-------------|--------------------------------------|
| 0      | 1    | packet type | `0x41`                              |
| 1      | 1    | effect_id   | The hardware slot (0 for constant/periodic, n for conditions) |
| 2      | 1    | command     | `0x41` = START, `0x00` = STOP        |
| 3      | 1    | argument    | `0xff` for START, `0x01` for STOP |

### 5.7 Control and sync commands (`0x40`, `0x42`)

Besides effects, the driver sends short control packets: `0x40` configures
behaviour such as the steering range and autocentering, and `0x42` packets are
brief handshake/sync messages the driver sends before periodic uploads (for
example `42 05` and `42 04`). You do not need them to understand the effect
protocol above.

---

## 6. A complete example: constant force

Putting the steps together, here is a real constant-force effect that plays for
about half a second at a low positive force:

```
01 00 00 40 f4 01 00 00 0e 00 1c 00 00 00    # 0x01: declare constant, 500 ms
02 1c 00 00 00 00 00 00 00                   # 0x02: envelope (zeros - required)
03 0e 00 03                                  # 0x03: level +3 (weak push)
41 00 41 01                                  # 0x41: START
... later ...
41 00 00 01                                  # 0x41: STOP
```

Reading it back:
- The `0x01` packet says "constant effect, 500 ms, channels `0x0e`/`0x1c`".
- The `0x02` envelope is all zeros (mandatory for constant force).
- The `0x03` packet sets a small positive level on channel `0x0e`.
- The `0x41` START begins playback; a later `0x41` STOP ends it.

Periodic and condition effects follow the exact same shape - only the effect type
and the parameter packets differ.

---

## 7. Converting values (a plain guide)

Programs on the computer work with large numbers (for example a force from
-32767 to +32767). The wheel expects small numbers (roughly -127 to +127), so the
driver scales everything down. You rarely need the exact math, but here it is for
reference:

| Quantity            | Computer range      | Wheel range     | Conversion (device = ...)        |
|---------------------|---------------------|-----------------|-------------------------------|
| Duration            | milliseconds        | milliseconds    | direct; `0xffff` = infinite    |
| Constant level      | -32767...+32767     | -127...+127     | `level x 127 / 32767`          |
| Synth stream level  | -32767...+32767     | -128...+127     | `level x 127 / 32767` (host-side) |
| Envelope level      | 0-32767             | (applied host-side, 0-100%) | `env / 32767` scale   |
| Condition coeff.    | 0-32767             | 0-10            | `coeff x level% x 10 / 32767`, rounded |
| Condition center    | -32767...+32767     | device units    | `center / 20`                  |
| Condition deadband  | 0-65535             | device units    | `deadband / 65` *(divisor unconfirmed)* |
| Condition saturation| 0-65535             | 0-100           | `sat x 100 / 65535`            |

Periodic magnitude, phase, offset, period and ramp levels no longer appear
in this table: they are consumed by the software synthesis engine and never
travel the wire as separate fields (see 5.4).

**Direction** never reaches the wheel as a number. A wheel has one force
axis, so the driver folds the direction into the level's *sign*
(`sin(dir) < 0` -> negate the level) and always sends full magnitude. The
level must not be scaled by `sin()`: games that encode the force sign as
polar 0/180 degrees (rFactor 2 and other DirectInput titles) land exactly where
`sin()` is zero and would be silenced.

**Sign convention:** both level channels - the native `0x03` packet and
the `0x04` stream - are UAPI-standard: a **positive** byte pulls the wheel
**rightward**, a negative byte pulls leftward. The driver never negates on
its own. One known exception is game-side: rFactor 2 uploads its effects
sign-inverted, so it needs the in-game "FFB invert" (-100%) setting; a
driver cannot detect or special-case a game.

Only the condition deadband divisor is unconfirmed - it works, but the
exact scaling was never checked against a known input/output pair.

---

## 8. Things to watch out for

- **`effect_id` names the hardware slot** (0 for constant/periodic, `n` for
  conditions). Hardcoding `0x00` everywhere breaks per-slot STOPs.
- **Constant force uses fixed subtypes** (`0x0e` / `0x1c`). Giving it a per-effect
  channel breaks level updates.
- **Never send per-slot periodic packets** (`04 2a ...` or a MAIN on condition
  channels with a `0x2x` type): the firmware STALLs them and the wheel wedges
  until re-enumeration.
- **Duration:** send `0xffff` in MAINs for constant/periodic; the driver's
  software timers enforce real durations for everything.
- **The wheel never auto-stops.** Ending an effect is the driver's job, via the
  software-expiry timer (native effects) or the synthesis engine (periodic/ramp).
- **Direction** is folded into the level's *sign* (+/-1, never a magnitude
  scale); it is not a separate field in any packet.
- **Live updates:** only the parameter packets (`0x03`/`0x04`/`0x05`) can be
  changed while an effect plays. Changing duration or delay requires re-uploading
  the whole effect.
