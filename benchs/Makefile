export KDIR ?= /lib/modules/$(shell uname -r)/build

CLANG ?= clang-9
ifeq ($(origin CC),default)
CC := ${CLANG}
endif

KPATH := /usr/src/linux-headers-`uname -r`

variant=default
OFA_DIR ?= /usr/src/ofa_kernel
FLAVOR ?= $(variant)
OFA = $(shell test -d $(OFA_DIR)/$(FLAVOR) && echo $(OFA_DIR)/$(FLAVOR) || (test -d /var/lib/dkms/mlnx-ofed-kernel/ && ls -d /var/lib/dkms/mlnx-ofed-kernel/*/build) || echo\
 $(OFA_DIR))

ifneq ($(shell test -d $(OFA) && echo "true" || echo "" ),)
    include $(OFA)/configure.mk.kernel
endif

EXTRA_CFLAGS += $(shell [ -f $(KOBJ)/include/linux/modversions.h ] && \
                    echo "-DMODVERSIONS -DEXPORT_SYMTAB \
                                  -include $(KSRC)/include/linux/modversions.h")
EXTRA_CFLAGS += $(shell [ -f $(KOBJ)/include/config/modversions.h ] && \
                    echo "-DMODVERSIONS -DEXPORT_SYMTAB \
                                  -include $(KSRC)/include/config/modversions.h")
#EXTRA_CFLAGS += "-O2"

ofa_autoconf_h=$(shell /bin/ls -1 $(OFA)/include/*/autoconf.h 2> /dev/null | head -1)
ifneq ($(ofa_autoconf_h),)
OFA_AUTOCONF_H = -include $(ofa_autoconf_h)
endif

autoconf_h=$(shell /bin/ls -1 $(KDIR)/include/*/autoconf.h 2> /dev/null | head -1)

export ofa_flags ?= -include $(autoconf_h) $(OFA_AUTOCONF_H) -I/$(OFA)/include

KBUILD_EXTRA_SYMBOLS=$(OFA)/Module.symvers

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) CC=$(CC) CONFIG_CC_IS_CLANG=y CONFIG_CC_OPTIMIZE_FOR_PERFORMANCE_O3=y

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) CC=$(CC) clean
