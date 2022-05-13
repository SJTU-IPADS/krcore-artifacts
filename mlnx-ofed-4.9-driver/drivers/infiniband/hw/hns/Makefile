ifneq ($(filter m,$(CONFIG_INFINIBAND_HNS) $(CONFIG_INFINIBAND_HISILICON_HNS)),)
obj-m += hns-roce.o
endif

hns-roce-y  := main.o

obj-$(CONFIG_INFINIBAND_HNS_HIP06) += hns-roce-hw-v1.o
hns-roce-hw-v1-objs := hns_roce_hw_v1.o
obj-$(CONFIG_INFINIBAND_HNS_HIP08) += hns-roce-hw-v2.o
hns-roce-hw-v2-objs := hns_roce_hw_v2.o

