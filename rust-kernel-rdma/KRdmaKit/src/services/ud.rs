use crate::linux_kernel_module::mutex::LinuxMutex;
use crate::linux_kernel_module::sync::Mutex;

use alloc::sync::Arc;

use hashbrown::HashMap;
use rust_kernel_rdma_base::*;

use crate::comm_manager::{CMCallbacker, CMError, CMReplyer};
use crate::queue_pairs::QueuePair;

#[allow(dead_code)]
#[repr(C, align(8))]
#[derive(Copy, Clone, Debug)]
pub struct DatagramMeta {
    // The payload's related data
    pub lid: u16,
    pub gid: ib_gid,
}

impl DatagramMeta {
    pub fn new(qp : &QueuePair) -> Result<Self, CMError> {
        let port_num = qp.port_num();
        let port_attr = qp.ctx().get_dev_ref().get_port_attr(port_num).map_err(|err| {
            CMError::Creation(err.to_kernel_errno())
        })?;
        let gid = qp.ctx().get_dev_ref().query_gid(port_num).map_err(|err| {
            CMError::Creation(err.to_kernel_errno())
        })?;

        let lid = port_attr.lid as u16;
        Ok(Self { lid, gid })
    }
}

impl Default for DatagramMeta {
    fn default() -> Self {
        Self {
            lid: 0,
            gid: Default::default(),
        }
    }
}


/// UnreliableDatagramServer provides a quick path for user to establish UD sidr connection.
/// You should wrap it with a CMServer and register UD queue pair to it to make it work;
///
/// Usage:
/// ```
/// use std::sync::Arc;
/// use KRdmaKit::KDriver;
/// use KRdmaKit::comm_manager::CMServer;
/// use KRdmaKit::queue_pairs::builder::QueuePairBuilder;
/// use KRdmaKit::services::UnreliableDatagramServer;
///
/// let driver = unsafe { KDriver::create().unwrap() };
/// let server_ctx = driver
///     .devices()
///     .into_iter()
///     .next()
///     .expect("No device available")
///     .open_context().unwrap();
/// 
/// let server_service_id = 73;
/// let ud_server = UnreliableDatagramServer::create();
/// 
/// let server_cm = CMServer::new(server_service_id, &ud_server, server_ctx.get_dev_ref()).unwrap();
/// let builder = QueuePairBuilder::new(&server_ctx);
/// let qp_res = builder.build_ud().unwrap();
/// let ud = qp_res.bring_up_ud().unwrap();
/// ud_server.reg_ud(73, &ud);
/// ```
pub struct UnreliableDatagramServer {
    registered_ud: LinuxMutex<HashMap<usize, Arc<QueuePair>>>,
}

impl UnreliableDatagramServer {
    pub fn create() -> Arc<Self> {
        let handler = Arc::new(Self {
            registered_ud: LinuxMutex::new(Default::default()),
        });
        handler.registered_ud.init();
        handler
    }

    pub fn reg_ud(&self, qd_hint: usize, ud: &Arc<QueuePair>) {
        self.registered_ud
            .lock_f(|uds| uds.insert(qd_hint, ud.clone()));
    }

    pub fn dereg_ud(&self, qd_hint: usize) {
        self.registered_ud.lock_f(|uds| uds.remove(&qd_hint));
    }

    pub fn get_ud(&self, qd_hint: usize) -> Option<&Arc<QueuePair>> {
        self.registered_ud.lock_f(|uds| uds.get(&qd_hint))
    }
}

impl CMCallbacker for UnreliableDatagramServer {
    fn handle_req(&mut self, _reply_cm: CMReplyer, _event: &ib_cm_event) -> Result<(), CMError> {
        Ok(())
    }

    /// Called on receiving SIDR_REQ from client side
    fn handle_sidr_req(
        &mut self,
        mut reply_cm: CMReplyer,
        event: &ib_cm_event,
    ) -> Result<(), CMError> {
        let qd_hint = unsafe { *(event.private_data as *mut usize) };
        let mut rep: ib_cm_sidr_rep_param = Default::default();
        let ud = self.get_ud(qd_hint);
        let mut payload = DatagramMeta::default();
        if ud.is_none() {
            rep.status = ib_cm_sidr_status::IB_SIDR_NO_QP;
        } else {
            let ud = ud.unwrap();
            let payload_opt = DatagramMeta::new(ud);
            match payload_opt {
                Ok(meta) => {
                    rep.qp_num = ud.qp_num();
                    rep.qkey = ud.qkey();
                    payload = meta;
                    rep.status = ib_cm_sidr_status::IB_SIDR_SUCCESS;
                }
                Err(_) => {
                    rep.status = ib_cm_sidr_status::IB_SIDR_NO_QP;
                }
            }
        }
        reply_cm.send_sidr_reply(rep, payload)
    }
}

