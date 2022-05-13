#include "./kernel_helper.h"

#include <linux/kallsyms.h>

ib_sa_comp_mask
path_rec_service_id(void)
{
  return IB_SA_PATH_REC_SERVICE_ID;
}

ib_sa_comp_mask
path_rec_dgid(void)
{
  return IB_SA_PATH_REC_DGID;
}

ib_sa_comp_mask
path_rec_sgid(void)
{
  return IB_SA_PATH_REC_SGID;
}

ib_sa_comp_mask
path_rec_numb_path(void)
{
  return IB_SA_PATH_REC_NUMB_PATH;
}

#if defined(BASE_MLNX_OFED_LINUX_5_4_1_0_3_0) ||                               \
  defined(BASE_MLNX_OFED_LINUX_4_9_3_1_5_0)
int
bd_ib_post_send(struct ib_qp* qp,
                struct ib_send_wr* send_wr,
                struct ib_send_wr** bad_send_wr)
{
  return ib_post_send(qp, (const struct ib_send_wr *)send_wr, (const struct ib_send_wr **)bad_send_wr);
}

#else 
int
bd_ib_post_send(struct ib_qp* qp,
                struct ib_send_wr* send_wr,
                struct ib_send_wr** bad_send_wr)
{
  return ib_post_send(qp, send_wr, bad_send_wr);
}
#endif

#if defined(BASE_MLNX_OFED_LINUX_5_4_1_0_3_0) ||                              \
  defined(BASE_MLNX_OFED_LINUX_4_9_3_1_5_0)
int
bd_ib_post_recv(struct ib_qp* qp,
                struct ib_recv_wr* wr,
                struct ib_recv_wr** bad_send_wr)
{
  return ib_post_recv(qp, wr, (const struct ib_recv_wr **) bad_send_wr);
}

int
bd_ib_post_srq_recv(struct ib_srq* qp,
                struct ib_recv_wr* wr,
                struct ib_recv_wr** bad_send_wr)
{
  return ib_post_srq_recv(qp, wr, (const struct ib_recv_wr **) bad_send_wr);
}

#else
int
bd_ib_post_recv(struct ib_qp* qp,
                struct ib_recv_wr* wr,
                struct ib_recv_wr** bad_send_wr)
{
  return ib_post_recv(qp, wr, bad_send_wr);
}

int
bd_ib_post_srq_recv(struct ib_srq* qp,
                struct ib_recv_wr* wr,
                struct ib_recv_wr** bad_send_wr)
{
    return ib_post_srq_recv(qp, wr, bad_send_wr);
}
#endif

int
bd_ib_poll_cq(struct ib_cq* cq, int num_entries, struct ib_wc* wc)
{
  return ib_poll_cq(cq, num_entries, wc);
}

void
bd_rdma_ah_set_dlid(struct rdma_ah_attr* attr, unsigned int dlid)
{
  rdma_ah_set_dlid(attr, dlid);
}

void
bd_set_recv_wr_id(struct ib_recv_wr* wr, unsigned long long wr_id)
{
  wr->wr_id = wr_id;
}

int
bd_get_recv_wr_id(struct ib_recv_wr* wr)
{
  return wr->wr_id;
}

unsigned long long
bd_get_wc_wr_id(struct ib_wc* wc)
{
  return wc->wr_id;
}

int
gfp_highuser(void)
{
  return GFP_HIGHUSER;
}

unsigned int
page_size(void)
{
  return PAGE_SIZE;
}

unsigned int
dma_from_device(void)
{
  return DMA_FROM_DEVICE;
}

int
bd_ib_dma_map_sg(struct ib_device* dev,
                 struct scatterlist* sg,
                 int nents,
                 enum dma_data_direction direction)
{
  return ib_dma_map_sg(dev, sg, nents, direction);
}

void
bd_sg_set_page(struct scatterlist* sg,
               struct page* page,
               unsigned int len,
               unsigned int offset)
{
  return sg_set_page(sg, page, len, offset);
}

struct page*
bd_alloc_pages(int mask, int page_order)
{
  return alloc_pages(mask, page_order);
}
void
bd_get_page(struct page* page)
{
    get_page(page);
}

void
bd_free_pages(struct page* page, unsigned int order)
{
  __free_pages(page, order);
}

void*
bd_page_address(const struct page* page)
{
  return page_address(page);
}

struct page*
bd_virt_to_page(void* kaddr)
{
  return virt_to_page(kaddr);
}

long
ptr_is_err(const void* ptr)
{
  return IS_ERR(ptr);
}

#if defined(BASE_MLNX_OFED_LINUX_5_4_1_0_3_0) ||                               \
  defined(BASE_MLNX_OFED_LINUX_4_9_3_1_5_0)
struct ib_cq*
bd_ib_create_cq(struct ib_device* device,
                ib_comp_handler comp_handler,
                void (*event_handler)(struct ib_event*, void*),
                void* cq_context,
                const struct ib_cq_init_attr* cq_attr)
{
  return __ib_create_cq(device, comp_handler, event_handler, cq_context, cq_attr, "rust-kernel-rdma-base");
}
#endif