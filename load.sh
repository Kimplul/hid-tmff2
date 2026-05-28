#!/bin/bash
if [ "$1" == "t598" ]; then
    echo "Loading T598 driver (skipping hid-tminit)"
    sudo rmmod hid-tmff-new 2>/dev/null
    sudo modprobe hid-tmff-new
    fftest /dev/hidraw0
else
    sudo rmmod hid-tmff-new 2>/dev/null
    sudo modprobe hid-tmff-new
    fftest /dev/hidraw0
fi

