//! mod RC. The reliable connected(RC) primitive
//!

use alloc::sync::Arc;

use crate::device::RContext;
use crate::rust_kernel_rdma_base::*;
// import explicitly
use crate::rust_kernel_rdma_base::linux_kernel_module;
use rust_kernel_linux_util::bindings::{completion, bd__builtin_bswap64};
use linux_kernel_module::{println, Error, KernelResult};
use core::option::Option;
use crate::qp::{create_ib_cq, create_ib_qp, get_qp_status};

use crate::{to_ptr, Profile};
use crate::cm::ClientCM;
use crate::qp::conn;
use crate::qp::conn::reply;
use crate::mem::TempMR;

pub struct RC {
    qp: *mut ib_qp,
    cq: *mut ib_cq,
    recv_cq: *mut ib_cq,
    srq: *mut ib_srq,
    done: completion,
    remote_mr: Option<TempMR>,
    pub cm: ClientCM,
    pub profile: Profile,
}

/// A high-level abstraction of RC PQ
impl RC {
    #[inline]
    pub fn new_from_config(config: crate::qp::Config, ctx: &RContext, cm: *mut ib_cm_id, recv_cq: *mut ib_cq) -> Option<Arc<Self>> {
        RC::new_with_srq(config, ctx, cm, core::ptr::null_mut(), recv_cq)
    }

    #[inline]
    pub fn new(ctx: &RContext, cm: *mut ib_cm_id, recv_cq: *mut ib_cq) -> Option<Arc<Self>> {
        let config: crate::qp::Config = Default::default();
        Self::new_from_config(config, ctx, cm, recv_cq)
    }


    #[inline]
    pub fn new_with_srq(config: crate::qp::Config, ctx: &RContext, cm: *mut ib_cm_id, srq: *mut ib_srq, recv_cq: *mut ib_cq) -> Option<Arc<Self>> {
        let res = Self {
            qp: core::ptr::null_mut(),
            cq: core::ptr::null_mut(),
            recv_cq: core::ptr::null_mut(),
            srq: core::ptr::null_mut(),
            remote_mr: None,
            done: Default::default(),
            cm: Default::default(),
            profile: Profile::new(),
        };

        let mut boxed = Arc::new(res);
        let qp = unsafe { Arc::get_mut_unchecked(&mut boxed) };
        // setup cq_attr
        let mut cq_attr: ib_cq_init_attr = Default::default();
        cq_attr.cqe = config.max_cqe as u32;

        // 1. create cq
        let cq = create_ib_cq(ctx.get_raw_dev(), &mut cq_attr as *mut _);
        if cq.is_null() {
            return None;
        }

        // 2. create recv_cq. In RC primitive, we just assign recv_cq = cq
        let recv_cq = if recv_cq.is_null() {
            cq
        } else {
            recv_cq
        };
        // let recv_cq = create_ib_cq(ctx.get_raw_dev(), &mut cq_attr as *mut _);
        if recv_cq.is_null() {
            return None;
        }

        // 3. create qp
        let ib_qp = create_ib_qp(&config, ctx.get_pd(),
                                 ib_qp_type::IB_QPT_RC as u32,
                                 cq, recv_cq, srq);
        if ib_qp.is_null() {
            return None;
        }

        // 4. init inner ClientCM
        if cm == core::ptr::null_mut() {
            let cm = ClientCM::new(
                ctx.get_raw_dev(),
                Some(RC::cm_handler),
                (qp as *mut RC).cast::<linux_kernel_module::c_types::c_void>(),
            );
            if cm.is_none() {
                return None;
            }
            qp.cm = cm.unwrap();
        } else {
            qp.cm = ClientCM::new_from_raw(cm);
        }


        // 5. finish init, then fill the qp fields
        qp.cq = cq;
        qp.recv_cq = recv_cq;
        qp.qp = ib_qp;
        qp.srq = srq;

        Some(boxed)
    }

    #[inline]
    pub fn get_status(&self) -> Option<u32> {
        get_qp_status(self.qp)
    }
}


/// Getter for RC
impl RC {
    #[inline]
    pub fn get_qp_num(&self) -> u32 {
        unsafe { (*self.qp).qp_num }
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
    pub fn get_srq(&self) -> *mut ib_srq { self.srq }

    #[inline]
    pub fn get_remote_mr(&self) -> TempMR { self.remote_mr.unwrap() }
}

/// State Transform of RC
/// Before putting RC into usage, the state of the qp has to be set
/// init -> ready to receive -> ready to receive. We split this process into
/// functions as below.
impl RC {
    // bring QP from ready to receive => ready to send
    pub fn bring_to_rts(&self, cm: *mut ib_cm_id) -> KernelResult<()> {
        if self.get_status().unwrap() == ib_qp_state::IB_QPS_RTR {
            let mut attr: ib_qp_attr = Default::default();
            let mut mask: linux_kernel_module::c_types::c_int = ib_qp_attr_mask::IB_QP_STATE;

            attr.qp_state = ib_qp_state::IB_QPS_RTS;
            let ret = unsafe { ib_cm_init_qp_attr(cm, to_ptr!(attr), to_ptr!(mask)) };
            if ret != 0 {
                return Err(Error::from_kernel_errno(ret));
            }

            let ret = unsafe { ib_modify_qp(self.get_qp(), to_ptr!(attr), mask) };
            if ret != 0 {
                println!("bring ready to send error!");
                return Err(Error::from_kernel_errno(ret));
            }
        }
        Ok(())
    }

