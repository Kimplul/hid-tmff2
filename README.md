# Linux kernel module for the Thrustmaster T300RS wheel

## Current state
Playable. Dynamic updating of effects isn't baked into the wheel quite as
well as I'd hoped, and so the driver in its current state doesn't really
support it. At some point in the future I'd like to add it to the feature
list, but it will need a lot more research and might take a while.

One other issue is that if you have the T3PA pedals, the clutch doesn't seem
to be registered by most(all?) games. It is registered and works as expected
in jstest and jstest-gtk, no clue what that is about.

Anycase, **this version is usable in most force feedback games, supports
rangesetting as well as gain and autocentering along with most force feedback effects.**
## Small note
    
While I haven't personally come across any crashes or lockups with this
version, I can't promise that they won't occur under any circumstances.

With that in mind,

## Installation

+ Unplug wheel from computer
+ `git clone this-repo`
+ `make`
+ `sudo make install`
+ Plug wheel back in
+ reboot (not strictly necessary, but recommended)
    
Done!

## Additional tidbits
    
+ Change range:
  
Write a value between 0 and 65535 to
/sys/bus/devices/XXXX:044F:B66E.XXX/range

+ Change autocenter and gain:

Use the default evdev ways, i.e. https://www.kernel.org/doc/html/v5.1/input/ff.html
    
+ Currently there is no support for this driver in oversteer(https://github.com/berarma/oversteer), but ffbwrap(https://github.com/berarma/ffbtools) should, at least in theory, work. Although I have to say that I haven't got it to work so far.
