# Force Feedback Effects - Thrustmaster T300RS and compatible

Here I will gather all info I can find out about the different force feedback
effects and their USB commands. All values seem to be little-endian.

Also, general note, it seems like some effects are affected by the direction of
the force, denoted by `***`. As far as I can tell, `fade_level` and
`attack_level` values are calculated by mapping the `u16` into `s16` and taking
the absolute value, and after that multiplying by the direction.

A full list of wheels that use USB packets in the format documented here is yet
to be determined, but at least T300, T248 and TX seem to use it. Each wheel may
have model specific peculiarities, though.

> **NOTE:** All values are examples of actual commands that I captured on the
> USB interface, not the only available ones.

## GENERAL
### Playing a single effect once
```
    60 00 - standard header
    01 - ID
    89 - playing options
    01 - play
    00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
```

### Playing a single effect multiple or infinite times
```
    60 00 - standard header
    01 - ID
    89 - playing options
    41 - play with count
    00 00 00 00 - play count, 0 for infinite, seems to be u16
    00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
```

### Stopping/removing (why are they rolled into one?)
```
    60 00 - standard header
    01 - ID
    89 - playing options
    00 - stop
    00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
```

## Open:
```
    60 01 - standard header?
    04 - ?
    00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00

    (this command doesn't appear every time, odd)
    60 12 - ?
    bf 04 00 00 03 b7 1e - ? /* wow, this is different on different wheels? */
    00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00

    60 01 - ?
    05 - ?
    00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
```

## Close:
```
    60 01 - standard header?
    00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
```

## Rotation angle:
```
    60 08 - header plus settings?
    11 - set rotation angle
    55 d5 - angle (between ff ff and 7b 09, one degree is roughly 3c)
    00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
```

## FF_AUTOCENTER:
```
    60 08 - header plus settings?
    03 - set autocenter force
    8f 02 - force (between ff ff and 00 00)
    00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
```

## FF_GAIN:
```
    60 02 - header plus gain?
    bf - gain (between ff and 00)
    00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
```

## FF_CONSTANT
### Initialization
```
    60 00 - standard header
    01 - ID
    6a - new constant effect(?)
    fe ff - strength between ff bf (-16385) and fd 3f (16381) ***
    00 00 - attack_length
    00 00 - attack_level ***
    00 00 - fade_length
    00 00 - fade_level ***
    00 - effect type?
    4f
    f7 17 - time in milliseconds, unsigned
    00 00
    07 00 - offset from start(?)
    00 ff ff - end of new effect(?)
    00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
```

### Modifying (For dynamic updating of effects)

```
4th byte 5th byte  hex
00001010          0a    - magnitude
01001001          49    - duration / offset / duration + offset
00110001 10000001 31 81 - envelope attack length
00110001 10000010 31 82 - envelope attack level
00110001 10000100 31 84 - envelope fade length
00110001 10001000 31 88 - envelope fade level
00101001          29    - envelope (all)
00101010          2a    - magnitude + envelope
01101010          6a    - magnitude + envelope + offset
00110010          32    - magnitude + envelope attack length & level
01110010          72    - magnitude + envelope attack length & level + duration
01101010          6a    - magnitude + envelope + duration + offset
```

#### Constant force:
```
    60 00 - standard header
    01 - ID
    0a - modify constant force
    05 16 - constant force between ff bf (-16385) and fd 3f (16381) ***
    00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
```

#### Envelope:
```
envelope attack/fade level/length:
    multiple values can be updated with one packet

    60 00 - standard header
    01 - ID
    31 85
    e8 03 - attack length
    dc 05 - fade length
    00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00

envelope (all):
    60 00 - standard header
    01 - ID
    29
    e8 03 - attack length
    cc 0c - attack level
    dc 05 - fade length
    cc 0c - fade level
    00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
```

#### Duration:
```
    60 00 - standard header
    01 - ID
    49
    00 - effect type?
    45 - update type
    88 13 - duration in milliseconds
    00 00 - offset in milliseconds
    00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
```

