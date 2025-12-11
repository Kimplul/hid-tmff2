KDIR ?= /lib/modules/$(shell uname -r)/build

all: deps/hid-tminit
	$(MAKE) -C $(KDIR) M=$(shell pwd) modules

install: deps/hid-tminit
	$(MAKE) -C $(KDIR) M=$(shell pwd) modules_install
	depmod -A

.PHONY: steamdeck-rules
steamdeck-rules:
	install -m 0644 udev/71-thrustmaster-steamdeck.rules /etc/udev/rules.d/

clean: deps/hid-tminit
	$(MAKE) -C $(KDIR) M=$(shell pwd) clean


.PHONY: deps/hid-tminit
deps/hid-tminit:
	$(MAKE) -C deps/hid-tminit KDIR="$(KDIR)" $(MAKECMDGOALS)
