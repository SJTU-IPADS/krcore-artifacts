# SPDX-License-Identifier: GPL-2.0

# Retpoline support: check if this is the right architecture and that
# the kernel does not support it already.
# Alternatively, if we are called from the main mlnx-ofa build system,
# CONFIG_RETPOLINE will be set by the configure script, however
# subdir-ccflags-y will be set by the toplevel Makefile.
ifneq (,$(findstring $(ARCH),i386 x86_64))
  ifndef CONFIG_RETPOLINE
    ifneq (,$(shell awk 'BEGIN {if ($(VERSION).$(PATCHLEVEL) < 4.15) {print 1}}' </dev/null))
      CFLAGS_RETPOLINE=-mindirect-branch=thunk-inline -mindirect-branch-register -DRETPOLINE_MLNX
    endif
  endif
endif

# Enable retpoline support if the above holds and the compiler supports it.
ifneq (,$(CFLAGS_RETPOLINE))
  do_retpoline = $(call cc-option-yn, -mindirect-branch-register)
  subdir-ccflags-$(do_retpoline) += $(CFLAGS_RETPOLINE)
endif

obj-$(CONFIG_RDMA_RXE_DUMMY) += rdma_rxe.o

rdma_rxe-y := rdma_rxe_dummy.o
