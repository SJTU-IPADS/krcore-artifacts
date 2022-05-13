//! RDMA ctrl for cm messaging

use crate::device::RContext;
use rust_kernel_rdma_base::*;

pub const MOD: &'static str = file!();

use alloc::boxed::Box;
use alloc::sync::Arc;
use core::pin::Pin;
use crate::cm::{ServerCM, ClientCM, EndPoint};
use crate::qp::{RC, UD, DCTServer, DCTargetMeta, DC, RecvHelper, Config};
use linux_kernel_module::{c_types, println, KernelResult};
use linux_kernel_module::mutex::LinuxMutex;
use linux_kernel_module::sync::Mutex;
use linux_kernel_module::bindings::mutex;
use hashbrown::HashMap;
use crate::qp::conn;
use crate::qp::conn::reply;
use crate::qp::conn::reply::SidrPayload;
use crate::Profile;
use crate::mem::{TempMR, RMemPhy, Memory};
use core::ptr::null_mut;
use alloc::vec::Vec;
use crate::consts::DEFAULT_RPC_HINT;

/// The struct RCtrl serves for the both kernel & user thread.
#[warn(dead_code)]
pub struct RCtrl<'a> {
    /// user defined id for this thread ctrl. It could be the RUNNING tid.
    /// Afterwards, the client could assign the **service_id** to reach the cm connection
    id: usize,

    context: &'a RContext<'a>,
    /// In connections (of type RC) created by connecting cm request send by client side.
    /// The hash is one map from `free_in_qp_key` to the RC ref.
    ///
    /// For `registered_qps` key generation
    registered_rcs: HashMap<usize, Arc<RC>>,

    /// UD inside of the RCtrl.
    ///
    /// Note that we can't make this type to be Arc<UD>, since the movement will break down
    /// the CM context and crush the process.
    registered_uds: LinuxMutex<HashMap<usize, Arc<UD>>>,
    /// for global dc server (contains the srq and recv cq for sharing)
    dct_server: Arc<DCTServer>,
    pub dc: Option<Arc<DC>>,
    registered_test_mr: RMemPhy,
    recv_buffer: Option<Arc<RecvHelper<2048>>>,
    /// CM for connections
    cm: ServerCM,
    free_in_qp_key: LinuxMutex<u64>,
    profile: Profile,
    trc: Option<&'a Arc<RC>>,
    mutex: mutex,
}

struct InQPContext {
    thread_ctrl_p: *mut linux_kernel_module::c_types::c_void,
    key: u64,
}

impl InQPContext {
    fn new(addr: u64, key: u64) -> Self {
        Self {
            thread_ctrl_p: addr as *mut linux_kernel_module::c_types::c_void,
            key,
        }
    }
}

impl<'a> RCtrl<'a> {
    /// Create one thread Ctrl.
    /// @param id: The `service_id` of further cm listening.
    /// @param ctx: RDMA context for the cm creation. Since `ib_create_cm_id` needs the ib_device
    /// return: After this, the field `cm` should be initialized. And the cm should listen to the `service_id`
    pub fn create(id: usize, ctx: &'a RContext<'a>) -> Option<Pin<Box<Self>>> {
        let res = Self {
            id,
            context: ctx,
            cm: Default::default(),
            registered_rcs: Default::default(),
            free_in_qp_key: LinuxMutex::new(0),
            registered_uds: LinuxMutex::new(Default::default()),
            profile: Profile::new(),
            dct_server: DCTServer::new(ctx).unwrap(),
            dc: Default::default(),
            registered_test_mr: RMemPhy::new(4 * 1024 * 1024),
            mutex: Default::default(),
            recv_buffer: None,
            trc: None,
        };
        let mut boxed = Box::pin(res);
        let me = unsafe { core::ptr::NonNull::from(boxed.as_mut().get_unchecked_mut()) };
        boxed.mutex.init();
        boxed.registered_uds.init();


        // default ud
        {
            let ud = UD::new_with_srq(ctx,
                                      boxed.get_recv_cq(),
                                      null_mut()).unwrap();
            boxed.reg_ud(crate::DEFAULT_RPC_HINT, ud);
        }
        // dc
        {
            boxed.dc = DC::new_with_srq(ctx,
                                        null_mut(),
                                        boxed.get_recv_cq());
        }

        {
            let pa = boxed.registered_test_mr.get_dma_buf();
            // let srq = boxed.get_srq();
            let mut recv_helper = RecvHelper::<2048>::create(2048,
                                                             unsafe { ctx.get_lkey() },
                                                             pa,
            );
            let recv = unsafe { Arc::get_mut_unchecked(&mut recv_helper) };
            let ud = boxed.get_mut_ud(crate::DEFAULT_RPC_HINT).unwrap();
            recv.post_recvs(ud.get_qp(), null_mut(), 1024);
            boxed.recv_buffer = Some(recv_helper);
        }

        ServerCM::new(
            boxed.context.get_raw_dev(),
            Some(RCtrl::cm_handler),
            me.as_ptr() as *mut linux_kernel_module::c_types::c_void,
        ).map(|cm| {
            boxed.cm = cm;
            boxed.cm.listen(id as u64).unwrap();    // listen to service_id = id
            boxed
        })
    }
}


