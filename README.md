# Linux kernel module for the Thrustmaster T300RS wheel

## Current state
Playable. I've made some improvements to the dynamic updating of effects, and
while still far for perfect, the experience is slowly getting better and better.
Some drawbacks include possible effect inaccuracies in comparison with the Windows driver and in some
games inconsistent pedal mapping. Meaning that all pedals should be detected in games, but may be mapped incorrectly.

Anycase, **this version is usable in most force feedback games, supports
rangesetting as well as gain and autocentering along with most force feedback effects.**
## Small note
    
While I haven't personally come across any crashes or lockups with this
version, I can't promise that they won't occur under any circumstances.

With that in mind,

## Installation

### Manual

+ Unplug wheel from computer
+ `git clone --recurse-submodules https://github.com/Kimplul/hid-tmff2.git`
+ `make`
+ `sudo make install`
+ Plug wheel back in
+ reboot (not strictly necessary, but definitely recommended)
    
Done!

### DKMS

+ Unplug wheel from computer
+ `sudo ./dkms-install.sh`
+ Plug wheel back in
+ reboot (not strictly necessary, but definitely recommended)

Done!
> :warning: Warning: There have been reports that this driver does not work if the wheel's firmware version is any other than v. 31. To update the firmware, you will have to fire up a Windows installation and update the firmware using the official Thrustmaster tools.

## Additional tidbits
    
+ Change range:
  
Write a value between 0 and 1080 to
`/sys/bus/hid/devices/XXXX:044F:B66E.XXX/range`

+ Change autocenter and gain:

Use the default evdev ways, i.e. https://www.kernel.org/doc/html/v5.1/input/ff.html
    
+ Currently there is support for this driver in oversteer(https://github.com/berarma/oversteer), and ffbwrap(https://github.com/berarma/ffbtools) should work just fine.
+ If the wheel has a deadzone in games, you can set up a udev rule:
    
    `/etc/udev/rules.d/99-joydev.rules`

    ```
    SUBSYSTEM=="input", ATTRS{idVendor}=="044f", ATTRS{idProduct}=="b66e", RUN+="/usr/bin/evdev-joystick --evdev %E{DEVNAME} --deadzone 0"
    ```

This should make sure that the wheel behaves like you'd want from a
wheel.

+ Change timer period:

There have been reports that some games work better with a different timer period (see [#11](https://github.com/Kimplul/hid-tmff2/issues/11) and [#10](https://github.com/Kimplul/hid-tmff2/issues/10)). To change the timer period, create `/etc/modprobe.d/hid-tmt300rs.conf` and add `options hid-tmt300rs timer_msecs=NUMBER` into it. The default timer period is 8, but numbers as low as 2 should work alright.