    // bring QP from init -> ready to receive
    pub fn bring_to_rtr(&self, cm: *mut ib_cm_id) -> KernelResult<()> {
        // we should first check qp status
        self.bring_to_init(cm)?;

        if self.get_status().unwrap() == ib_qp_state::IB_QPS_INIT {
            let mut attr: ib_qp_attr = Default::default();
            let mut mask: linux_kernel_module::c_types::c_int = ib_qp_attr_mask::IB_QP_STATE;
            attr.qp_state = ib_qp_state::IB_QPS_RTR;

            let ret = unsafe { ib_cm_init_qp_attr(cm, to_ptr!(attr), to_ptr!(mask)) };
            if ret != 0 {
                println!("cm init qp attr error!");
                return Err(Error::from_kernel_errno(ret));
            }

            attr.rq_psn = self.get_qp_num();
            attr.max_dest_rd_atomic = crate::consts::MAX_RD_ATOMIC as u8;
            mask |= ib_qp_attr_mask::IB_QP_RQ_PSN | ib_qp_attr_mask::IB_QP_MAX_DEST_RD_ATOMIC;

            let ret = unsafe { ib_modify_qp(self.get_qp(), to_ptr!(attr), mask) };
            if ret != 0 {
                println!("bring init to recv error!");
                return Err(Error::from_kernel_errno(ret));
            }
        } else {}
        Ok(())
    }

    pub fn bring_to_err(&self) -> KernelResult<()> {
        let mut attr: ib_qp_attr = Default::default();
        attr.qp_state = ib_qp_state::IB_QPS_ERR;

        let mut mask: linux_kernel_module::c_types::c_int = 0;
        mask |= ib_qp_attr_mask::IB_QP_STATE;
        let ret = unsafe { ib_modify_qp(self.get_qp(), to_ptr!(attr), mask) };
        if ret != 0 {
            return Err(Error::from_kernel_errno(ret));
        }
        Ok(())
    }

    pub fn bring_to_reset(&self) -> KernelResult<()> {
        let mut attr: ib_qp_attr = Default::default();
        attr.qp_state = ib_qp_state::IB_QPS_RESET;

        let mut mask: linux_kernel_module::c_types::c_int = 0;
        mask |= ib_qp_attr_mask::IB_QP_STATE;
        let ret = unsafe { ib_modify_qp(self.get_qp(), to_ptr!(attr), mask) };
        if ret != 0 {
            println!("[bring_to_reset] cm reset qp attr error!");
            return Err(Error::from_kernel_errno(ret));
        }
        Ok(())
    }

