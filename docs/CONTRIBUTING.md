# Contributing

## Overview
This file contains general contributing information. This project is seeking for help from people who can contribute. If you have a wheel that's not on the list, but suspect it might fit into the driver, please feel free to open up an issue about it.

Currently open requests for wheels:
- [T500 RS](https://github.com/Kimplul/hid-tmff2/issues/18)
- [T818](https://github.com/Kimplul/hid-tmff2/issues/58)
- [T-GT II](https://github.com/Kimplul/hid-tmff2/issues/55)
- [T128P](https://github.com/Kimplul/hid-tmff2/issues/67)

Other documents available are linked here:

- [FFBEffects-T300RS](./FFBEffects-T300RS.md): Force Feedback Effects example for T300RS
- [FFBEffects-T248](./FFBEffects-T248.md): Force Feedback Effects example for T248
- [Structure](https://github.com/Kimplul/hid-tmff2/wiki#structure-of-the-thrustmaster-device-stack): Structure of Thrustmaster device stack
- [TO-DO](./TODO.md): TO-DO list for maintainers

## How to add in support for a new T-series wheel?
Should probably not be too often that you need this info, but essentially use wireshark like in the previous example, but spin up a Windows virtual machine and install the Thrustmaster drivers on to it, and pass the device to the virtual machine. I prefer to use `qemu` with `virt-manager` as a frontend. With the wheel working under Windows, install [fedit.exe](https://gimx.fr/download/b882e209a0ac023d03abbf560dfc3f25fe6367ca/fedit.zip) and methodically go through all effects the device supports and compare the USB packets the driver sends out. You should be able to build up a table of what each value in the USB packet means, see [FFBEffects-T300RS.md](./FFBEffects-T300RS.md) for an example of what I found out about the T300.

## How to capture what effects a game sends to the driver?
Use ffbwrap from ffbtools: [github:berarma/ffbtools](https://github.com/berarma/ffbtools)
The documentation gives good examples, but tl;dr; For steam, insert the following into a game's launch options:
```shell
$ ffbwrap --logger=/home/$USER/game.log /dev/input/by-id/usb-Thrustmaster_Thrustmaster_T300RS_Racing_wheel-event-joystick -- %command%
```
This will create a file called `game.log` (with additional timestamp) in your home directory. Preferably change the name to suit the game, but you do you.

> **NOTE:** Most fixes presented in the documentation are more or less obsolete by now, but the tool is still very useful for logging purposes.

## How to capture what USB packets the driver sends to the device?
I'd recommend [wireshark](https://www.wireshark.org/)

Usb capture setup is fairly straightforward: [wireshark/CaptureSetup](https://wiki.wireshark.org/CaptureSetup/USB#linux)

Here's what I typically do when starting capturing:

- Run `sudo modprobe usbmon`. This will load a kernel module that allows Wireshark to read the USB packets.
- Open wireshark with root privileges. There are some ways to allow wireshark to access the packets with regular user privileges, I just haven't bothered with it.
- Select `usbmon0` from the view that opens up by default.
- The screen will quickly fill up with noise from other devices connected to the computer, so you will have to filter out the noise.
    - Run `lsusb` in a terminal. Look for the T300 in the list, you should see something like `Bus 001 Device 006: ID 044f:b66e ThrustMaster, Inc. Thrustmaster T300RS Racing wheel`
    - From the previous command, the Bus and Device fields can be used to filter out only packets from/to the device. To see all packets coming from the wheel, add in a filter `usb.src ~ "1\.6\..*"`, where `1` is in this case from the `Bus` field and `6` from the `Device` field. To see all packets coming from the device, use `usb.dst`.
    - This data is probably also filled with a lot of cruft. Data to/from endpoint 2 is button state info, which is probably unnecessary. To see FFB data being sent to the device, use endpoint 1, i.e. `usb.dst == "1.6.1"`. `~` is a Perl-compliant regex operator, whereas `==` just matches the string directly. To see both data coming from the device and going to it, use `usb.dst == "..." || usb.src == "..."`.
    - There is also endpoint 0, `usb.src == "1.6.0"` but it doesn't seem to be used for much.
- Use the three buttons in the top left of the screen to start, stop and restart captures.
- Do whatever you want with the device, packets should automatically be captured. When you want to save your capture to a file, stop the recording and go to `File > Export Specified Packets` and make sure `Displayed` is selected. This will apply the filter you've been using, and will only include the packets that are visible in Wireshark, i.e. it applies the filter you've specified.

> **NOTE:** Every time you unplug and replug your wheel, its `Device` field will probably change.
