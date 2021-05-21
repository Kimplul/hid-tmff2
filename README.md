# Linux kernel module for Thrustmaster T300RS and T248 wheels

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

### Dependencies

Kernel modules require kernel headers to be installed.

+ Debian-based: `apt install linux-headers-$(uname -r)`
+ Arch-based: `pacman -S linux-headers`
+ Fedora-based: `yum install kernel-devel kernel-headers`


### Manual installation

+ Unplug wheel from computer
+ `git clone --recurse-submodules https://github.com/Kimplul/hid-tmff2.git`
+ `make`
+ `sudo make install`
+ Plug wheel back in
+ reboot (not strictly necessary, but definitely recommended)
    
Done!

> Note: On some systems, you will get an error/warning about SSL. This is normal for unsigned modules. For info on signing modules yourself (completely optional), see [here](https://www.kernel.org/doc/html/latest/admin-guide/module-signing.html?highlight=module%20signing).

### DKMS

+ Unplug wheel from computer
+ `sudo ./dkms-install.sh`
+ Plug wheel back in
+ reboot (not strictly necessary, but definitely recommended)

Done!
> :warning: Warning: There have been reports that this driver does not work if the wheel's firmware version is any other than v. 31. To update the firmware, you will have to fire up a Windows installation and update the firmware using the official Thrustmaster tools.

> :warning: Warning: There was a name change when adding support for the T248 from `hid-tmt300rs` to `hid-tmff-new`, and you may have to uninstall the older version of the driver.

## Additional tidbits

+ If you've bought a new wheel, you will most likely have to update the firmware through Windows before it will work with this driver.

+ T300 RS has an advanced F1 mode that can be activated with an F1 attachment when in PS3 mode. The base wheel will also work in PS4 mode,
 but it's less tested and if you encounter issues with this mode, please feel free to open up an issue about it.
 
+ T248 isn't as extensively tested as T300 RS, please see issues and open new ones if you encounter problems.
  There is currently no support for the built-in screen.

+ To change gain, autocentering etc. use [Oversteer](https://github.com/berarma/oversteer).

+ If a wheel has a deadzone in games, you can try setting up a udev rule:
    
    `/etc/udev/rules.d/99-joydev.rules`

    ```
    SUBSYSTEM=="input", ATTRS{idVendor}=="044f", ATTRS{idProduct}=="WHEEL_ID", RUN+="/usr/bin/evdev-joystick --evdev %E{DEVNAME} --deadzone 0"
    ```
    
    where `WHEEL_ID` is
    | Wheel                      | WHEEL_ID   |
    |----------------------------|------|
    | T300 RS, PS3 normal mode   | b66e |
    | T300 RS, PS3 advanced mode | b66f |
    | T300 RS, PS4 normal mode   | b66d |
    | T248                       | b696 |
    | T500 RS                    | b65e |

    This should make sure that the wheel behaves like you'd want from a wheel.

+ There have been reports that some games work better with a different timer period (see [#11](https://github.com/Kimplul/hid-tmff2/issues/11) and [#10](https://github.com/Kimplul/hid-tmff2/issues/10)). To change the timer period, create `/etc/modprobe.d/hid-tmff-new.conf` and add `options hid-tmff-new timer_msecs=NUMBER` into it. The default timer period is 8, but numbers as low as 2 should work alright.

There have been reports that some games work better with a different timer period (see [#11](https://github.com/Kimplul/hid-tmff2/issues/11) and [#10](https://github.com/Kimplul/hid-tmff2/issues/10)). To change the timer period, create `/etc/modprobe.d/hid-tmt300rs.conf` and add `options hid-tmt300rs timer_msecs=NUMBER` into it. The default timer period is 8, but numbers as low as 2 should work alright.
