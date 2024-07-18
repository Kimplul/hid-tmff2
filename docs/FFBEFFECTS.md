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
    00 4f - ?
    f7 17 - time in milliseconds, unsigned
    00 00
    07 00 - offset from start(?)
    00 ff ff - end of new effect(?)
    00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
```

### Modifying (For dynamic updating of effects)

#### Direction: ``N/A``

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
    60 00 - standard header
    01 - ID
    31 - modify envelope
    84 - ID of envelope attribute ( attack_level 82,
                                    attack_length 81,
                                    fade_level 88,
                                    fade_length 84 )
    63 04 - value attribute should be set to ?***?
    00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
```

#### Duration:
```
    60 00 - standard header
    01 - ID
    49 00 41 - modify timing?
    6c 20 - length in milliseconds
    00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
```

#### Offset: `N/A` 

## FF_RAMP

Ramps seem to follow triangle wave parameters.

### Init:
```
    60 00 - standard header
    01 - ID
    6b - new ramp effect
    f6 7f - difference ***
    fe ff - level? *** (level = (if end_level > start_level, end_level, else
        start_level) - difference?) (signed)
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
    60 00 - standard header
    01 - ID
    0e - ???
    03 - ramp?
    e3 04 - difference?
    7d 75 - "level"?
    05 - ????
    00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
```

#### Envelope:
```
    60 00 - standard header
    01 - ID
    31 - modify envelope
    84 - ID of envelope attribute ( attack_level 82,
                                    attack_length 81,
                                    fade_level 88,
                                    fade_length 84 )
    63 04 - value attribute should be set to
    00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
```

#### Duration:
```     
    60 00
    01 - ID
    4e - ?
    08 - ?
    01 00 - time in milliseconds
    05 - ?
    41 - ?
    01 00 - time in milliseconds
    00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
```

#### Offset: `N/A`

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
00001110 01000011  0e 43  both     coefficint
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
    45 - update type, see below
    10 27 - length in milliseconds
    00 00 - offset in milliseconds
    00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00

```

### Duration:

Update type:

```
6th byte  hex
01000001  41  length
01000100  44  offset
01000101  45  length + offset
```

```
    60 00 - standard header
    01 - ID
    49
    06 - effect type (condition)?
    45 - update type
    6c 20 - length in milliseconds
    00 00 - offset in milliseconds
    00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
```

### Offset: `N/A` 

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
    magnitude:
    60 00
    02 - ID
    0e 01
    64 35 - value, between 00 00 and fc 7f (32764)
    00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00

    offset of effect:
    60 00
    02 - ID
    0e 02
    64 35 - value, between ff bf (-16385) and fd 3f (16381)
    00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00

    phase:
    60 00
    02 - ID
    0e 04
    64 35 - value, between 00 00 and 4a f7 (32586) in 5b steps
            meaning is 0 to 359 deg phase shift
    00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00

    period:
    60 00
    02 - ID
    0e 08
    64 35 - value, between 00 00 and ff ff (in milliseconds)
    00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00

```

### Envelope:
```
    60 00 - standard header
    01 - ID
    31 - modify envelope
    84 - ID of envelope attribute ( attack_level 82,
                                    attack_length 81,
                                    fade_level 88,
                                    fade_length 84  )
    63 04 - value attribute should be set to
    00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
```

### Duration:
```
    Square:
    60 00
    01 - ID
    49 - ?
    01 - Effect type
    41 - ?
    4e 0c - Time in milliseconds
    00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00

    Sine:
    60 00
    01 - ID
    49 - ?
    03 - Effect type
    41 - ?
    ec 13 - Time in milliseconds
    00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00

    Triangle:
    60 00
    01 - ID
    49 - ?
    02 - Effect type
    41 - ?
    4e 0c - Time in milliseconds
    00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00

    Sawtooth up:
    60 00
    01 - ID
    49 - ?
    04 - Effect type
    41 - ?
    4e 0c - Time in milliseconds
    00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00

    Sawtooth down:
    60 00
    01 - ID
    49 - ?
    05 - Effect type
    41 - ?
    ec 13 - Time in milliseconds
    00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
```
### Offset: `N/A ?`

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
