#
# Build this dummy module only when building real nvme core module
#

ifneq ($(CONFIG_NVME_HOST_DUMMY),m)

obj-$(CONFIG_SCSI_LPFC) := lpfc.o

lpfc-y := lpfc_dummy.o

endif
