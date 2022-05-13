#ifndef UVERBS_EXP_H
#define UVERBS_EXP_H

#include <linux/kref.h>
#include <linux/idr.h>
#include <linux/mutex.h>
#include <linux/completion.h>
#include <linux/cdev.h>

#include <rdma/ib_verbs.h>
#include <rdma/ib_umem.h>
#include <rdma/ib_user_verbs.h>

struct ib_udct_object {
	struct ib_uevent_object	uevent;
};

#define IB_UVERBS_DECLARE_EXP_CMD(name)				\
	int ib_uverbs_exp_##name(struct uverbs_attr_bundle *attrs)

IB_UVERBS_DECLARE_EXP_CMD(create_qp);
IB_UVERBS_DECLARE_EXP_CMD(modify_cq);
IB_UVERBS_DECLARE_EXP_CMD(query_device);
IB_UVERBS_DECLARE_EXP_CMD(create_cq);
IB_UVERBS_DECLARE_EXP_CMD(modify_qp);
IB_UVERBS_DECLARE_EXP_CMD(reg_mr);
IB_UVERBS_DECLARE_EXP_CMD(create_dct);
IB_UVERBS_DECLARE_EXP_CMD(destroy_dct);
IB_UVERBS_DECLARE_EXP_CMD(query_dct);
IB_UVERBS_DECLARE_EXP_CMD(arm_dct);
IB_UVERBS_DECLARE_EXP_CMD(create_mr);
IB_UVERBS_DECLARE_EXP_CMD(prefetch_mr);
IB_UVERBS_DECLARE_EXP_CMD(create_flow);
IB_UVERBS_DECLARE_EXP_CMD(query_mkey);
IB_UVERBS_DECLARE_EXP_CMD(create_wq);
IB_UVERBS_DECLARE_EXP_CMD(modify_wq);
IB_UVERBS_DECLARE_EXP_CMD(destroy_wq);
IB_UVERBS_DECLARE_EXP_CMD(create_rwq_ind_table);
IB_UVERBS_DECLARE_EXP_CMD(destroy_rwq_ind_table);
IB_UVERBS_DECLARE_EXP_CMD(set_context_attr);
IB_UVERBS_DECLARE_EXP_CMD(create_srq);
IB_UVERBS_DECLARE_EXP_CMD(alloc_dm);
IB_UVERBS_DECLARE_EXP_CMD(free_dm);

unsigned long ib_uverbs_exp_get_unmapped_area(struct file *filp,
					      unsigned long addr,
					      unsigned long len, unsigned long pgoff,
					      unsigned long flags);
long ib_uverbs_exp_ioctl(struct file *filp,
			 unsigned int cmd, unsigned long arg);

void ib_uverbs_dct_event_handler(struct ib_event *event, void *context_ptr);

int ib_uverbs_create_flow_common(struct uverbs_attr_bundle *attrs, bool is_exp);

int ib_uverbs_exp_create_srq_resp(struct ib_uverbs_create_srq_resp *resp,
				  u64 response);

void uverbs_uobj_event(struct ib_uevent_object *eobj, struct ib_event *event);
#endif