#### Combined:
```
magnitude + envelope:
    60 00 - standard header
    01 - ID
    2a
    fd 3f - magnitude
    e8 03 - attack length
    cc 0c - attack level
    dc 05 - fade length
    cc 0c - fade level
    00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00

magnitude + envelope attack length & level + duration:
    60 00 - standard header
    01 - ID
    72
    fd 3f - magnitude
    83 - envelope update type
    e8 03 - attack length
    cc 0c - attack level
    00 - effect type?
    41 - update type
    88 13 - duration
    00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00

magnitude + envelope + duration + offset:
    60 00 - standard header
    01 - ID
    6a
    fd 3f - magnitude
    e8 03 - attack length
    cc 0c - attack level
    dc 05 - fade length
    cc 0c - fade level
    00 - effect type?
    45 - update type
    88 13 - duration
    00 00 - offset
    00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
```

## FF_RAMP

```
00001110 00000001 0e 01 - slope
00001110 00000010 0e 02 - center
01001001          49    - invert
00001110 00000011 0e 03 - slope + center
01001110 00000001 4e 01 - slope + invert
01001110 00000011 4e 03 - slope + center + invert
01001110 00001000 4e 08 - duration
01001001          49    - offset
01001110 00001000 4e 08 - duration + offset
00110001 10000001 31 81 - envelope attack length
00110001 10000010 31 82 - envelope attack level
00110001 10000100 31 84 - envelope fade length
00110001 10001000 31 88 - envelope fade level
00101001          29    - envelope (all)
01110110 00000011 76 03 - envelope attack & fade length + slope + center + invert
00101110 00000011 2e 03 - envelope + slope + center + invert
01101110 00001011 6e 0b - duration + offset + envelope + slope + center + invert
```

### Notes

#### Invert

There is a bug in the Windows driver during the Ramp effect update.
If the update requires inverting the effect and the duration or the offset is
not changed, the effect just stops.

This can be fixed by adding the `update type` byte with the value of `40`.

Affected examples:
 - slope + invert
 - slope + center + invert
 - invert
 - envelope attack & fade length + slope + center + invert
 - envelope + slope + center + invert

Example:
```
Invalid:
    60 00 - standard header
    01 - ID
    49
    05 - effect type?
    00 <- missing "update type"
    [...]

Valid:
    60 00 - standard header
    01 - ID
    49
    05 - effect type?
    40 - update type <- correct update type
    [...]
```


### Init:
```
    60 00 - standard header
    01 - ID
    6b - new ramp effect
    f6 7f - slope *** abs(start - stop) / 2
    fe ff - center *** (start + end) / 2 (signed)
    00 00
    f7 17 - time
    00 80 - some kind of marker
    00 00 - attack_length
    f6 7f - attack_level ***
    00 00 - fade_length
    f6 7f - fade_level***
    04 - invert (going "down", 05, going "up", 04)
    4f
    f7 17 - time again?
    00 00
    00 00 - offset
    00 ff ff - end of init
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
```

### Modifying:

```
center:
    60 00 - standard header
    01 - ID
    0e 02
    1e 40 - center
    00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00

slope + invert:
    This packet is affected by the "invert" driver bug. See Notes above.

    60 00 - standard header
    01 - ID
    4e 01
    20 00 - slope
    04 - effect type? (invert) 04 or 05
    00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00

slope + center + invert:
    This packet is affected by the "invert" driver bug. See Notes above.

    60 00 - standard header
    01 - ID
    4e 03
    06 00 - slope
    25 40 - center
    04 - effect type? (invert) 04 or 05
    00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
```

#### Duration, offset, invert:

```
duration:
    60 00 - standard header
    01 - ID
    4e 08
    88 13 - duration
    04 - effect type? must reflect the actual inversion 04 or 05
    41 - update type, see Update type section
    88 13 - duration
    00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00

offset:
    60 00 - standard header
    01 - ID
    49
    04 - effect type? must reflect the actual inversion 04 or 05
    44 - update type, see Update type section
    00 00 - offset
    00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00

duration + offset:
    60 00 - standard header
    01 - ID
    4e 08
    88 13 - duration
    04 - effect type? must reflect the actual inversion 04 or 05
    45 - update type, see Update type section
    88 13 - duration
    00 00 - offset
    00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00

dir + offset:
    60 00 - standard header
    01 - ID
    49
    05 - effect type? must reflect the actual inversion 04 or 05
    44 - update type, see Update type section
    00 00 - offset
    00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00

invert:
    This packet is affected by the "invert" driver bug. See Notes above.

    60 00 - standard header
    01 - ID
    49
    05 - effect type? invert 04 or 05
    00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
```

#### Envelope:

