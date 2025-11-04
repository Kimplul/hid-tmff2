obj-m := hid-tmff-new.o
hid-tmff-new-y := \
		src/hid-tmff2.o \
		src/tmt300rs/hid-tmt300rs.o \
		src/tmt248/hid-tmt248.o \
		src/tmtx/hid-tmtx.o \
		src/tmtsxw/hid-tmtsxw.o \
		src/tmt500rs/hid-tmt500rs-usb.o

# Pass through the T500RS version define from Makefile (original branch style)
ccflags-y += $(T500RS_VERSION_DEF)
