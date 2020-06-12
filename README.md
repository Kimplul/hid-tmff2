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
