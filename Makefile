obj-m += hid-tminit.o
obj-m += hid-tmt300rs.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) modules
install:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) modules_install
	depmod -A

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) clean
