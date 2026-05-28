#!/bin/bash
if [ "$1" == "t598" ]; then
    echo "Installing T598 driver (skipping hid-tminit)"
    sudo rmmod hid-tmff-new 2>/dev/null
    sudo make -C /lib/modules/$(uname -r)/build M=$(pwd) modules_install 2>/dev/null
    sudo depmod -A
else
    sudo rmmod hid-tmff-new 2>/dev/null
    sudo make -C deps/hid-tminit KDIR="/lib/modules/$(uname -r)/build" install
    sudo make -C /lib/modules/$(uname -r)/build M=$(pwd) modules_install
    sudo depmod -A
fi

