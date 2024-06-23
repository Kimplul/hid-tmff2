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

## FF_SPRING:
### Init:
```
    60 00 - standard header
    01 - ID
    64 - new conditional effect
    fc 7f - positive coefficient (right?)
    fc 7f - negative coefficient (left?)
    fe ff - deadband (left?) (deadband + offset) (fe ff means no deadband)
    fe ff - deadband (right?) (deadband + offset)
    a6 6a a6 6a fe ff fe ff fe ff fe ff df 58 a6 6a 06 - some weird hard-coded
                                                    values to do with springs?

    4f
    f7 17 - duration
    00 00
    00 00 - offset
    00 ff ff - end of init
    00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
```

### Modifying:
Same as below (FF_DAMPER + FF_FRICTION + FF_INERTIA)

## FF_DAMPER + FF_FRICTION + FF_INERTIA:
### Init:
```
    60 00 - standard header
    02 -ID
    64 - new conditional effect
    fc 7f - positive coefficient (right?)
    fc 7f - negative coefficient (left?)
    fe ff - deadband (left?) (deadband + offset) (fe ff means no deadband)
    fe ff - deadband (right?) (deadband + offset)
    fc 7f fc 7f fe ff fe ff fe ff fe ff fc 7f fc 7f 07 - some weird hard-coded
                                                    values to do with friction?
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
    positive coefficient:
    60 00
    02 - ID
    0e 41
    64 35 - value, signed (between 00 00 and 01 80?)
    00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00

    negative coefficient:
    60 00
    02 - ID
    0e 42
    64 35 - value, signed (between 00 00 and ff 7f?)
    00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00

    deadband/offset:
    60 00
    02 - ID
    0e 4c
    64 35 - deadband (right?) (deadband + offset) (fe ff means no deadband)
    00 00 - deadband (left?) (deadband + offset)
    00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
    00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
```

### Duration:
```
    60 00 - standard header
    01 - ID
    49 06 41 - modify timing?
    6c 20 - length in milliseconds
    00 00 00 00 00 00 00 00
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
