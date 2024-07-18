# Linux kernel module for Thrustmaster T300RS, T248 and (experimental) TX and TS-XW wheels

> **DISCLAIMER:** The module is now ready for near-stable USE in most force
> feedback games, supports rangesetting as well as gain and autocentering along
> with most force feedback effects. While I haven't personally come across any
> crashes or lockups with this version, I can't promise that they won't occur
> under any circumstances.

![GitHub last commit (master)](https://img.shields.io/github/last-commit/Kimplul/hid-tmff2/master)
![License](https://img.shields.io/github/license/Kimplul/hid-tmff2)
![GitHub contributors](https://img.shields.io/github/contributors/Kimplul/hid-tmff2)


## Description

A Linux kernel module for Thrustmaster T300RS, T248, and (experimental support)
TX and TS-XV wheels.

I've been working on enhancing the real-time updating of effects, and although
it's not flawless yet, the overall experience is gradually improving. There are
a couple of issues, though. First, there might be occasional inaccuracies in how
the effects compare to the Windows driver. Second, in certain games, the mapping
of pedal inputs can be inconsistent. This means that while all pedals should be
recognized by the games, they might not be mapped correctly.

I only have access to the base editions of T300RS and T248 wheels to test with, but
from reports it seems that other editions (F1, GT, Alcantara, etc.) should also work
with this driver.

TX support was contributed by
[@davidedmundson](https://github.com/davidedmundson),
TS-XW support was contributed by
[@yassineimounachen](https://github.com/yassineimounachen).

## Installation

You can either install this kernel module by using DKMS or
manually building from source. If you're unsure which to pick,
go with DKMS, it will automatically recompile the driver
whenever needed.

An AUR package is also available:
[hid-tmff2-dkms-git](https://aur.archlinux.org/packages/hid-tmff2-dkms-git)

### Dependencies

Kernel modules require kernel headers to be installed. Use any
one of the right command for your distribution:

```shell
sudo apt install linux-headers-$(uname -r)   # Debian-based
sudo pacman -S linux-headers                 # Arch-based
sudo pacman -S linux-neptune-61-headers      # For SteamDeck specifically
sudo yum install kernel-devel kernel-headers # Fedora-based
```

#### Manual installation
+ Unplug wheel from computer
+ Run
  ```shell
  git clone --recurse-submodules https://github.com/Kimplul/hid-tmff2.git
  cd hid-tmff2
  make
  sudo make install
  ```
+ Plug wheel back in
+ Reboot *(Optional, yet Recommended)*

#### DKMS (Dynamic Kernel Module Support)

+ Unplug wheel from computer
+ Run
  ```shell
  git clone --recurse-submodules https://github.com/Kimplul/hid-tmff2.git
  cd hid-tmff2
  sudo ./dkms/dkms-install.sh
  ```
+ Plug wheel back in
+ Reboot *(Optional, yet Recommended)*

> **NOTE:** See [INTEGRATION](./docs/INTEGRATION.md)
> for install instructions for other linux distributions.

> **NOTE:** On some systems, you will get an error/warning about SSL. This is
> normal for unsigned modules. For info on signing modules yourself
> (completely optional), see
> [here](https://www.kernel.org/doc/html/latest/admin-guide/module-signing.html).

> **NOTE:** Thrustmaster TX and TS-XW wheels aren't supported by `hid-tminit` as of yet,
> meaning that the wheels have to be initialized with `tmdrv`. Please see
> https://github.com/Kimplul/hid-tmff2/issues/48.

> **WARNING:** There have been reports that this driver does not work if
> the wheel's firmware version is older than v. 31. To update the firmware, you
> will have to fire up a Windows installation and update the firmware using the
> official Thrustmaster tools.

> **WARNING:** There was a name change when adding support for the T248
> from `hid-tmt300rs` to `hid-tmff-new`, and you may have to uninstall the older
> version of the driver.

## Contributing

This project wants help from people who can contribute.
If you would like to help add a wheel to this driver,
please have a look through `docs` and/or [CONTRIBUTING.md](./docs/CONTRIBUTING.md)
for what might need to be done.

If you have a wheel that's not not supported, but suspect it might fit into the
driver, please feel free to open up an issue about it. Currently open requests
for wheels:

+ [T500 RS](https://github.com/Kimplul/hid-tmff2/issues/18)
+ [T818](https://github.com/Kimplul/hid-tmff2/issues/58)
+ [T-GT II](https://github.com/Kimplul/hid-tmff2/issues/55)
+ [T128P](https://github.com/Kimplul/hid-tmff2/issues/67) (initial alpha support, needs `hid-tminit` tweaking)
+ [TS-PC](https://github.com/Kimplul/hid-tmff2/issues/65)

## Common issues and notes

+ Reportedly some games running under Wine/Proton won't recognize wheels without
  the official Thrustmaster drivers installed within the prefix. See
  [#46](https://github.com/Kimplul/hid-tmff2/issues/46#issuecomment-1199080845).
  For installation instructions, see
  [DRIVER](./docs/DRIVER.md).

  Note that you will still need
  the Linux driver, the Windows driver just installs some files needed by games to
  correctly recognize the Linux driver. The Windows driver itself does not work
  under Wine/Proton.

+ Until the updated `hid-tminit` is
  [upstreamed](https://github.com/scarburato/hid-tminit), you might want to
  blacklist the kernel module `hid-thrustmaster`. Do this with
  ```shell
  echo 'blacklist hid_thrustmaster' | sudo tee /etc/modprobe.d/hid_thrustmaster.conf
  ```

+ If you've bought a new wheel, you will most likely have to update the firmware
  through Windows before it will work with this driver.

+ T300 RS has an advanced F1 mode that can be activated with an F1 attachment
  when in PS3 mode. The base wheel will also work in PS4 mode, but it's less
  tested and if you encounter issues with this mode, please feel free to open up
  an issue about it.

+ T248 isn't as extensively tested as T300 RS, please see issues and open new
  ones if you encounter problems. There is currently no support for the built-in
  screen.

+ TX support is considered experimental, please see issues
  (especially https://github.com/Kimplul/hid-tmff2/issues/48)
  and open new ones if you encounter any problems.

+ To change gain, autocentering etc. use
  [Oversteer](https://github.com/berarma/oversteer).

+ If a wheel has a deadzone in games, you can try setting up a udev rule:
  `/etc/udev/rules.d/99-joydev.rules`

  ```
  SUBSYSTEM=="input", ATTRS{idVendor}=="044f", ATTRS{idProduct}=="WHEEL_ID", RUN+="/usr/bin/evdev-joystick --evdev %E{DEVNAME} --deadzone 0"
  ```

  where `WHEEL_ID` is

  | Wheel                      | WHEEL_ID   |
  |----------------------------|------------|
  | T300 RS, PS3 normal mode   | b66e       |
  | T300 RS, PS3 advanced mode | b66f       |
  | T300 RS, PS4 normal mode   | b66d       |
  | T248                       | b696       |
  | TX                         | b669       |


  This should make sure that the wheel behaves like you'd want from a wheel.

+ There have been reports that some games work better with a different timer
  period (see [#11](https://github.com/Kimplul/hid-tmff2/issues/11) and
  [#10](https://github.com/Kimplul/hid-tmff2/issues/10)).

  To change the timer period, create `/etc/modprobe.d/hid-tmff-new.conf`
  and add `options hid-tmff-new timer_msecs=NUMBER` into it.
  The default timer period is 8, but numbers as low as 2 should work alright.
