pub use rust_user_rdma::*;

#[allow(non_camel_case_types)]
mod wrapper_types {
    use crate::ffi::c_types::c_int;
    use super::*;

    /// We follow the convention of the kernel to rename the user-space verbs types
    /// types:
    pub type ib_device = ibv_device;
    pub type ib_device_attr = ibv_device_attr;
    pub type ib_port_attr = ibv_port_attr;

    pub type ib_pd = ibv_pd;
    pub type ib_ah = ibv_ah;
    pub type ib_ah_attr = ibv_ah_attr;
    pub type ib_gid = ibv_gid;

    pub type rdma_ah_attr = ibv_ah_attr;

    // CQ related types
    pub type ib_cq = ibv_cq;
    pub type ib_srq = ibv_srq;
    pub type ib_wc = ibv_wc;
    pub type ib_srq_init_attr = ibv_srq_init_attr;

    // QP related types

    /// functions

    #[inline(always)]
    pub unsafe fn ib_dealloc_pd(pd: *mut ib_pd) {
        ibv_dealloc_pd(pd);
    }

    #[inline(always)]
    pub unsafe fn rdma_create_ah_wrapper(pd: *mut ib_pd, attr: *mut ib_ah_attr) -> *mut ib_ah {
        ibv_create_ah(pd, attr)
    }

    #[inline(always)]
    pub unsafe fn rdma_destroy_ah(ah: *mut ib_ah) {
        ibv_destroy_ah(ah);
    }

    #[inline(always)]
    pub unsafe fn ib_free_cq(cq: *mut ib_cq) {
        ibv_destroy_cq(cq);
    }

    #[inline(always)]
    pub unsafe fn ib_destroy_srq(srq: *mut ib_srq) {
        ibv_destroy_srq(srq);
    }

    #[inline(always)]
    pub unsafe fn ib_create_srq(pd: *mut ib_pd, attr: *mut ib_srq_init_attr) -> *mut ib_srq {
        ibv_create_srq(pd, attr)
    }
}

pub use wrapper_types::*;

#[allow(unused_unsafe)]
pub unsafe fn ptr_is_err<T>(ptr: *mut T) -> super::ffi::c_types::c_int {
    if ptr.is_null() {
        1
    } else {
        0
    }
}