```
envelope attack/fade level/length:
    multiple values can be updated with one packet

    60 00 - standard header
    01 - ID
    31 85
    d0 07 - attack length
    dc 05 - fade length
    00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00

envelope (all):
    60 00 - standard header
    01 - ID
    29
    d0 07 - attack length
    69 34 - attack level
    dc 05 - fade length
    32 1a - fade level
    00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
```

#### Combined:

```
envelope attack & fade length + slope + center + invert:
    This packet is affected by the "invert" driver bug. See Notes above.

    60 00 - standard header
    01 - ID
    76 03
    06 00 - slope
    25 40 - center
    85 - envelope update type
    d0 07 - attack length
    dc 05 - fade length
    04 - effect type? (invert) 04 or 05
    00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00

envelope + slope + center + invert:
    This packet is affected by the "invert" driver bug. See Notes above.

    60 00 - standard header
    01 - ID
    6e 03
    06 00 - slope
    25 40 - center
    d0 07 - attack length
    69 34 - attack level
    dc 05 - fade length
    32 1a - fade level
    04 - effect type? (invert) 04 or 05
    00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00

duration + offset + envelope + slope + center + invert:
    60 00 - standard header
    01 - ID
    6e 0b
    06 00 - slope
    25 40 - center
    88 13 - length
    d0 07 - attack length
    69 34 - attack level
    dc 05 - fade length
    32 1a - fade level
    04 - effect type? (invert) 04 or 05
    45 - update type, see Update type section
    88 13 - length 2
    00 00 - offset
    00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00

```

## FF_DAMPER + FF_FRICTION + FF_INERTIA + FF_SPRING:
### Init:
```
    60 00 - standard header
    02 -ID
    64 - new conditional effect (sometimes e4)
    fc 7f - positive coefficient (right)
                between 01 80 (-32767) and fc 7f (32764)
    fc 7f - negative coefficient (left)
                between 01 80 (-32767) and fc 7f (32764)
    fe ff - deadband right (offset + deadband)
                between 01 80 (-32767) and ff 7f (32767)
    fe ff - deadband left (offset - deadband)
                between 01 80 (-32767) and ff 7f (32767)
    fc 7f - positive saturation (right)
                between 00 00 and fc 7f (32764) or a6 6a (27302)
    fc 7f - negative saturation (left)
                between 00 00 and fc 7f (32764) or a6 6a (27302)
    fe ff fe ff fe ff fe ff - some weird hard-coded
                              values to do with friction?
    fc 7f - max positive saturation (type 07 - fc 7f
                                     type 06 - a6 6a)
    fc 7f - max negative saturation (type 07 - fc 7f
                                     type 06 - a6 6a)
    07 - type (damper, friction, inertia - 07
                                  spring - 06)
    4f
    f7 17 - duration
    00 00
    00 00 - offset
    00 ff ff - end of init
    00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
```

### Modifying:

```
4th byte 5th byte   hex
00001110 01000001  0e 41  positive coefficient
00001110 01000010  0e 42  negative coefficient
00001110 01000011  0e 43  both     coefficient
00001110 01001100  0e 4c  right and left deadband
00001110 01010000  0e 50  positive saturation
00001110 01100000  0e 60  negative saturation
00001110 01110000  0e 70  both     saturation
00001110 01001111  0e 4f  coefficient + deadband
00001110 01111100  0e 7c  deadband    + saturation
00001110 01110011  0e 73  coefficient + saturation
00001110 01101101  0e 6d  positive coefficient + deadband + negative saturation
01001110 01000001  4e 41  positive coefficient + duration
00001100           0c     all effect specific parameters
01001001           49     duration
01001100           4c     all effect specific parameters + duration
```

Any combination that makes sense is possible, not restricted to the list above.

The order of the fields are defined in the `all effect specific parameters + duration` packet example below.

