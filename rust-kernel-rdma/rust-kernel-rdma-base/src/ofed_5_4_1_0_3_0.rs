#[macro_export]
macro_rules! gen_add_dev_func {
    ($fn_name:ident, $new_fn_name:ident) => {
        unsafe extern "C" fn $new_fn_name(dev: *mut ib_device) -> i32 {
            $fn_name(dev);
            0
        }
    };
}

/// we use wrapper to provide uniform syscalls for all functions
use crate::bindings::*;
use crate::linux_kernel_module::c_types;

#[inline]
pub unsafe fn ib_dereg_mr(mr: *mut ib_mr) -> c_types::c_int {
    ib_dereg_mr_user(mr, core::ptr::null_mut())
}

#[inline]
pub unsafe fn ib_dealloc_pd(pd: *mut ib_pd) {
    ib_dealloc_pd_user(pd, core::ptr::null_mut());
}

#[inline]
pub unsafe fn ib_destroy_qp(qp: *mut ib_qp) -> c_types::c_int {
    ib_destroy_qp_user(qp, core::ptr::null_mut())
}

#[inline]
pub unsafe fn ib_free_cq(cq: *mut ib_cq) {
    ib_free_cq_user(cq, core::ptr::null_mut());
}

#[inline]
pub unsafe fn ib_create_cq(
    device: *mut ib_device,
    comp_handler: ib_comp_handler,
    event_handler: ::core::option::Option<
        unsafe extern "C" fn(arg1: *mut ib_event, arg2: *mut c_types::c_void),
    >,
    cq_context: *mut c_types::c_void,
    cq_attr: *const ib_cq_init_attr,
) -> *mut ib_cq {
    crate::bindings::bd_ib_create_cq(device, comp_handler, event_handler, cq_context, cq_attr)
}

#[inline]
pub unsafe fn ib_create_srq(pd: *mut ib_pd, attr: *mut ib_srq_init_attr) -> *mut ib_srq {
    ib_create_srq_user(pd, attr, core::ptr::null_mut(), core::ptr::null_mut())
}

#[inline]
pub unsafe fn rdma_destroy_ah(ah: *mut ib_ah, flags: u32) -> c_types::c_int {
    rdma_destroy_ah_user(ah, flags, core::ptr::null_mut())
}

#[inline]
pub unsafe fn ib_destroy_srq(srq: *mut ib_srq) -> c_types::c_int {
    ib_destroy_srq_user(srq, core::ptr::null_mut())
}
