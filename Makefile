KDIR ?= /lib/modules/$(shell uname -r)/build

all: deps/tminit
	$(MAKE) -C $(KDIR) M=$(shell pwd) modules

install: deps/tminit
	$(MAKE) -C $(KDIR) M=$(shell pwd) modules_install
	depmod -A

clean: deps/tminit
	$(MAKE) -C $(KDIR) M=$(shell pwd) clean


.PHONY: deps/tminit
deps/tminit:
	$(MAKE) -C tminit KDIR="$(KDIR)" $(MAKECMDGOALS)