// CM related
impl<'a> RCtrl<'a> {
    pub unsafe extern "C" fn cm_handler(cm_id: *mut ib_cm_id, env: *const ib_cm_event) -> c_types::c_int {
        let event = *env;
        let cm = &mut { *cm_id };

        // XD: this is not safe if the cm_id already existed

        let ctrl = &mut (*(cm.context as *mut RCtrl));
        match event.event {
            ib_cm_event_type::IB_CM_REQ_RECEIVED => {
                #[cfg(feature = "profile")]
                    ctrl.profile.reset_timer();
                ctrl.rc_conn_callback(cm_id, event);
                #[cfg(feature = "profile")]
                    {
                        ctrl.profile.tick_record(2);
                        ctrl.profile.increase_op(1);
                        println!("[RCtrl report]");
                        ctrl.profile.report(3);
                    }
                // println!("handle req res: {:?}", res);
            }
            ib_cm_event_type::IB_CM_REJ_RECEIVED => {
                println!("<<ctrl>> ib cm rej received {}", cm.service_id);
            }
            ib_cm_event_type::IB_CM_TIMEWAIT_EXIT => {
                println!("<<ctrl>> ib cm timewait received {}", cm.service_id);
            }
            ib_cm_event_type::IB_CM_RTU_RECEIVED => {}
            ib_cm_event_type::IB_CM_DREQ_RECEIVED => {
                println!("<<id:{}>> ib cm dreq received: {}", ctrl.id, event.event);
                let ctx = Box::from_raw(cm.context as *mut InQPContext);
                // Reset
                (*(ctx.thread_ctrl_p as *mut RCtrl)).dereg_rc(ctx.key as usize);
            }
            ib_cm_event_type::IB_CM_SIDR_REQ_ERROR => {
                println!("<<ctrl>> ib cm sidr req error!")
            }
            ib_cm_event_type::IB_CM_SIDR_REQ_RECEIVED => {
                println!("<<ctrl>> ib cm sidr req received");
                let _ = ctrl.sidr_callback(cm_id, event);
            }
            _ => println!("unknown ib cm event {}", event.event),
        }
        0
    }

