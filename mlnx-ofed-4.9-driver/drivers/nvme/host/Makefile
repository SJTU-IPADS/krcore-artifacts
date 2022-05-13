# SPDX-License-Identifier: GPL-2.0

ccflags-y				+= -I$(src)

obj-$(CONFIG_NVME_CORE)			+= nvme-core.o
obj-$(CONFIG_BLK_DEV_NVME)		+= nvme.o
obj-$(CONFIG_NVME_FABRICS)		+= nvme-fabrics.o
obj-$(CONFIG_NVME_RDMA)			+= nvme-rdma.o
obj-$(CONFIG_NVME_FC)			+= nvme-fc.o
obj-$(CONFIG_NVME_TCP)			+= nvme-tcp.o

obj-$(CONFIG_NVME_HOST_DUMMY)		+= nvme-rdma.o

# --with-nvmf-host-rdma-only
# dummy
obj-$(CONFIG_NVME_HOST_WITHOUT_FC)		+= nvme-fc.o

ifeq ($(CONFIG_NVME_HOST_DUMMY),m)
nvme-rdma-y				:= nvme-rdma_dummy.o
else
nvme-core-y				:= core.o
nvme-core-$(CONFIG_TRACING)		+= trace.o
nvme-core-$(CONFIG_NVME_MULTIPATH)	+= multipath.o
nvme-core-$(CONFIG_NVM)			+= lightnvm.o
nvme-core-$(CONFIG_FAULT_INJECTION_DEBUG_FS)	+= fault_inject.o

nvme-y					+= pci.o

nvme-fabrics-y				+= fabrics.o

nvme-rdma-y				+= rdma.o

ifeq ($(CONFIG_NVME_HOST_WITHOUT_FC),m)
nvme-fc-y				+= nvme-fc_dummy.o
else
nvme-fc-y				+= fc.o
endif

nvme-tcp-y				+= tcp.o

endif

# these 2 drivers depend on nvme-fc, so replace them with dummy drivers
ifneq ($(filter m,$(CONFIG_NVME_FC) $(CONFIG_NVME_HOST_WITHOUT_FC)),)
obj-$(CONFIG_SCSI_QLA_FC) += qla2xxx.o
qla2xxx-y := qla2xxx_dummy.o

obj-$(CONFIG_TCM_QLA2XXX) += tcm_qla2xxx.o
tcm_qla2xxx-y := tcm_qla2xxx_dummy.o
endif
