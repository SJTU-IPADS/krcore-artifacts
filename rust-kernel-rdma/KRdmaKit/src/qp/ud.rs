//! mod UD (unreliable datagram) primitive

use alloc::sync::Arc;

use crate::device::RContext;
use crate::rust_kernel_rdma_base::*;
// import explicitly
use crate::rust_kernel_rdma_base::linux_kernel_module;
use crate::qp::{create_ib_cq, create_ib_qp, get_qp_status};
use crate::to_ptr;
use linux_kernel_module::{println, Error, KernelResult};
use core::option::Option;
use core::ptr::null_mut;

#[derive(Debug)]
pub struct UD {
    qp: *mut ib_qp,
    cq: *mut ib_cq,
    recv_cq: *mut ib_cq,
    qkey: u32,
    shared_recv_cq: bool,
}

impl UD {
    #[inline]
    pub fn new_with_srq(ctx: &RContext, recv_cq: *mut ib_cq, srq: *mut ib_srq) -> Option<Arc<Self>> {
        let config = crate::qp::Config::new()
            .set_max_recv_wr_sz(MAX_UD_RECV_WR)
            .set_max_recv_cqe(MAX_UD_RECV_CQE);
        Self::new_from_config(config, ctx, recv_cq, srq)
    }

    #[inline]
    pub fn new(ctx: &RContext) -> Option<Arc<Self>> {
        let config = crate::qp::Config::new()
            .set_max_recv_wr_sz(MAX_UD_RECV_WR)
            .set_max_recv_cqe(MAX_UD_RECV_CQE);
        Self::new_from_config(config, ctx, null_mut(), null_mut())
    }

    pub fn new_from_config(config: crate::qp::Config, ctx: &RContext,
                           recv_cq: *mut ib_cq, srq: *mut ib_srq) -> Option<Arc<Self>> {
        let res = Self {
            qp: core::ptr::null_mut(),
            cq: core::ptr::null_mut(),
            recv_cq: core::ptr::null_mut(),
            shared_recv_cq: !recv_cq.is_null(),
            qkey: config.qkey,
        };

        let mut boxed = Arc::new(res);
        let qp = unsafe { Arc::get_mut_unchecked(&mut boxed) };
        let mut cq_attr: ib_cq_init_attr = Default::default();
        cq_attr.cqe = config.max_cqe as u32;
        // 1. create cq
        let cq = create_ib_cq(ctx.get_raw_dev(), &mut cq_attr as *mut _);
        if cq.is_null() {
            return None;
        }
        // 2. create recv_cq.
        cq_attr.cqe = config.max_recv_cqe as u32;
        let recv_cq = if recv_cq.is_null() {
            create_ib_cq(ctx.get_raw_dev(), &mut cq_attr as *mut _)
        } else {
            recv_cq
        };
        if recv_cq.is_null() {
            return None;
        }
        // 3. create qp
        let ib_qp = create_ib_qp(&config, ctx.get_pd(),
                                 ib_qp_type::IB_QPT_UD as u32,  // of type `UD`
                                 cq, recv_cq, srq);
        if ib_qp.is_null() {
            return None;
        }

        // 4. finish init, then fill the qp fields
        qp.cq = cq;
        qp.recv_cq = recv_cq;
        qp.qp = ib_qp;

        // bring ud to state to RTR and RTS
        match qp.bring_ud_to_ready() {
            Ok(_) => {}
            Err(e) => {
                println!("ud QP in cm reply bring to RTR err {:?}", e);
            }
        };
        Some(boxed)
    }
}

/// Getter for UD
impl UD {
    #[inline]
    pub fn get_qp_num(&self) -> u32 {
        unsafe { (*self.qp).qp_num }
    }

    #[inline]
    pub fn get_qkey(&self) -> u32 {
        self.qkey
    }

    #[inline]
    pub fn get_qp(&self) -> *mut ib_qp {
        self.qp
    }

    #[inline]
    pub fn get_cq(&self) -> *mut ib_cq {
        self.cq
    }

    #[inline]
    pub fn get_recv_cq(&self) -> *mut ib_cq {
        self.recv_cq
    }


    #[inline]
    pub fn get_pd(&self) -> *mut ib_pd { unsafe { (*self.qp).pd } }
}

/// State change
impl UD {
    pub fn bring_ud_to_ready(&mut self) -> KernelResult<()> {
        if get_qp_status(self.qp).unwrap() == ib_qp_state::IB_QPS_RESET {
            let mut attr: ib_qp_attr = Default::default();
            // INIT
            attr.qp_state = ib_qp_state::IB_QPS_INIT;
            attr.pkey_index = 0;
            attr.port_num = 1;
            attr.qkey = self.qkey;
            let mask = ib_qp_attr_mask::IB_QP_STATE | ib_qp_attr_mask::IB_QP_PKEY_INDEX |
                ib_qp_attr_mask::IB_QP_PORT | ib_qp_attr_mask::IB_QP_QKEY;

            let ret = unsafe { ib_modify_qp(self.qp, to_ptr!(attr), mask) };
            if ret != 0 {
                println!("bring ready to init error!");
                return Err(Error::from_kernel_errno(ret));
            }

            // RTR
            let mut attr: ib_qp_attr = Default::default();
            attr.qp_state = ib_qp_state::IB_QPS_RTR;

            let ret = unsafe {
                ib_modify_qp(self.qp, to_ptr!(attr),
                             ib_qp_attr_mask::IB_QP_STATE)
            };
            if ret != 0 {
                println!("bring ready to recv error!");
                return Err(Error::from_kernel_errno(ret));
            }

            // RTS
            let mut attr: ib_qp_attr = Default::default();
            attr.qp_state = ib_qp_state::IB_QPS_RTS;
            attr.sq_psn = self.get_qp_num();

            let ret = unsafe {
                ib_modify_qp(self.qp, to_ptr!(attr),
                             ib_qp_attr_mask::IB_QP_STATE | ib_qp_attr_mask::IB_QP_SQ_PSN)
            };
            if ret != 0 {
                println!("bring ready to send error!");
                return Err(Error::from_kernel_errno(ret));
            }
        }
        Ok(())
    }

    pub fn bring_to_err(&mut self) -> KernelResult<()> {
        let mut attr: ib_qp_attr = Default::default();
        attr.qp_state = ib_qp_state::IB_QPS_ERR;

        let mut mask: linux_kernel_module::c_types::c_int = 0;
        mask |= ib_qp_attr_mask::IB_QP_STATE;
        let ret = unsafe { ib_modify_qp(self.qp, to_ptr!(attr), mask) };
        if ret != 0 {
            return Err(Error::from_kernel_errno(ret));
        }
        Ok(())
    }
}

impl Drop for UD {
    fn drop(&mut self) {
        let _qpn = self.get_qp_num();
        // free qp
        if !self.qp.is_null() {
            unsafe { ib_destroy_qp(self.qp) };
        }
        // free cq
        if !self.cq.is_null() {
            unsafe { ib_free_cq(self.cq) };
        }

        // free recv cq
        if !self.shared_recv_cq && !self.recv_cq.is_null() {
            unsafe { ib_free_cq(self.recv_cq) };
        }
        // linux_kernel_module::println!("destroy UD done, qpn:{}", qpn);
    }
}

impl Default for UD {
    fn default() -> Self {
        Self {
            qp: core::ptr::null_mut(),
            cq: core::ptr::null_mut(),
            recv_cq: core::ptr::null_mut(),
            qkey: 0,
            shared_recv_cq: false,
        }
    }
}

unsafe impl Sync for UD {}

unsafe impl Send for UD {}

