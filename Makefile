KDIR ?= /lib/modules/$(shell uname -r)/build

all: hid-tminit
	$(MAKE) -C $(KDIR) M=$(shell pwd) modules

install: hid-tminit
	$(MAKE) -C $(KDIR) M=$(shell pwd) modules_install
	depmod -A

clean: hid-tminit
	$(MAKE) -C $(KDIR) M=$(shell pwd) clean


.PHONY: hid-tminit
hid-tminit:
	$(MAKE) -C hid-tminit KDIR="$(KDIR)" $(MAKECMDGOALS)

test:
	sudo $(MAKE) install
	clear
	sudo dmesg -C
	sudo modprobe -r hid-tmt500rs
	sudo modprobe hid-tmt500rs
	dmesg