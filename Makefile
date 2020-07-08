obj-m += hid-tminit.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) modules
install:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) modules_install
clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) clean
