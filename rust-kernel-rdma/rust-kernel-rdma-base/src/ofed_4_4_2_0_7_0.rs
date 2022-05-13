#[macro_export]
macro_rules! gen_add_dev_func {
    ($fn_name:ident, $new_fn_name:ident) => {
        unsafe extern "C" fn $new_fn_name(dev: *mut ib_device) {
            $fn_name(dev);
        }
    };
}

pub use crate::bindings::ib_alloc_mr;
pub use crate::bindings::ib_create_qp;
pub use crate::bindings::ib_create_cq;
pub use crate::bindings::ib_create_srq;
pub use crate::bindings::ib_dealloc_pd;
pub use crate::bindings::ib_dereg_mr;
pub use crate::bindings::ib_destroy_qp;
pub use crate::bindings::ib_destroy_srq;
pub use crate::bindings::ib_free_cq;
pub use crate::bindings::rdma_destroy_ah;

use crate::bindings::*;
use crate::linux_kernel_module::c_types;

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
    pub unsafe fn ib_create_qp_dct(pd: *mut ib_pd, attr: *mut ib_exp_qp_init_attr) -> *mut ib_qp {
        use core::ptr::null_mut;

        use linux_kernel_module::{println, Error};

        let qp = ib_create_qp(pd, (attr as u64) as _);
        if ptr_is_err(qp as _) > 0 {
            println!(
                "check kernel error {:?}",
                Error::from_kernel_errno(-(qp as u64 as i64) as _)
            );
            return null_mut();
        }
        qp
    }
}


#[cfg(feature = "dct")]
pub use _private_dct::{ib_exp_create_dct, ib_exp_destroy_dct, safe_ib_create_dct,ib_create_qp_dct};

#[inline]
pub unsafe fn ib_alloc_cq(
    dev: *mut ib_device,
    private: *mut c_types::c_void,
    nr_cqe: c_types::c_int,
    comp_vector: c_types::c_int,
    poll_ctx: ib_poll_context,
) -> *mut ib_cq {
    __ib_alloc_cq(
        dev,
        private,
        nr_cqe,
        comp_vector,
        poll_ctx,
        crate::kModelName.as_ptr() as *const i8,
        false,
    )
}