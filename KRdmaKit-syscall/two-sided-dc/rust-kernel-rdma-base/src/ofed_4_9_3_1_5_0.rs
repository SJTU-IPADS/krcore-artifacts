#[macro_export]
macro_rules! gen_add_dev_func {
    ($fn_name:ident, $new_fn_name:ident) => {
        unsafe extern "C" fn $new_fn_name(dev: *mut ib_device) {
            $fn_name(dev);
        }
    };
}

/// we use wrapper to provide uniform syscalls for all functions
use crate::bindings::*;
use crate::linux_kernel_module::c_types;

pub use crate::bindings::ib_create_srq;

#[inline]
pub unsafe fn ib_create_qp(pd : *mut ib_pd, qp_init_attr : *mut ib_qp_init_attr) -> *mut ib_qp {
    ib_create_qp_user(pd, qp_init_attr, core::ptr::null_mut())
}

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
pub unsafe fn rdma_create_ah_wrapper(pd : *mut ib_pd, ah_attr : *mut rdma_ah_attr) ->  *mut ib_ah { 
    crate::bindings::rdma_create_ah(pd, ah_attr, 0)
}

#[inline]
pub unsafe fn rdma_destroy_ah(ah: *mut ib_ah) -> c_types::c_int {
    rdma_destroy_ah_user(ah, 0, core::ptr::null_mut())
}

#[inline]
pub unsafe fn ib_destroy_srq(srq: *mut ib_srq) -> c_types::c_int {
    ib_destroy_srq_user(srq, core::ptr::null_mut())
}

#[inline]
pub unsafe fn ib_alloc_mr(pd: *mut ib_pd, mr_type: ib_mr_type::Type, max_num_sg: u32) -> *mut ib_mr { 
    ib_alloc_mr_user(pd, mr_type, max_num_sg, core::ptr::null_mut())
}

#[cfg(feature = "dct")]
mod _private_dct {
    use super::*;

    /// DCT related functions
    pub use crate::bindings::ib_exp_create_dct;
    pub use crate::bindings::ib_exp_destroy_dct;

    #[inline]
    pub unsafe fn safe_ib_create_dct(pd: *mut ib_pd, attr: *mut ib_dct_init_attr) -> *mut ib_dct {
        use core::ptr::null_mut;
        use linux_kernel_module::{println, Error};

        let dct = ib_exp_create_dct(pd, attr, null_mut());
        if ptr_is_err(dct as _) > 0 {
            println!(
                "create dct err error {:?}",
                Error::from_kernel_errno(-(dct as u64 as i64) as _)
            );
            return null_mut();
        }
        (*dct).device = (*pd).device;
        dct
    }

    #[inline]
    pub unsafe fn safe_ib_destroy_dct(dct : *mut ib_dct) -> c_types::c_int { 
        ib_exp_destroy_dct(dct, core::ptr::null_mut())
    }

    #[inline]
    pub unsafe fn ib_create_qp_dct(pd: *mut ib_pd, attr: *mut ib_exp_qp_init_attr) -> *mut ib_qp {
        use core::ptr::null_mut;

        use linux_kernel_module::{println, Error};

        let qp = ib_create_qp(pd, (attr as u64) as _);
        if ptr_is_err(qp as _) > 0 {
            println!(
                "[ib_create_qp_dct] check kernel error {:?}",
                Error::from_kernel_errno(-(qp as u64 as i64) as _)
            );
            return null_mut();
        }
        qp
    }
}


#[cfg(feature = "dct")]
pub use _private_dct::{ib_exp_create_dct, ib_exp_destroy_dct, safe_ib_create_dct,ib_create_qp_dct, safe_ib_destroy_dct};

//XD: for backward 
impl ib_device {
    #[inline]
    pub unsafe fn query_device(&mut self, device: *mut ib_device,
            device_attr: *mut ib_device_attr,
            udata: *mut ib_udata,
        ) -> c_types::c_int { 
        self.ops.query_device.unwrap()(device, device_attr, udata)        
    }

    #[inline]
    pub unsafe fn query_gid(&mut self, device: *mut ib_device,
            port_num: u8,
            index: c_types::c_int,
            gid: *mut ib_gid,
        ) -> c_types::c_int { 
        self.ops.query_gid.unwrap()(device, port_num, index, gid)
    }
}