# Retpoline support: check if this is the right architecture and that
# the kernel does not support it already.
# Alternatively, if we are called from the main mlnx-ofa build system,
# CONFIG_RETPOLINE will be set by the configure script, however
# subdir-ccflags-y will be set by the toplevel Makefile.
ifneq (,$(findstring $(ARCH),i386 x86_64))
  ifndef CONFIG_RETPOLINE
    CFLAGS_RETPOLINE=-mindirect-branch=thunk-inline -mindirect-branch-register -DRETPOLINE_MLNX
  endif
endif

# Enable retpoline support if the above holds and the compiler supports it.
ifneq (,$(CFLAGS_RETPOLINE))
  do_retpoline = $(call cc-option-yn, -mindirect-branch-register)
  subdir-ccflags-$(do_retpoline) += $(CFLAGS_RETPOLINE)
endif

obj-$(CONFIG_INFINIBAND_ISER)	+= ib_iser.o
obj-$(CONFIG_INFINIBAND_ISER_DUMMY)		+= ib_iser.o

ifeq ($(CONFIG_INFINIBAND_ISER_DUMMY),m)
ib_iser-y := ib_iser_dummy.o
else
ib_iser-y			:= iser_verbs.o iser_initiator.o iser_memory.o \
				   iscsi_iser.o
endif
