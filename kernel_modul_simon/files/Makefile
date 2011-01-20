###################################################
#
# Makefile for building the hellokernel derived from char-driver sample module
#
###################################################

KERNEL_VERSION := `2.6.31`
KERNEL_DIR := /opt/angstrom-setup-scripts/build/tmp-angstrom_2008_1/work/beagleboard-angstrom-linux-gnueabi/linux-omap-2.6.31-r90/git
##KERNEL_DIR := /opt/oe/angstrom-dev/work/beagleboard-angstrom-linux-gnueabi/linux-omap-2.6.32-r89/git
##KERNEL_DIR := /opt/oe/old-angstrom-dev/work/beagleboard-angstrom-linux-gnueabi/linux-omap-2.6.29-r46/git
#KERNEL_DIR := /opt/oe/angstrom-dev/work/beagleboard-angstrom-linux-gnueabi/linux-omap-2.6.31-r89/git
##KERNEL_DIR := /opt/oe/angstrom-dev/sysroots/beagleboard-angstrom-linux-gnueabi/kernel/
#KERNEL_CROSS := /opt/oe/angstrom-dev/sysroots/i686-linux/usr/armv7a/bin/arm-angstrom-linux-gnueabi-
KERNEL_CROSS := /opt/angstrom-setup-scripts/build/tmp-angstrom_2008_1/sysroots/i686-linux/usr/armv7a/bin/arm-angstrom-linux-gnueabi-

#ifeq ($(DEBUG),y)
	EXTRA_CFLAGS += -O -ggdb # -O is need to expand inlines
#else
#	EXTRA_CFLAGS += -O2
#endif

ifneq ($(KERNELRELEASE),)
# call from kernel build system
obj-m := helloMcBSP.o
obj-m += gpioSet.o
obj-m += streamingOutput.o
obj-m += adsInput.o
obj-m += networkUDPfromkernel.o
obj-m += mcbspADS1258ToDBR.o
else
PWD := $(shell pwd)

default: kernel-module
.PHONY: kernel-module

TARGET_ARCH = "arm"
TARGET_OS = "linux-gnueabi"

kernel-module:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) ARCH=arm CROSS_COMPILE=$(KERNEL_CROSS) modules
endif

clean:
	rm -rf *.o *~ core .depend .*.cmd *.ko *.mod.c .tmp_versions sample Module.symvers

depend .depend dep:
	$(CC) $(EXTRA_CFLAGS) -M *.c > .depend

ifeq (.depend,$(wildcard .depend))
include .depend
endif
