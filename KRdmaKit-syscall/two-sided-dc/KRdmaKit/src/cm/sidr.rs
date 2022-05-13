//! Besides the UD communication handshake, we wanna to fetch the remote
//! `qpn` and `qkey`
use crate::cm::ClientCM;
use crate::device::RContext;
use crate::qp::conn::reply;
use crate::qp::{conn, create_ib_ah};
use crate::rust_kernel_rdma_base::*;
use linux_kernel_module::{println, KernelResult};
use rust_kernel_linux_util::bindings::{bd__builtin_bswap64, completion};
use rust_kernel_rdma_base::linux_kernel_module::Error;

pub struct EndPoint {
    pub qpn: u32,
    pub qkey: u32,
    pub lid: u16,
    pub gid: ib_gid,
    pub dct_num: u32,
    pub mr: TempMR,
    pub ah: *mut ib_ah,
}
unsafe impl Sync for EndPoint {}

unsafe impl Send for EndPoint {}

impl Default for EndPoint {
    fn default() -> Self {
        Self {
            qpn: 0,
            qkey: 0,
            lid: 0,
            gid: Default::default(),
            dct_num: 0,
            mr: Default::default(),
            ah: null_mut(),
        }
    }
}

impl EndPoint {
    pub fn new(pd: *mut ib_pd,
               qpn: u32,
               qkey: u32,
               lid: u16,
               gid: ib_gid,
               dct_num: u32,
               mr: &TempMR,
    ) -> Self {
        Self {
            qpn,
            qkey,
            lid,
            gid,
            dct_num,
            mr: *mr,
            ah: create_ib_ah(pd, lid, gid),
        }
    }

    #[inline]
    pub fn valid(&self) -> bool {
        !self.ah.is_null()
    }

    #[inline]
    fn transfer(&mut self) -> Self {
        let temp = self.ah;
        self.ah = null_mut();
        Self {
            qpn: self.qpn,
            qkey: self.qkey,
            lid: self.lid,
            gid: self.gid,
            dct_num: self.dct_num,
            mr: self.mr.clone(),
            ah: temp,
        }
    }

    #[inline]
    pub fn bind_pd(&mut self, pd: *mut ib_pd) {
        if self.valid() {
            unsafe { rdma_destroy_ah(self.ah) };
        }
        self.ah = create_ib_ah(pd, self.lid, self.gid);
    }
}

impl Drop for EndPoint {
    fn drop(&mut self) {
        // avoid double free
        if self.valid() {
            unsafe { rdma_destroy_ah(self.ah) };
        }
    }
}

use alloc::boxed::Box;
use core::ptr::null_mut;
use crate::mem::TempMR;

/// SidrCM serves for a wrapper of ClientCM.
/// It could send / reply the sidr requests to the remote side.
pub struct SidrCM<'a> {
    context: &'a RContext<'a>,
    done: completion,
    cm: ClientCM,
    end_point: EndPoint,
}

impl<'a> SidrCM<'a> {
    pub fn new(ctx: &'a RContext<'a>, cm: *mut ib_cm_id) -> Option<Box<SidrCM>> {
        let res = Self {
            context: ctx,
            done: Default::default(),
            cm: Default::default(),
            end_point: Default::default(),
        };
        let mut boxed = Box::new(res);
        let sidr_cm = boxed.as_mut();

        if cm == core::ptr::null_mut() {
            let cm = ClientCM::new(
                ctx.get_raw_dev(),
                Some(SidrCM::cm_handler),
                (sidr_cm as *mut SidrCM).cast::<linux_kernel_module::c_types::c_void>(),
            );
            if cm.is_none() {
                return None;
            }
            sidr_cm.cm = cm.unwrap();
        } else {
            sidr_cm.cm = ClientCM::new_from_raw(cm);
        }

        Some(boxed)
    }
}

// CM related
impl SidrCM<'_> {
    pub unsafe extern "C" fn cm_handler(
        cm_id: *mut ib_cm_id,
        env: *const ib_cm_event,
    ) -> linux_kernel_module::c_types::c_int {
        let event = *env;
        let cm: &mut ib_cm_id = &mut *cm_id;
        let sidr_cm: &mut SidrCM = &mut *(cm.context as *mut SidrCM);

        match event.event {
            ib_cm_event_type::IB_CM_SIDR_REP_RECEIVED => {
                // println!("QP sidr reply received!");
                sidr_cm.handle_sidr_reply(&event);
            }
            ib_cm_event_type::IB_CM_SIDR_REQ_ERROR => {
                println!("ib cm sidr req error!");
                // end to wait
                sidr_cm.done.done();
            }
            // others should be handled later
            _ => {
                println!("unknown QP cm event: {}", event.event);
            }
        };
        0
    }

    /// Sidr connect to get the remote message
    pub fn sidr_connect<'a>(
        &mut self,
        path: sa_path_rec,
        service_id: u64,
        qd_hint: u64,
    ) -> KernelResult<EndPoint> {
        self.done.init();
        let mut req: ib_cm_sidr_req_param = Default::default();

        let mut s_path = path.clone();
        req.path = &mut s_path as *mut sa_path_rec;
        req.service_id = unsafe { bd__builtin_bswap64(service_id as u64) };
        req.timeout_ms = 20;
        req.max_cm_retries = 3;

        self.cm.send_sidr(req, conn::Request::new(qd_hint))?;
        self.done.wait(crate::consts::CONNECT_TIME_OUT_MS as _)?;
        // extract out the result
        // not exist
        if !self.end_point.valid() {
            println!("remote side not exist!");
            return Err(Error::EINVAL);
        }
        Ok(self.end_point.transfer())
    }

    #[inline]
    pub fn handle_sidr_reply(&mut self, event: &ib_cm_event) {
        let rep_param = unsafe { event.param.sidr_rep_rcvd }; // sidr_rep_rcvd

        let reply = unsafe { *(rep_param.info as *mut reply::SidrPayload) };
        match reply.status {
            reply::Status::Ok => {
                let qkey: u32 = rep_param.qkey;
                let qpn: u32 = rep_param.qpn;
                let _qd_hint = reply.qd;
                self.end_point = EndPoint::new(
                    self.context.get_pd(),
                    qpn as u32,
                    qkey as u32,
                    reply.lid,
                    reply.gid,
                    reply.dct_num,
                    &reply.mr
                );
            }
            reply::Status::NotExist => {
                // no such UD remote
                self.end_point = Default::default();
            }
            _ => {}
        }

        self.done.done();
    }
}

impl Drop for SidrCM<'_> {
    fn drop(&mut self) {
        self.cm.reset();
    }
}