    #[inline]
    pub fn rc_conn_callback(&'a mut self, cm: *mut ib_cm_id, event: ib_cm_event) -> KernelResult<()> {
        #[cfg(feature = "profile")]
            self.profile.reset_timer();
        // 1. Fetch the connection arguments.
        let conn_arg = &unsafe { *(event.private_data as *mut conn::Request) };
        // 2. Create one RC at server side.
        let mut in_qp = RC::new_with_srq(
            Config::new().set_max_send_wr_sz(256),
            self.context,
            cm,
            null_mut(),
            self.get_recv_cq(),
        ).unwrap();
        let qp: &mut RC = unsafe { Arc::get_mut_unchecked(&mut in_qp) };
        let ib_qp = qp.get_qp();
        // 3. Init the `reply` entity, assemble the newly generated qp_key to inform the client side.
        let new_key = self.get_next_in_qp_key(conn_arg.qd as u64);
        let mut reply = reply::Payload::new_from_null(conn_arg.qd as u64);

        reply.mr = self.get_self_test_mr();
        #[cfg(feature = "profile")]
            self.profile.tick_record(0);
        // 4. Bring RC state into RTR. Register the qp into `registered_qps` (key: new_qp_key, value: qp)
        match qp.bring_to_rtr(cm) {
            Ok(_) => {
                #[cfg(feature = "profile")]
                    self.profile.tick_record(1);
                let _ = qp.bring_to_rts(cm);
                // 5. Connect Reply to client
                let ret = qp.connect_reply(&event, &qp.cm, reply);
                // store the key inside of the inner context, for further deregister
                let new_ctx = Box::new(InQPContext::new((self as *mut _) as u64, new_key));
                let new_ctx_ptr = Box::<InQPContext>::into_raw(new_ctx);
                qp.cm.set_context(new_ctx_ptr as u64);
                self.mutex.lock();
                {
                    self.registered_rcs.insert(new_key as usize, in_qp);
                    // self.trc = self.registered_rcs.get(&(new_key as usize));
                }
                self.mutex.unlock();
                // push recv
                {
                    let recv_buffer = self.get_recv_buffer();
                    let recv = unsafe { Arc::get_mut_unchecked(recv_buffer) };
                    recv.post_recvs(ib_qp, null_mut(), 512);
                }

                ret
            }
            Err(e) => {
                println!("Bring QP to RTR err {:?}", e);
                qp.connect_reply(&event, &qp.cm, reply)
            }
        }
    }

    #[inline]
    fn get_next_in_qp_key(&self, vid: u64) -> u64 {
        if vid == 0 {
            return self.free_in_qp_key.lock_f(|qp_key| {
                let ret = *qp_key;
                *qp_key += 1;
                ret
            });
        } else {
            vid
        }
    }

    #[inline]
    pub fn trc_post_recv(&mut self, vid: usize, push_cnt: usize) -> KernelResult<()> {
        let ib_qp = self.get_trc(vid).unwrap().get_qp();
        let mut recv_buffer = self.recv_buffer.as_mut().unwrap();
        let recv = unsafe { Arc::get_mut_unchecked(recv_buffer) };
        recv.post_recvs(ib_qp, null_mut(), push_cnt)
    }

    #[inline]
    pub fn ud_post_recv(&mut self, push_cnt: usize) -> KernelResult<()> {
        let ib_qp = self.get_ud(DEFAULT_RPC_HINT).unwrap().get_qp();
        let mut recv_buffer = self.recv_buffer.as_mut().unwrap();
        let recv = unsafe { Arc::get_mut_unchecked(recv_buffer) };
        recv.post_recvs(ib_qp, null_mut(), push_cnt)
    }
}

impl RCtrl<'_> {
    #[inline]
    pub fn sidr_callback(&mut self, cm: *mut ib_cm_id, event: ib_cm_event) -> KernelResult<()> {
        let conn_arg = &unsafe { *(event.private_data as *mut conn::Request) };
        let qd_hint = conn_arg.qd as u64;
        let status: reply::Status;
        let qpn: u32;
        let qkey: u32;
        let in_qp = self.get_mut_ud(qd_hint as usize);

        // See if the UD exists
        match in_qp {
            Some(..) => {
                let mut in_qp = in_qp.unwrap();
                let ud: &mut UD = unsafe { Arc::get_mut_unchecked(in_qp) };
                qpn = ud.get_qp_num();
                qkey = ud.get_qkey();
                status = reply::Status::Ok;
            }
            None => {
                qpn = 0;
                qkey = 0;
                status = reply::Status::NotExist;
            }
        }

        let reply = SidrPayload::new(qd_hint, self.context,
                                     &mut self.get_self_dct_meta(), status).
            unwrap_or(SidrPayload::new_from_null(qd_hint as u64));
        // send sidr reply
        ServerCM::ud_conn_reply(cm, qpn, qkey, reply)
    }
}

impl<'a> RCtrl<'a> {
    #[inline]
    pub fn get_context(&self) -> &'a RContext<'a> {
        self.context
    }

