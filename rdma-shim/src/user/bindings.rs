pub use rust_user_rdma::*;

#[allow(non_camel_case_types)]
mod wrapper_types {
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
