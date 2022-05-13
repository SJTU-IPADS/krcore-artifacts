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
use crate::qp::conn::reply::Status;
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
    status: Status,
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
            status: Status::Ok,
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
        Ok(())
    }

    // bring QP from init -> ready to receive
    pub fn bring_to_rtr(&self, cm: *mut ib_cm_id) -> KernelResult<()> {
        // we should first check qp status
        self.bring_to_init(cm)?;

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
        let _qpn = self.get_qp_num();
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
            status: Status::Ok,
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

    /// RC connect to remote side
    /// @param qd: Set as zero if connect for one-sided communication and not-zero if connected as two sided
    /// @param path: Should be generated by ib_explorer
    /// @param service_id: The remote RCtrl listening id. We could connect to one unique RCtrl via
    ///         the combination of `path` and `service_id`
    #[inline]
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
        match self.status {
            Status::Ok => {}
            _ => {
                return Err(linux_kernel_module::Error::from_kernel_errno(self.status as i32));
            }
        }
        #[cfg(feature = "profile")]
            {
                self.profile.tick_record(3);
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
            }
            Err(e) => {
                println!("qp in cm reply bring to RTR err {:?}", e);
            }
        };
        // check the conn reply
        let reply = unsafe { *(event.private_data as *mut reply::Payload) };
        self.remote_mr = Some(reply.mr);
        match reply.status {
            Status::Ok => {
                self.status = Status::Ok;
            }
            _ => {
                println!("Invalid status {:?}", reply.status);
                self.status = reply.status;
            }
        }
        // finish waiting
        self.done.done();
    }
}

unsafe impl Sync for RC {}

unsafe impl Send for RC {}

// Connect path without CM (could be implemented by RPC)
pub mod rc_connector {
    use rust_kernel_rdma_base::linux_kernel_module::println;
    use rust_kernel_rdma_base::*;
    use linux_kernel_module::{Error, KernelResult};
    use crate::to_ptr;

    pub fn bring_to_init(qp: *mut ib_qp) -> KernelResult<()> {
        use linux_kernel_module::c_types::*;

        let mut attr: ib_qp_attr = Default::default();
        let mut mask: c_int = ib_qp_attr_mask::IB_QP_STATE;
        let access_flags = 0
            | ib_access_flags::IB_ACCESS_LOCAL_WRITE
            | ib_access_flags::IB_ACCESS_REMOTE_ATOMIC
            | ib_access_flags::IB_ACCESS_REMOTE_READ
            | ib_access_flags::IB_ACCESS_REMOTE_WRITE;
        attr.pkey_index = 0;
        attr.port_num = 1;
        attr.qp_state = ib_qp_state::IB_QPS_INIT;
        attr.qp_access_flags |= access_flags as i32;
        mask |= ib_qp_attr_mask::IB_QP_ACCESS_FLAGS |
            ib_qp_attr_mask::IB_QP_PKEY_INDEX |
            ib_qp_attr_mask::IB_QP_PORT;

        // modify
        let ret = unsafe { ib_modify_qp(qp, to_ptr!(attr), mask) };
        if ret != 0 {
            println!("bring to init error!");
            return Err(Error::from_kernel_errno(ret));
        }

        Ok(())
    }

    pub fn bring_init_to_rtr(qp: *mut ib_qp, dst_qpn: u32, lid: u16, gid: ib_gid) -> KernelResult<()> {
        use linux_kernel_module::c_types::*;

        let mut attr: ib_qp_attr = Default::default();
        let mut mask: c_int = ib_qp_attr_mask::IB_QP_STATE;
        {
            let mut ah_attr: rdma_ah_attr = Default::default();
            ah_attr.type_ = rdma_ah_attr_type::RDMA_AH_ATTR_TYPE_IB;
            unsafe { bd_rdma_ah_set_dlid(&mut ah_attr, lid as u32) };
            ah_attr.sl = 0;
            ah_attr.port_num = 1;
            // grh
            ah_attr.grh.sgid_index = 0;
            ah_attr.grh.flow_label = 0;
            ah_attr.grh.hop_limit = 255;
            unsafe {
                ah_attr.grh.dgid.global.subnet_prefix = gid.global.subnet_prefix;
                ah_attr.grh.dgid.global.interface_id = gid.global.interface_id;
            }
            // qp_attr
            attr.qp_state = ib_qp_state::IB_QPS_RTR;
            attr.path_mtu = 2; // MTU_512
            attr.dest_qp_num = dst_qpn;
            attr.rq_psn = dst_qpn;
            attr.max_dest_rd_atomic = 16;
            attr.min_rnr_timer = 20;
            attr.ah_attr = ah_attr;
        }
        mask |= ib_qp_attr_mask::IB_QP_STATE |
            ib_qp_attr_mask::IB_QP_AV |
            ib_qp_attr_mask::IB_QP_PATH_MTU |
            ib_qp_attr_mask::IB_QP_DEST_QPN |
            ib_qp_attr_mask::IB_QP_RQ_PSN |
            ib_qp_attr_mask::IB_QP_MAX_DEST_RD_ATOMIC |
            ib_qp_attr_mask::IB_QP_MIN_RNR_TIMER;
        let ret = unsafe { ib_modify_qp(qp, to_ptr!(attr), mask) };
        if ret != 0 {
            println!("bring INIT to RTR error, ret:{}", ret);
            return Err(Error::from_kernel_errno(ret));
        }
        Ok(())
    }

    pub fn bring_rtr_to_rts(qp: *mut ib_qp) -> KernelResult<()> {
        use linux_kernel_module::c_types::*;
        let mut attr: ib_qp_attr = Default::default();
        let mut mask: c_int = ib_qp_attr_mask::IB_QP_STATE;
        attr.qp_state = ib_qp_state::IB_QPS_RTS;
        attr.timeout = 14;
        attr.retry_cnt = 7;
        attr.rnr_retry = 7;
        attr.sq_psn = unsafe { (*qp).qp_num };
        attr.sq_psn = unsafe { (*qp).qp_num };
        attr.max_rd_atomic = 16;
        attr.max_dest_rd_atomic = 16;
        mask |=
            ib_qp_attr_mask::IB_QP_STATE |
                ib_qp_attr_mask::IB_QP_TIMEOUT |
                ib_qp_attr_mask::IB_QP_RETRY_CNT |
                ib_qp_attr_mask::IB_QP_RNR_RETRY |
                ib_qp_attr_mask::IB_QP_MAX_QP_RD_ATOMIC |
                ib_qp_attr_mask::IB_QP_SQ_PSN;
        let ret = unsafe { ib_modify_qp(qp, to_ptr!(attr), mask) };
        if ret != 0 {
            println!("bring to RTS error, ret:{}", ret);
            return Err(Error::from_kernel_errno(ret));
        }

        Ok(())
    }
}