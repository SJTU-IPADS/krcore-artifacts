/// we use wrapper to provide uniform syscalls for all functions
use crate::bindings::*;
use crate::linux_kernel_module::c_types;

#[inline]
pub unsafe fn ib_alloc_pd(dev: *mut ib_device, flags: c_types::c_uint) -> *mut ib_pd {
    #[cfg(BASE_MLNX_OFED_LINUX_4_4_2_0_7_0)]
    return __ib_alloc_pd(dev, flags, crate::kModelName.as_ptr() as *const i8, false);

    #[cfg(BASE_MLNX_OFED_LINUX_5_4_1_0_3_0)]
    return __ib_alloc_pd(dev, flags, crate::kModelName.as_ptr() as *const i8, false);

    #[cfg(BASE_MLNX_OFED_LINUX_4_9_3_1_5_0)]
    return __ib_alloc_pd(dev, flags, crate::kModelName.as_ptr() as *const i8, false);

    #[cfg(BASE_MLNX_OFED_LINUX_4_2_1_2_0_0)]
    return __ib_alloc_pd(dev, flags, crate::kModelName.as_ptr() as *const i8);
}

pub use crate::bindings::ib_query_port;
/// context-related functions
pub use crate::bindings::ib_register_client;
pub use crate::bindings::ib_unregister_client;

pub use crate::bindings::bd_get_recv_wr_id;
pub use crate::bindings::bd_get_wc_wr_id;
pub use crate::bindings::bd_ib_poll_cq;
pub use crate::bindings::bd_ib_post_recv;
pub use crate::bindings::bd_ib_post_send;
pub use crate::bindings::bd_ib_post_srq_recv;
pub use crate::bindings::bd_rdma_ah_set_dlid;
pub use crate::bindings::bd_set_recv_wr_id;
/// QP related functions
pub use crate::bindings::ib_modify_qp;
pub use crate::bindings::ib_query_qp;
pub use crate::bindings::rdma_create_ah;

pub use crate::bindings::ib_cm_init_qp_attr;
pub use crate::bindings::ib_cm_listen;
/// CM related functions;
pub use crate::bindings::ib_create_cm_id;
pub use crate::bindings::ib_destroy_cm_id;
pub use crate::bindings::ib_sa_path_rec_get;
pub use crate::bindings::ib_sa_register_client;
pub use crate::bindings::ib_sa_unregister_client;
pub use crate::bindings::ib_send_cm_drep;
pub use crate::bindings::ib_send_cm_dreq;
pub use crate::bindings::ib_send_cm_rep;
pub use crate::bindings::ib_send_cm_req;
pub use crate::bindings::ib_send_cm_rtu;
pub use crate::bindings::ib_send_cm_sidr_rep;
pub use crate::bindings::ib_send_cm_sidr_req;
pub use crate::bindings::path_rec_dgid;
pub use crate::bindings::path_rec_numb_path;
pub use crate::bindings::path_rec_service_id;
pub use crate::bindings::path_rec_sgid;

pub use crate::bindings::bd_alloc_pages;
pub use crate::bindings::bd_free_pages;
pub use crate::bindings::bd_ib_dma_map_sg;
pub use crate::bindings::bd_page_address;
pub use crate::bindings::bd_sg_set_page;
pub use crate::bindings::bd_virt_to_page;
pub use crate::bindings::bd_get_page;
pub use crate::bindings::dma_from_device;
pub use crate::bindings::gfp_highuser;

/// MR related functions;
pub use crate::bindings::ib_get_dma_mr;
pub use crate::bindings::ib_map_mr_sg;
pub use crate::bindings::page_size;
pub use crate::bindings::sg_init_table;
pub use crate::bindings::vmalloc_to_page;

pub use crate::bindings::ptr_is_err;
