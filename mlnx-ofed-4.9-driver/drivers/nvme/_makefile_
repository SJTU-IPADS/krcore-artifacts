KVER ?= $(shell uname -r)
SRC_DIR ?= $(shell pwd)
OFA_DIR ?= /usr/src/ofa_kernel
# get flavor
knownvariants=(default|trace|ppc64|bigsmp|debug|ppc|kdump|kdumppae|smp|vmi|vmipae|xen|xenpae|pae)
verrel=$(shell echo $(KVER) | sed -r -e s'/-$(knownvariants)//g')
ifneq ($(verrel),$(KVER))
variant1=$(shell echo $(KVER) | sed -r -e s'/$(verrel)[-]//g')
variant=$(shell test -d $(OFA_DIR)/$(variant1) && echo $(variant1) || echo default)
else
variant=default
endif
FLAVOR ?= $(variant)
OFA = $(shell ( test -d $(OFA_DIR)/$(KVER) && echo $(OFA_DIR)/$(KVER) ) || ( test -d $(OFA_DIR)/$(FLAVOR) && echo $(OFA_DIR)/$(FLAVOR) ) || ( test -d /var/lib/dkms/mlnx-ofed-kernel/ && ls -d /var/lib/dkms/mlnx-ofed-kernel/*/build ) || ( echo $(OFA_DIR) ))
OFA_SYMBOLS = $(wildcard $(OFA)/Module*.symvers)

ifneq ($(shell test -d $(OFA) && echo "true" || echo "" ),)
include $(OFA)/configure.mk.kernel
endif

export KERNELRELEASE=$(KVER)
K_BUILD ?= /lib/modules/$(KVER)/build
K_OBJ ?= $(K_BUILD)
K_SRC ?= $(shell test -d /lib/modules/$(KVER)/source && echo /lib/modules/$(KVER)/source || echo $(K_BUILD))

EXTRA_CFLAGS += $(shell echo $(BACKPORT_INCLUDES) | sed -e 's@/var/tmp/OFED_topdir/BUILD@/usr/src@')

autoconf_h=$(shell /bin/ls -1 $(K_BUILD)/include/*/autoconf.h 2> /dev/null | head -1)
kconfig_h=$(shell /bin/ls -1 $(K_BUILD)/include/*/kconfig.h 2> /dev/null | head -1)

ifneq ($(kconfig_h),)
KCONFIG_H = -include $(kconfig_h)
endif

V ?= 0

# GCC earlier than 4.6.0 will build modules which require 'mcount',
# and this symbol will not be available in the kernel if the kernel was
# compiled with GCC 4.6.0 and above.
# therefore, to prevent unknown symbol issues we disable function tracing.
#
CC  = $(CROSS_COMPILE)gcc
CPP = $(CC) -E

CPP_MAJOR := $(shell $(CPP) -dumpversion 2>&1 | cut -d'.' -f1)
CPP_MINOR := $(shell $(CPP) -dumpversion 2>&1 | cut -d'.' -f2)
CPP_PATCH := $(shell $(CPP) -dumpversion 2>&1 | cut -d'.' -f3)
# Assumes that major, minor, and patch cannot exceed 999
CPP_VERS  := $(shell expr 0$(CPP_MAJOR) \* 1000000 + 0$(CPP_MINOR) \* 1000 + 0$(CPP_PATCH))
compile_h=$(shell /bin/ls -1 $(K_BUILD)/include/*/compile.h 2> /dev/null | head -1)
ifneq ($(compile_h),)
KERNEL_GCC_MAJOR := $(shell grep LINUX_COMPILER $(compile_h) | sed -r -e 's/.*gcc version ([0-9\.\-]*) .*/\1/g' | cut -d'.' -f1)
KERNEL_GCC_MINOR := $(shell grep LINUX_COMPILER $(compile_h) | sed -r -e 's/.*gcc version ([0-9\.\-]*) .*/\1/g' | cut -d'.' -f2)
KERNEL_GCC_PATCH := $(shell grep LINUX_COMPILER $(compile_h) | sed -r -e 's/.*gcc version ([0-9\.\-]*) .*/\1/g' | cut -d'.' -f3)
KERNEL_GCC_VER  := $(shell expr 0$(KERNEL_GCC_MAJOR) \* 1000000 + 0$(KERNEL_GCC_MINOR) \* 1000 + 0$(KERNEL_GCC_PATCH))
ifneq ($(shell if [ $(CPP_VERS) -lt 4006000 ] && [ $(KERNEL_GCC_VER) -ge 4006000 ]; then \
					echo "YES"; else echo ""; fi),)
$(info Warning: The kernel was compiled with GCC newer than 4.6.0, while the current GCC is older than 4.6.0, Disabling function tracing to prevent unknown symbol issues...)
override WITH_MAKE_PARAMS += CONFIG_FUNCTION_TRACER= CONFIG_HAVE_FENTRY=
endif
ifneq ($(shell if [ $(CPP_VERS) -ge 4006000 ] && [ $(KERNEL_GCC_VER) -lt 4006000 ]; then \
					echo "YES"; else echo ""; fi),)
$(info Warning: The kernel was compiled with GCC older than 4.6.0, while the current GCC is newer than 4.6.0, Disabling function tracing to prevent unknown symbol issues...)
override WITH_MAKE_PARAMS += CONFIG_FUNCTION_TRACER= CONFIG_HAVE_FENTRY=
endif
endif

ifneq ($(shell if (echo $(KVER) | grep -qE 'uek'); then \
					echo "YES"; else echo ""; fi),)
override WITH_MAKE_PARAMS += ctf-dir=$(CWD)/.ctf
endif

ifeq ($(CONFIG_NVME_HOST_WITHOUT_FC),m)
MY_NVMF_KCONFIGS :=	CONFIG_NVME_HOST_WITHOUT_FC=m \
			CONFIG_NVME_TARGET= \
			CONFIG_NVME_TARGET_LOOP= \
			CONFIG_NVME_TARGET_RDMA= \
			CONFIG_NVME_TARGET_FC= \
			CONFIG_NVME_TARGET_FCLOOP= \
			CONFIG_NVME_TARGET_DUMMY= \
			CONFIG_NVME_CORE=m \
			CONFIG_BLK_DEV_NVME=m \
			CONFIG_NVME_FABRICS=m \
			CONFIG_NVME_FC= \
			CONFIG_NVME_RDMA=m \
			CONFIG_NVME_MULTIPATH=y \
			CONFIG_NVME_HOST_DUMMY=
else
MY_NVMF_KCONFIGS :=	CONFIG_NVME_HOST_WITHOUT_FC= \
			CONFIG_NVME_TARGET=m \
			CONFIG_NVME_TARGET_LOOP=m \
			CONFIG_NVME_TARGET_RDMA=m \
			CONFIG_NVME_TARGET_FC=m \
			CONFIG_NVME_TARGET_FCLOOP=m \
			CONFIG_NVME_TARGET_DUMMY= \
			CONFIG_NVME_CORE=m \
			CONFIG_BLK_DEV_NVME=m \
			CONFIG_NVME_FABRICS=m \
			CONFIG_NVME_FC=m \
			CONFIG_NVME_RDMA=m \
			CONFIG_NVME_MULTIPATH=y \
			CONFIG_NVME_HOST_DUMMY=
endif

EXTRA_CFLAGS += -DCONFIG_NVME_MULTIPATH=1

name := mlnx-nvme
VERSION=$(shell grep "define _version" $(name).spec  | sed -e 's/.*_version //' | sed -e 's/}//' | sed -e 's/\s*//g')
RELEASE=$(shell grep "define _release" $(name).spec  | sed -e 's/.*_release //' | sed -e 's/}//' | sed -e 's/\s*//g')
PACKAGE := $(name)-$(VERSION)
SHELL = /bin/bash
rpmspec := $(name).spec
rpmroot = $(PWD)/rpm-dist/
rpmopts = --nodeps --buildroot='$(rpmroot)/_rpm' --define '_source_filedigest_algorithm md5' --define '_binary_filedigest_algorithm md5'
rpmmacros =\
		   --define='_topdir $(rpmroot)'\
		   --define='_rpmdir $(rpmroot)'\
		   --define='_srcrpmdir $(rpmroot)'\
		   --define='_sourcedir $(rpmroot)'\
		   --define='_specdir $(PWD)'

default:
# compile with ofed driver
	make -C $(K_BUILD) O=$(K_OBJ) M=$(shell pwd) $(WITH_MAKE_PARAMS) \
		$(MY_NVMF_KCONFIGS) \
		CONFIG_DTRACE= \
		CONFIG_CTF= \
		LINUXINCLUDE=' \
		$(EXTRA_CFLAGS) \
		-include $(autoconf_h) \
		$(KCONFIG_H) \
		-include $(OFA)/include/linux/compat-2.6.h \
		-I$(PWD) \
		-I$(OFA)/include \
		-I$(OFA)/include/uapi \
		$(BACKPORT_INCLUDES) \
		$$(if $$(CONFIG_XEN),-D__XEN_INTERFACE_VERSION__=$$(CONFIG_XEN_INTERFACE_VERSION)) \
		$$(if $$(CONFIG_XEN),-I$$(srctree)/arch/x86/include/mach-xen) \
		-I$$(srctree)/arch/$$(SRCARCH)/include \
		-Iarch/$$(SRCARCH)/include/generated \
		-Iinclude \
		-I$$(srctree)/arch/$$(SRCARCH)/include/uapi \
		-Iarch/$$(SRCARCH)/include/generated/uapi \
		-I$$(srctree)/include \
		-I$$(srctree)/include/uapi \
		-Iinclude/generated/uapi \
		$$(if $$(KBUILD_SRC),-Iinclude2 -I$$(srctree)/include) \
		-I$$(srctree)/arch/$$(SRCARCH)/include \
		-Iarch/$$(SRCARCH)/include/generated \
		' \
		KBUILD_EXTRA_SYMBOLS="$(OFA_SYMBOLS)" \
		modulesymfile=$(SRC_DIR)/Module.symvers \
		modules

install:
	make -C $(K_BUILD) O=$(K_OBJ) M=$(shell pwd) INSTALL_MOD_PATH=$(INSTALL_MOD_PATH) INSTALL_MOD_DIR=$(INSTALL_MOD_DIR) $(WITH_MAKE_PARAMS) modules_install
	if [ ! -n "$(INSTALL_MOD_PATH)" ]; then /sbin/depmod $(KVER);fi;

rpmcheck:
	@which rpmbuild &> /dev/null; \
		if [ $$? -ne 0 ]; then \
		echo "*** This make target requires an rpm-based linux distribution."; \
		(exit 1); exit 1; \
		fi
	-mkdir -p $(rpmroot)/BUILD

srcrpm: dist rpmcheck $(rpmspec)
	-rpmbuild -bs --define 'src_release $(RELEASE)' $(rpmmacros) $(rpmopts) $(rpmspec); \
		if [ $$? -ne 0 ]; then \
		(exit 1); exit 1; \
		fi

binrpm: rpmcheck $(rpmspec)
	-rpmbuild -bb $(rpmmacros) $(rpmopts) $(rpmspec); \
		if [ $$? -ne 0 ]; then \
		(exit 1); exit 1; \
		fi

dist:
	mkdir -p $(rpmroot)/$(PACKAGE)/
	cp {$(rpmspec),makefile,Makefile,dkms.conf,Module.supported} $(rpmroot)/$(PACKAGE)/
	cp -r target $(rpmroot)/$(PACKAGE)/
	cp -r host $(rpmroot)/$(PACKAGE)/
	cp -r lpfc $(rpmroot)/$(PACKAGE)/
	cp -r debian $(rpmroot)/$(PACKAGE)/
	cp -r tools $(rpmroot)/$(PACKAGE)/
	cd $(rpmroot) && tar czf $(PACKAGE).tgz $(PACKAGE)
	cd $(rpmroot) && tar czf $(name)_$(VERSION).orig.tar.gz $(PACKAGE)

clean:
	rm -f *.o
	rm -f *.ko *.ko.gz
	rm -f *.mod.c
	rm -f Module*.symvers modules*.order

distclean: clean
	@rm -rf $(PWD)/rpm-dist
	rm -f makefile *.spec

all: clean distclean dist srcrpm binrpm
