# SPDX-License-Identifier: GPL-2.0

obj-$(CONFIG_NVME_TARGET)		+= nvmet.o
obj-$(CONFIG_NVME_TARGET_LOOP)		+= nvme-loop.o
obj-$(CONFIG_NVME_TARGET_RDMA)		+= nvmet-rdma.o
obj-$(CONFIG_NVME_TARGET_FC)		+= nvmet-fc.o
obj-$(CONFIG_NVME_TARGET_FCLOOP)	+= nvme-fcloop.o
obj-$(CONFIG_NVME_TARGET_TCP)		+= nvmet-tcp.o

obj-$(CONFIG_NVME_TARGET_DUMMY) += nvmet-rdma.o

# --with-nvmf-host-rdma-only
# dummy
obj-$(CONFIG_NVME_HOST_WITHOUT_FC)		+= nvmet.o
obj-$(CONFIG_NVME_HOST_WITHOUT_FC)		+= nvme-loop.o
obj-$(CONFIG_NVME_HOST_WITHOUT_FC)		+= nvmet-rdma.o
obj-$(CONFIG_NVME_HOST_WITHOUT_FC)		+= nvmet-fc.o
obj-$(CONFIG_NVME_HOST_WITHOUT_FC)		+= nvme-fcloop.o

ifeq ($(CONFIG_NVME_TARGET_DUMMY),m)
nvmet-rdma-y	+= nvmet-rdma_dummy.o
else
ifeq ($(CONFIG_NVME_HOST_WITHOUT_FC),m)
nvmet-y		+= nvmet_dummy.o
nvme-loop-y	+= nvme-loop_dummy.o
nvmet-rdma-y	+= nvmet-rdma_dummy.o
nvmet-fc-y	+= nvmet-fc_dummy.o
nvme-fcloop-y	+= nvme-fcloop_dummy.o
else
nvmet-y		+= core.o configfs.o admin-cmd.o fabrics-cmd.o \
			discovery.o io-cmd-file.o io-cmd-bdev.o
nvme-loop-y	+= loop.o
nvmet-rdma-y	+= rdma.o
nvmet-fc-y	+= fc.o
nvme-fcloop-y	+= fcloop.o
nvmet-tcp-y	+= tcp.o
endif
endif
