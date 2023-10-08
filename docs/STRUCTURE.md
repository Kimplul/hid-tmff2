# Structure of the Thrustmaster device 'stack'

+ `hid-tmff`

Driver for older Thrustmaster wheels, has existed in the Kernel for a long time
and I have nothing to with it.

+ `hid-thrustmaster`

Driver for initializing newer Thrustmaster wheels, initially added in version
5.13. Essentially `hid-tminit`, with some modifications to better fit into the
kernel.

+ `hid-tminit`

Driver for initializing newer Thrustmaster wheels, currently maintained over at
https://github.com/scarburato/hid-tminit.

All newer Thrustmaster wheels initially identify themselves as `Thrustmaster FFB
Wheel`, and it is up to the `hid-tminit` driver to boot the wheel into its
correct designation, after which the corresponding driver can take over. No FFB
takes place while this driver is active.

+ `hid-tmff-new`

Active driver module for different wheels, responsible for handling FFB.
At the moment only supports T300 RS and T248. Note that the T300 RS has a number
of different modes, of which PS3, PS4, and PS3 F1 (advanced) mode are known to
work (at least on some systems, please open up any issues you encounter.)

+ `hid-tmff2`

Name of this projects, initially I intended to write `hid-tmff` but for newer
wheels, hence the name. Because of some slight weirdness in the Kernel build
configuration, I couldn't use `hid-tmff2` as the module name, so I chose
`hid-tmff-new`.

I don't know exactly which wheels are 'newer' and which are 'older', but at
least T150/T248/T300/T500 are in the 'newer' category.

Note that the T150 already has a driver in a separate module
(https://github.com/scarburato/t150_driver).
