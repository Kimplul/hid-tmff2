KDIR ?= /lib/modules/$(shell uname -r)/build

# Auto-generated build-time version for T500RS
T500RS_BASE_VERSION ?= 0.1
GIT_HASH := $(shell git rev-parse --short=7 HEAD 2>/dev/null || echo "local")
BUILD_HASH := $(shell date +%s | sha1sum | cut -c1-7)

T500RS_VERSION ?= $(T500RS_BASE_VERSION)-$(GIT_HASH)+b$(BUILD_HASH)
export T500RS_VERSION_DEF := -DT500RS_DRIVER_VERSION=\"$(T500RS_VERSION)\"


all: deps/hid-tminit
	@echo "T500RS build version: $(T500RS_VERSION)"
	@echo " - base: $(T500RS_BASE_VERSION), commit: $(GIT_HASH), build: $(BUILD_HASH)"
	$(MAKE) -C $(KDIR) M=$(shell pwd) modules

install: deps/hid-tminit
	$(MAKE) -C $(KDIR) M=$(shell pwd) modules_install
	depmod -A

.PHONY: udev-rules
udev-rules:
	install -m 0644 udev/99-thrustmaster.rules /etc/udev/rules.d/

.PHONY: steamdeck-rules
steamdeck-rules:
	install -m 0644 udev/71-thrustmaster-steamdeck.rules /etc/udev/rules.d/

clean: deps/hid-tminit
	$(MAKE) -C $(KDIR) M=$(shell pwd) clean


.PHONY: deps/hid-tminit
deps/hid-tminit:
	$(MAKE) -C deps/hid-tminit KDIR="$(KDIR)" $(MAKECMDGOALS)
