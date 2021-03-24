obj-m += hid-tmt300rs.o
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
	$(MAKE) -C hid-tminit $(MAKECMDGOALS)