    #[inline]
    pub fn get_ud(&'a self, qd: usize) -> Option<&'a Arc<UD>> {
        self.registered_uds.lock_f(|uds|
            {
                uds.get(&(qd as usize))
            }
        )
    }

    #[inline]
    pub fn get_mut_ud(&'a self, qd: usize) -> Option<&'a mut Arc<UD>> {
        self.registered_uds.lock_f(|uds|
            {
                uds.get_mut(&(qd as usize))
            }
        )
    }

    #[inline]
    pub fn reg_ud(&self, qd: usize, ud: Arc<UD>) {
        self.registered_uds.lock_f(|uds| uds.insert(qd, ud));
    }

    #[inline]
    pub fn dereg_ud(&self, qd: usize) {
        self.registered_uds.lock_f(|uds| uds.remove(&qd));
    }


    #[inline]
    pub fn dereg_rc(&mut self, qd: usize) {
        self.mutex.lock();
        {
            if self.registered_rcs.contains_key(&qd) {
                self.registered_rcs.remove(&qd).map(|mut qp| {
                    let qp = unsafe { Arc::get_mut_unchecked(&mut qp) };
                    // Just put inner cm to be null ptr rather than destroy it.
                    // since the cm inside of the RCtrl is related to this cm in RC
                    qp.cm.handoff_raw_cm();
                });
            }
            self.trc = None;
        }
        self.mutex.unlock();
    }


    #[inline]
    pub fn get_self_dct_meta(&mut self) -> DCTargetMeta {
        DCTargetMeta {
            dct_num: self.dct_server.get_dct_num(),
            lid: self.context.get_lid(),
            gid: self.context.get_gid(),
            mr: self.get_self_test_mr(),
        }
    }

    #[inline]
    pub fn get_self_endpoint(&mut self, ud_id: usize) -> Option<EndPoint> {
        match self.get_ud(ud_id) {
            Some(ud) => {
                Some(
                    EndPoint {
                        qpn: ud.get_qp_num(),
                        qkey: ud.get_qkey(),
                        lid: self.get_context().get_lid(),
                        gid: self.get_context().get_gid(),
                        dct_num: self.get_dc_num(),
                        mr: self.get_self_test_mr(),
                        ah: null_mut(),
                    }
                )
            }
            None => None
        }
    }

    #[inline]
    pub fn get_id(&self) -> usize {
        self.id
    }

    #[inline]
    pub fn get_dc_num(&mut self) -> u32 {
        self.dct_server.get_dct_num()
    }

    #[inline]
    pub fn get_self_test_mr(&mut self) -> TempMR {
        TempMR::new(
            self.registered_test_mr.get_dma_buf(),
            self.registered_test_mr.get_sz() as u32,
            unsafe { self.get_context().get_rkey() },
        )
    }

    #[inline]
    pub fn get_trc(&'a self, vid: usize) -> Option<&'a Arc<RC>> {
        return self.registered_rcs.get(&vid);
    }

    #[inline]
    pub fn get_dc(&self) -> Option<&Arc<DC>> {
        self.dc.as_ref()
    }

    #[inline]
    pub fn get_dc_mut(&mut self) -> Option<&mut Arc<DC>> {
        self.dc.as_mut()
    }

    #[inline]
    pub fn get_srq(&self) -> *mut ib_srq { self.dct_server.get_srq() }

    #[inline]
    pub fn get_recv_cq(&self) -> *mut ib_cq { self.dct_server.get_recv_cq() }

    #[inline]
    pub fn get_dct_cq(&self) -> *mut ib_cq { self.dct_server.get_cq() }

    #[inline]
    pub fn get_recv_buffer(&'a mut self) -> &'a mut Arc<RecvHelper<2048>> {
        self.recv_buffer.as_mut().unwrap()
    }

    #[inline]
    pub fn reset(&mut self) {
        // cm destroy (conduct manually)
        self.cm.reset();
        // connections clear
        self.registered_uds.lock_f(|uds| uds.clear());
    }
}

impl Drop for RCtrl<'_> {
    fn drop(&mut self) {
        self.reset();
    }
}

unsafe impl Sync for RCtrl<'_> {}

unsafe impl Send for RCtrl<'_> {}