    // bring QP from reset -> init
    pub fn bring_to_init(&self, cm: *mut ib_cm_id) -> KernelResult<()> {
        // we should first check qp status
        if self.get_status().unwrap() == ib_qp_state::IB_QPS_RESET || self.get_status().unwrap() == ib_qp_state::IB_QPS_ERR {
            // println!("real bring to init with cm {:?}", self.cm);
            // bring if only in a reset state
            let mut attr: ib_qp_attr = Default::default();
            let mut mask: linux_kernel_module::c_types::c_int = ib_qp_attr_mask::IB_QP_STATE;

            attr.qp_state = ib_qp_state::IB_QPS_INIT;

            let ret = unsafe { ib_cm_init_qp_attr(cm, to_ptr!(attr), to_ptr!(mask)) };
            if ret != 0 {
                println!("[bring_to_init] cm init qp attr error!");
                return Err(Error::from_kernel_errno(ret));
            }
            let access_flags = 0
                | ib_access_flags::IB_ACCESS_LOCAL_WRITE
                | ib_access_flags::IB_ACCESS_REMOTE_ATOMIC
                | ib_access_flags::IB_ACCESS_REMOTE_READ
                | ib_access_flags::IB_ACCESS_REMOTE_WRITE;
            attr.qp_access_flags |= access_flags as i32;

            mask |= ib_qp_attr_mask::IB_QP_ACCESS_FLAGS;
            let ret = unsafe { ib_modify_qp(self.get_qp(), to_ptr!(attr), mask) };
            if ret != 0 {
                println!("[bring_to_init] bring reset to init error!");
                return Err(Error::from_kernel_errno(ret));
            }
        }
        Ok(())
    }
}

impl RC {
    pub fn connect_reply(&self, event: &ib_cm_event, cm: &ClientCM, reply: reply::Payload) -> KernelResult<()> {
        let req = &unsafe { event.param.req_rcvd };

        let mut rep: ib_cm_rep_param = Default::default();

        rep.qp_num = self.get_qp_num();
        rep.starting_psn = self.get_qp_num();
        rep.initiator_depth = req.initiator_depth;
        rep.flow_control = req.flow_control() as u8;
        rep.responder_resources = req.responder_resources;
        rep.rnr_retry_count = req.rnr_retry_count() as u8;

        // 0 is a dummy private data
        cm.send_reply(rep, reply)
    }
}

impl Drop for RC {
    fn drop(&mut self) {
        let qpn = self.get_qp_num();
        // free qp
        if !self.qp.is_null() {
            unsafe { ib_destroy_qp(self.qp) };
        }
        // free cq
        if !self.cq.is_null() {
            unsafe { ib_free_cq(self.cq) };
        }

        // no need to free the shared recv cq

        self.cm.reset();
        // println!("destroy RC done, qpn:{}", qpn);
    }
}

impl Default for RC {
    fn default() -> Self {
        Self {
            qp: core::ptr::null_mut(),
            cq: core::ptr::null_mut(),
            recv_cq: core::ptr::null_mut(),
            srq: core::ptr::null_mut(),
            remote_mr: None,
            done: Default::default(),
            cm: Default::default(),
            profile: Profile::new(),
        }
    }
}

/// CM related functions
impl RC {
    /// CM callback handler shown as below.
    ///
    /// cm handler for RC primitive
    pub unsafe extern "C" fn cm_handler(
        cm_id: *mut ib_cm_id,
        env: *const ib_cm_event,
    ) -> linux_kernel_module::c_types::c_int {
        let event = *env;
        let cm: &mut ib_cm_id = &mut *cm_id;
        let qp: &mut RC = &mut *(cm.context as *mut RC);

        match event.event {
            ib_cm_event_type::IB_CM_REP_RECEIVED => {
                #[cfg(feature = "profile")]
                    qp.profile.tick_record(1);
                qp.handle_reply(&event);
                #[cfg(feature = "profile")]
                    qp.profile.tick_record(4);
            }
            ib_cm_event_type::IB_CM_REJ_RECEIVED => {
                println!("QP cm reply rejected!");
            }
            ib_cm_event_type::IB_CM_TIMEWAIT_EXIT => {
                println!("QP cm reply time wait exit!")
            }
            ib_cm_event_type::IB_CM_DREP_RECEIVED => {
                println!("QP cm reply derep received!");
                qp.done.done();
            }
            // others should be handled later
            _ => {
                println!("sunknown QP cm event: {}", event.event);
            }
        };
        0
    }

    pub fn connect(&mut self, qd: u64, path: sa_path_rec, service_id: u64) -> KernelResult<()> {
        #[cfg(feature = "profile")]
            self.profile.reset_timer();
        self.done.init();
        let mut req: ib_cm_req_param = Default::default();

        let mut s_path = path.clone();
        req.primary_path = &mut s_path as *mut sa_path_rec;

        req.service_id = unsafe { bd__builtin_bswap64(service_id as u64) };
        req.responder_resources = 16;
        req.initiator_depth = 16;
        req.remote_cm_response_timeout = 20;
        req.local_cm_response_timeout = 20;
        req.max_cm_retries = 15;
        req.rnr_retry_count = 7;
        req.flow_control = 1;
        req.qp_num = self.get_qp_num();
        req.qp_type = ib_qp_type::IB_QPT_RC;
        req.starting_psn = self.get_qp_num();
        #[cfg(feature = "profile")]
            self.profile.tick_record(0);
        self.cm.send_req(req, conn::Request::new(qd))?;
        self.done.wait(crate::consts::CONNECT_TIME_OUT_MS)?;
        #[cfg(feature = "profile")]
            {
                self.profile.tick_record(5);
                self.profile.increase_op(1);
            }
        return self.cm.send_rtu(0 as u8);
    }

    pub fn handle_reply(&mut self, event: &ib_cm_event) {
        match self.bring_to_rtr(self.cm.get_cm()) {
            Ok(_) => {
                #[cfg(feature = "profile")]
                    self.profile.tick_record(2);

                let _ = self.bring_to_rts(self.cm.get_cm());
                #[cfg(feature = "profile")]
                    self.profile.tick_record(3);
            }
            Err(e) => {
                println!("qp in cm reply bring to RTR err {:?}", e);
            }
        };
        // check the conn reply
        let reply = unsafe { *(event.private_data as *mut reply::Payload) };
        self.remote_mr = Some(reply.mr);
        // finish waiting
        self.done.done();
    }
}

unsafe impl Sync for RC {}

unsafe impl Send for RC {}