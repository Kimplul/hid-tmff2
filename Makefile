KDIR ?= /lib/modules/$(shell uname -r)/build

all: deps/hid-tminit deps/usb-tminit
	$(MAKE) -C $(KDIR) M=$(shell pwd) modules

install: deps/hid-tminit deps/usb-tminit
	$(MAKE) -C $(KDIR) M=$(shell pwd) modules_install
	depmod -A

clean: deps/hid-tminit deps/usb-tminit
	$(MAKE) -C $(KDIR) M=$(shell pwd) clean


.PHONY: deps/hid-tminit deps/usb-tminit
deps/hid-tminit:
	$(MAKE) -C deps/hid-tminit KDIR="$(KDIR)" $(MAKECMDGOALS)

deps/usb-tminit:
	$(MAKE) -C deps/usb-tminit KDIR="$(KDIR)" $(MAKECMDGOALS)