```
    positive coefficient:
    60 00
    02 - ID
    0e 41
    64 35 - value, signed, between 01 80 (-32767) and fc 7f (32764)
    00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00

    negative coefficient:
    60 00
    02 - ID
    0e 42
    64 35 - value, signed, between 01 80 (-32767) and fc 7f (32764)
    00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00

    positive and negative coefficient:
    60 00
    01 - ID
    0e 43
    ab 67 - right coefficient, signed, between 01 80 (-32767) and fc 7f (32764)
    50 18 - left coefficient, signed, between 01 80 (-32767) and fc 7f (32764)
    00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00

    deadband/offset:
    60 00
    02 - ID
    0e 4c
    64 35 - deadband right (offset + deadband)
                between 01 80 (-32767) and ff 7f (32767)
    00 00 - deadband left (offset - deadband)
                between 01 80 (-32767) and ff 7f (32767)
    00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00

    positive saturation
    60 00
    01 - ID
    0e 50
    fc 7f - value between 00 00 and fc 7f or a6 6a (defined in init)
    00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00

    negative saturation
    60 00
    01 - ID
    0e 60
    fc 7f - value between 00 00 and fc 7f or a6 6a (defined in init)
    00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00

    positive and negative saturation
    60 00
    01 - ID
    0e 70
    da 5d - right saturation between 00 00 and fc 7f or a6 6a (defined in init)
    b9 0b - left saturation between 00 00 and fc 7f or a6 6a (defined in init)
    00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00

    all effect specific parameters
    60 00
    01 - ID
    0c
    fe 1f - positive coefficient
    98 19 - negative coefficient
    96 59 - right deadband
    fe 3f - left deadband
    65 26 - positive saturation
    98 19 - negative saturation
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00

    all effect specific parameters + duration
    60 00
    01 - ID
    4c
    30 73 - positive coefficient
    30 73 - negative coefficient
    ff 7f - right deadband
    fc ff - left deadband
    fd 5f - positive saturation
    fd 5f - negative saturation
    06 - effect type (condition)?
    45 - update type, see Update type section
    10 27 - length in milliseconds
    00 00 - offset in milliseconds
    00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00

```

### Duration:
```
    60 00 - standard header
    01 - ID
    49
    06 - effect type (condition)?
    45 - update type, see Update type section
    6c 20 - length in milliseconds
    00 00 - offset in milliseconds
    00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
```

## FF_PERIODIC:

### Init
```
    60 00 - standard header
    01 - ID
    6b - new periodic effect
    00 00 - magnitude between 00 00 and fc 7f (32764) ***
    fe ff - offset (up/down) between ff bf (-16385) and fd 3f (16381)
    00 00 - phase (left/right) between 00 00 and 4a f7 (32586)
            meaning is 0 to ~359 deg phase shift in 5b steps
    e8 03 - period between 00 00 and ff ff (in milliseconds)
    00 80
    00 00 - attack_length
    00 00 - attack_level ***
    00 00 - fade_length
    00 00 - fade_level ***
    01 - type of periodic ( square 01,
                            sine 03,
                            triangle 02,
                            sawtooth down 05,
                            sawtooth up 04  )
    4f
    f7 17 - duration
    00 00
    00 00 - offset
    00 ff ff - end of init
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
```

### Modifying:

```
4th byte 5th byte  hex
00001110 00000001 0e 01 - magnitude
00001110 00000010 0e 02 - offset
00001110 00000100 0e 04 - phase
00001110 00001000 0e 08 - period
00001110 00001111 0e 0f - magnitude + offset + phase + period
01001001          49    - duration / offset / duration + offset
00110001 10000001 31 81 - envelope attack length
00110001 10000010 31 82 - envelope attack level
00110001 10000100 31 84 - envelope fade length
00110001 10001000 31 88 - envelope fade level
00101001          29    - envelope (all)
00101110 00001111 2e 0f - magnitude + offset + phase + period + envelope
00110110 00001111 36 0f - magnitude + offset + phase + period + envelope attack length & level
01101110 00001111 6e 0f - magnitude + offset + phase + period + envelope + duration + offset
```

```
magnitude:
    60 00
    02 - ID
    0e 01
    64 35 - value, between 00 00 and fc 7f (32764)
    00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00

magnitude + offset + phase + period:
    60 00
    01 - ID
    0e 0f
    dd 3f - magnitude, between 00 00 and fc 7f (32764)
    cb 0c - offset, between ff bf (-16385) and fd 3f (16381)
    5a 00 - phase, between 00 00 and 4a f7 (32586) in 5b steps
            meaning is 0 to 359 deg phase shift
    f8 2a - period, between 00 00 and ff ff (in milliseconds)
    00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00

```

#### Envelope:
```
envelope attack/fade level/length:
    multiple values can be updated with one packet

    60 00 - standard header
    01 - ID
    31 85
    d0 07 - attack length
    dc 05 - fade length
    00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00

envelope (all):
    60 00 - standard header
    01 - ID
    29
    d0 07 - attack length
    69 34 - attack level
    dc 05 - fade length
    32 1a - fade level
    00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
```

