obj-m += usb-tminit.o
KDIR ?= /lib/modules/$(shell uname -r)/build

all:
	make -C $(KDIR) M=$(shell pwd) modules
install:
	make -C $(KDIR) M=$(shell pwd) modules_install
clean:
	make -C $(KDIR) M=$(shell pwd) clean
