use core::marker::PhantomData;

use alloc::string::ToString;
use alloc::sync::Arc;

use hashbrown::HashMap;
use rdma_shim::bindings::*;
use rdma_shim::utils::{LinuxMutex, Mutex}; 

use crate::comm_manager::{CMCallbacker, CMError, CMReplyer};

pub mod ud;

#[cfg(feature = "dct")]
pub mod dc;

/// Reliable connection requires a complex handler than UD & DC
/// Consequently, we separately implement in a individual modeule.
pub mod rc;

pub use rc::{RCConnectionData, ReliableConnectionServer};
pub use ud::{DatagramMeta, UnreliableDatagramAddressService};

pub trait GenerateConnectionReply<ReplyData> {
    fn generate_connection_reply(&self) -> Result<ReplyData, CMError>;
}

pub trait GetQueuePairInfo {
    fn get_qkey(&self) -> u32;
    fn get_qp_num(&self) -> u32;
}

impl GetQueuePairInfo for crate::queue_pairs::QueuePair {
    fn get_qkey(&self) -> u32 {
        self.qkey()
    }

    fn get_qp_num(&self) -> u32 {
        self.qp_num()
    }
}

/// QP: only supports DCQP and UDQP
///
/// ConnectionService provides a quick path for user to establish sidr-based RDMA connection.
/// You should wrap it with a CMServer and register UD queue pair to it to make it work;
///
/// Usage:
///
/// ```
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
/// let server = ConnectionService::create();
/// let server_cm = CMServer::new(server_service_id, &server, server_ctx.get_dev_ref()).unwrap();
///
/// let builder = QueuePairBuilder::new(&server_ctx);
/// let qp = builder.build_ud().unwrap().bring_up_...().unwrap();
/// server.reg_qp(73, &qp);
/// ```
pub struct ConnectionService<R, QP: GenerateConnectionReply<R>> {
    registered_qps: LinuxMutex<HashMap<usize, Arc<QP>>>,
    _phantom: PhantomData<R>,
}

impl<R, QP> ConnectionService<R, QP>
where
    QP: GenerateConnectionReply<R> + GetQueuePairInfo,
{
    pub fn create() -> Arc<Self> {
        let handler = Arc::new(Self {
            registered_qps: LinuxMutex::new(Default::default()),
            _phantom: PhantomData,
        });
        handler.registered_qps.init();
        handler
    }

    pub fn reg_qp(&self, qd_hint: usize, qp: &Arc<QP>) {
        self.registered_qps
            .lock_f(|qps| qps.insert(qd_hint, qp.clone()));
    }

    pub fn dereg_qp(&self, qd_hint: usize) {
        self.registered_qps.lock_f(|qps| qps.remove(&qd_hint));
    }

    pub fn get_qp(&self, qd_hint: usize) -> Option<&Arc<QP>> {
        self.registered_qps.lock_f(|qps| qps.get(&qd_hint))
    }

    fn generate_all_reply_data(
        &self,
        qd_hint: usize,
    ) -> Result<(R, u32, u32), CMError> {
        let qp = self.get_qp(qd_hint).ok_or(CMError::InvalidArg(
            "QP hint unfound: ",
            qd_hint.to_string(),
        ))?;

        Ok((
            qp.generate_connection_reply()?,
            qp.get_qkey(),
            qp.get_qp_num(),
        ))
    }
}

impl<R, QP> CMCallbacker for ConnectionService<R, QP>
where
    QP: GenerateConnectionReply<R> + GetQueuePairInfo,
{
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
        match self.generate_all_reply_data(qd_hint) {
            Ok((reply, qkey, num)) => {
                rep.qp_num = num;
                rep.qkey = qkey;
                rep.status = ib_cm_sidr_status::IB_SIDR_SUCCESS;
                reply_cm.send_sidr_reply(rep, reply)
            }
            Err(_) => {
                // FIXME: what if a user using DDOS?
                // log::error!("CM handler error {:?}", e);
                rep.status = ib_cm_sidr_status::IB_SIDR_NO_QP;
                reply_cm.send_sidr_reply(rep, 0 as u64)
            }
        }
    }
}