#### Duration:
```
    60 00
    01 - ID
    49
    03 - effect type 01 - Square
                     02 - Triangle
                     03 - Sine
                     04 - Sawtooth up
                     05 - Sawtooth down
    45 - update type, see Update type section
    74 27 - duration
    00 00 - offset
    00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
```

#### Combined

```
magnitude + offset + phase + period + envelope:
    60 00
    01 - ID
    2e 0f
    dd 3f - magnitude, between 00 00 and fc 7f (32764)
    cb 0c - offset
    5a 00 - phase
    f8 2a - period
    dc 05 - attack length
    30 73 - attack level
    e8 03 - fade length
    30 73 - fade level
    00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00

magnitude + offset + phase + period + envelope attack length & level:
    60 00
    01 - ID
    36 0f
    dd 3f - magnitude
    cb 0c - offset
    5a 00 - phase
    f8 2a - period
    83 - envelope update type
    e8 03 - attack length
    cc 0c - attack level
    00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00

magnitude + offset + phase + period + envelope + duration + offset:
    60 00
    01 - ID
    6e 0f
    dd 3f - magnitude
    cb 0c - offset
    5a 00 - phase
    f8 2a - period
    cc 0c - attack length
    e8 03 - attack level
    dc 05 - fade length
    cc 0c - fade level
    03 - effect type 01 - Square
                     02 - Triangle
                     03 - Sine
                     04 - Sawtooth up
                     05 - Sawtooth down
    45 - update type, see Update type section
    74 27 - duration
    00 00 - offset
    00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
```

## Envelope

Envelope related parameters are the same across all the effects. The only
difference is which effect specific parameters are used during the calculations.

Damper, Friction, Inertia, and Spring do not support envelope.

### attack length

between 0 and 65535

On Windows, it has a minimum value depending on how far is the target from the
attack/fade level. This means that in case of an envelope that goes from zero
to max has the minimum length of 256ms.

```length = abs((effectval * 32768 / effectmax - level) / 128)```

`effectval` is the current value of an effect specific parameter

`effectmax` is the maximum value of `effectval`

`level` is the current value of the attack/fade level

Used parameters for each effect:

| Effect   | Parameter |
|----------|-----------|
| Constant | Strength  |
| Periodic | Magnitude |
| Ramp     | Slope     |

### attack level

between 0 and 32764

### fade length

between 0 and 65535

Uses the same calculation as attack length.

### fade level

between 0 and 32764


## Update type

```
binary    hex
01000001  41  duration
01000100  44  offset
01000101  45  duration + offset
```

## PS4 Input `rdesc`
```
    ff00.0021 = 0
    ff00.0021 = 0
    ff00.0021 = 0
    ff00.0021 = 0
    ff00.0021 = 0
    ff00.0021 = 0
    ff00.0021 = 0
    ff00.0021 = 0
    ff00.0021 = 0
    ff00.0021 = 0
    ff00.0021 = 0
    ff00.0021 = 0
    ff00.0021 = 0
    ff00.0021 = 0
    ff00.0021 = 0
    ff00.0021 = 0
    ff00.0021 = 0
    ff00.0021 = 0
    ff00.0021 = 0
    ff00.0021 = 0
    ff00.0021 = 0
    ff00.0021 = 0
    ff00.0021 = 0
    ff00.0021 = 0
    ff00.0021 = 0
    ff00.0021 = 0
    ff00.0021 = 0
    ff00.0021 = 0
    ff00.0021 = 0
    ff00.0021 = 0
    ff00.0021 = 0
    ff00.0021 = 0
    ff00.0021 = 0
    ff00.0021 = 226 /* wheel */
    ff00.0021 = 125 /* wheel */
    ff00.0021 = 255 /* gas */
    ff00.0021 = 255 /* gas */
    ff00.0021 = 255 /* brake */
    ff00.0021 = 255 /* brake */
    ff00.0021 = 255 /* clutch */
    ff00.0021 = 255 /* clutch */
    ff00.0021 = 0
    ff00.0021 = 255
    ff00.0021 = 255
    ff00.0021 = 0
    ff00.0021 = 0
    ff00.0021 = 0
    ff00.0021 = 0
    ff00.0021 = 0
    ff00.0021 = 0
    ff00.0021 = 0
    ff00.0021 = 0
    ff00.0021 = 0
    ff00.0021 = 0
```
