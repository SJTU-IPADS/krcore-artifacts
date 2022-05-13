#ifndef KBUILD_MODNAME
#define KBUILD_MODNAME "rust-kernel-rdma-base"
#endif

#include <rdma/ib_cm.h>
#include <rdma/ib_sa.h>
#include <rdma/ib_verbs.h>
#include <rdma/rdma_cm.h>


ib_sa_comp_mask
path_rec_service_id(void);

ib_sa_comp_mask
path_rec_dgid(void);

ib_sa_comp_mask
path_rec_sgid(void);

ib_sa_comp_mask
path_rec_numb_path(void);

int
bd_ib_poll_cq(struct ib_cq *cq, int num_entries, struct ib_wc *wc);

int
bd_ib_post_send(struct ib_qp *qp,
                struct ib_send_wr *send_wr,
                struct ib_send_wr **bad_send_wr);

int
bd_ib_post_recv(struct ib_qp *qp,
                struct ib_recv_wr *wr,
                struct ib_recv_wr **bad_send_wr);

int
bd_ib_post_srq_recv(struct ib_srq *qp,
                struct ib_recv_wr *wr,
                struct ib_recv_wr **bad_send_wr);


void
bd_rdma_ah_set_dlid(struct rdma_ah_attr *attr, unsigned int dlid);

void
bd_set_recv_wr_id(struct ib_recv_wr *wr, unsigned long long wr_id);

int
bd_get_recv_wr_id(struct ib_recv_wr *wr);

unsigned long long
bd_get_wc_wr_id(struct ib_wc *wc);

int bd_ib_dma_map_sg(struct ib_device *dev,
                     struct scatterlist *sg, int nents,
                     enum dma_data_direction direction);

void bd_sg_set_page(struct scatterlist *sg, struct page *page,
                    unsigned int len, unsigned int offset);

struct page *bd_alloc_pages(int mask, int page_order);

void
bd_get_page(struct page* page);

void *bd_page_address(const struct page *page);

int gfp_highuser(void);

unsigned int page_size(void);

unsigned int dma_from_device(void);

struct page* bd_virt_to_page(void* kaddr);

void bd_put_page(struct page* page) {
    put_page(page);
}

void*
get_free_page(int gfp_mask) {
    return __get_free_page(gfp_mask);
}

void bd_free_pages(struct page *page, unsigned int order);
long ptr_is_err(const void* ptr);

#ifdef ib_create_cq
struct ib_cq*
bd_ib_create_cq(struct ib_device* device,
             ib_comp_handler comp_handler,
             void (*event_handler)(struct ib_event*, void*),
             void* cq_context,
             const struct ib_cq_init_attr* cq_attr);
#endif