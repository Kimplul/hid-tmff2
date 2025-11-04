KDIR ?= /lib/modules/$(shell uname -r)/build

# Auto-generated global build-time version for TMFF2
TMFF2_BASE_VERSION ?= 0.1

# Allow packagers / CI to provide a fixed hash or full version:
#   make GIT_HASH=deadbee
#   make TMFF2_VERSION=0.1-1
#
# Only derive GIT_HASH from git if none was provided and this is a git checkout.
ifeq ($(origin GIT_HASH), undefined)
  GIT_HASH := $(shell if command -v git >/dev/null 2>&1 && [ -d .git ]; then \
      git rev-parse --short=7 HEAD 2>/dev/null; \
    else \
      echo local; \
    fi)
endif

BUILD_HASH := $(shell date +%s | sha1sum | cut -c1-7)

TMFF2_VERSION ?= $(TMFF2_BASE_VERSION)-$(GIT_HASH)+b$(BUILD_HASH)
export TMFF2_VERSION_DEF := -DTMFF2_DRIVER_VERSION=\"$(TMFF2_VERSION)\"


all: deps/hid-tminit
	@echo "TMFF2 build version: $(TMFF2_VERSION)"
	@echo " - base: $(TMFF2_BASE_VERSION), commit: $(GIT_HASH), build: $(BUILD_HASH)"
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
