#!/bin/bash
KDIR="/lib/modules/$(uname -r)/build"

if [ "$1" == "t598" ]; then
    echo "Building T598 driver (skipping hid-tminit)"
    make -C $KDIR M=$(pwd) CC=clang LLVM=1 clean
    make -C $KDIR M=$(pwd) CC=clang LLVM=1 modules
else
    make -C deps/hid-tminit KDIR="$KDIR" CC=clang LLVM=1 all
    make -C deps/hid-tminit KDIR="$KDIR" CC=clang LLVM=1 install
    make -C $KDIR M=$(pwd) CC=clang LLVM=1 clean
    make -C $KDIR M=$(pwd) CC=clang LLVM=1 modules
    make -C $KDIR M=$(pwd) CC=clang LLVM=1 modules_install
    depmod -A
fi
