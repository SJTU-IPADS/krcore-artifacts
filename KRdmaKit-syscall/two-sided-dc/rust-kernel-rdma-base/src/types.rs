use crate::bindings::*;

use no_std_net::Guid;

// some minor utilities for types
impl ib_wc {
    pub fn get_wr_id(&self) -> u64 {
        unsafe { self.__bindgen_anon_1.wr_id }
    }
}

impl core::fmt::Debug for ib_gid {
    fn fmt(&self, fmt: &mut ::core::fmt::Formatter) -> core::fmt::Result {
        let guid = Guid::new_u8(unsafe { &self.raw });
        write!(fmt, "{}", guid)
    }
}

impl list_head {
    pub const fn new_as_nil() -> Self {
        Self {
            next: core::ptr::null_mut(),
            prev: core::ptr::null_mut(),
        }
    }
}

unsafe impl Send for ib_device {}

unsafe impl Sync for ib_device {}

unsafe impl Sync for ib_cq {}

unsafe impl Send for ib_cq {}

unsafe impl Sync for ib_qp {}

unsafe impl Send for ib_qp {}

/// All binding uses as below.
/// The module which depends on the `rust-kernel-base` could import all of the
/// bindings related to RDMA at once via `use rust_kernel_rdma_base::*;`.

/// context related 
pub use crate::bindings::dma_addr_t;
pub use crate::bindings::ib_device;
pub use crate::bindings::ib_client;
pub use crate::bindings::ib_sa_client;
pub use crate::bindings::ib_gid;
pub use crate::bindings::ib_pd;
pub use crate::bindings::ib_udata;
pub use crate::bindings::ib_access_flags;
pub use crate::bindings::ib_device_attr;
pub use crate::bindings::ib_port_attr;
pub use crate::bindings::ib_port_state;

/// QP related
pub use crate::bindings::ib_cq_init_attr;
pub use crate::bindings::ib_cq;
pub use crate::bindings::ib_qp_type;
pub use crate::bindings::ib_qp_attr_mask;
pub use crate::bindings::ib_qp_attr;
pub use crate::bindings::ib_qp_state;
pub use crate::bindings::ib_qp;
pub use crate::bindings::ib_sig_type;
pub use crate::bindings::ib_qp_init_attr;
pub use crate::bindings::ib_sge;
pub use crate::bindings::ib_rdma_wr;
pub use crate::bindings::ib_recv_wr;
pub use crate::bindings::ib_ud_wr;
pub use crate::bindings::ib_wc;
pub use crate::bindings::ib_wr_opcode;
pub use crate::bindings::ib_send_flags;
pub use crate::bindings::ib_send_wr;
pub use crate::bindings::ib_reg_wr;
pub use crate::bindings::rdma_ah_attr;
pub use crate::bindings::ib_ah;

/// MR related;
pub use crate::bindings::ib_mr;
pub use crate::bindings::ib_mr_type;
pub use crate::bindings::page;
pub use crate::bindings::scatterlist;

/// cm related
pub use crate::bindings::sa_family_t;
pub use crate::bindings::ib_cm_id;
pub use crate::bindings::sa_path_rec;
pub use crate::bindings::ib_sa_query;
pub use crate::bindings::ib_cm_sidr_status;
pub use crate::bindings::ib_cm_event;
pub use crate::bindings::ib_cm_event_type;
pub use crate::bindings::ib_cm_state;
pub use crate::bindings::ib_cm_handler;
pub use crate::bindings::ib_cm_req_param;
pub use crate::bindings::ib_cm_rep_param;
pub use crate::bindings::ib_cm_sidr_rep_param;
pub use crate::bindings::ib_cm_sidr_req_param;

pub use crate::bindings::ib_srq;
pub use crate::bindings::ib_srq_init_attr;

pub use crate::bindings::ib_mtu;

/// dct related
// The ib_dc_wr is implemented by our instrumented driver
// Hence, it is not supported on common ofed drivers
#[cfg(feature = "dct")]
mod _private_dct {
    pub use crate::bindings::ib_exp_qp_init_attr;
    pub use crate::bindings::ib_dc_wr;
    pub use crate::bindings::ib_dct;
    pub use crate::bindings::ib_dct_init_attr;
}

#[cfg(feature = "dct")]
pub use _private_dct::{ib_exp_qp_init_attr,ib_dc_wr,ib_dct,ib_dct_init_attr};
