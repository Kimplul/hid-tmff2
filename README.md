# Linux kernel module for the Thrustmaster T300RS wheel

## General information for the curious:
* This driver is heavily under development, and is currently missing some major features, such as force feedback among many other things.

* I try to only push to Github whenever I'm relatively sure the driver won't freeze up the kernel, but I absolutely do not recommend running this driver on your main computer. Set up a virtual machine if you want to test this out. I personally recommend QEMU/libvirt: https://www.qemu.org/download/

* In case you want to try installing this driver, it should be fairly simple:
  ```
  make
  sudo make install 
  ```
  And possibly some ```sudo depmod -a && sudo modprobe hid-tmff2```
  To see if the driver is being loaded, run ```dmesg```. The feed should be filled with debugging messages that honestly probably should be improved. A lot.

Update as of 27.06.2020 21:21

I managed to somewhat successfully play DiRT Rally. The force feedback works, but isn't anywhere near as high quality as on windows. I've experiences some odd cases where the wheel decides to lock up, but it doesn't seem to affect the host computer in any way. No clue why it happens, but they seem to be relatively rare and reseating the USB plug has so far always returned the wheel to a usable state.

NOTE: I still don't necessarily recommend using this driver, unless you're really desperate for some ffb. I can't promise that this driver won't freeze up your kernel, corrupt all your data and kick your dog.
