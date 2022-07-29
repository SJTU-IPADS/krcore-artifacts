use rust_kernel_rdma_base::bindings::*;

use rdma_shim::utils::{LinuxMutex, Mutex};

use alloc::sync::Arc;
use core::ptr::NonNull;
use hashbrown::HashMap;

use crate::comm_manager::{CMCallbacker, CMError, CMReplyer};
use crate::context::Context;
use crate::log::*;
use crate::queue_pairs::{QueuePair, QueuePairBuilder};

/// RCConnectionData is used for remote QP to connect with it
#[repr(C, align(8))]
#[derive(Copy, Clone, Debug)]
pub struct RCConnectionData {
    pub lid: u16,
    pub gid: ib_gid,
}

impl RCConnectionData {
    /// Create the connection data of the client
    pub fn new(qp: &QueuePair) -> Result<Self, CMError> {
        let port_num = qp.port_num();

        let port_attr = qp
            .ctx()
            .get_dev_ref()
            .get_port_attr(port_num)
            .map_err(|err| CMError::Creation(err.to_kernel_errno()))?;

        let gid = qp
            .ctx()
            .get_dev_ref()
            .query_gid(port_num)
            .map_err(|err| CMError::Creation(err.to_kernel_errno()))?;


        Ok(Self { lid : port_attr.lid as _, gid })
    }
}

/// The connection server basically stores the in-coming QPs 
/// After receiving an RCQP connection request, 
/// it will automatically create a new one and store it in its registered_c. 
pub struct ReliableConnectionServer {
    ctx: Arc<Context>,
    port_num: u8,
    registered_rc: LinuxMutex<HashMap<u64, Arc<QueuePair>>>,
}

impl ReliableConnectionServer {
    /// Create an ReliableConnectionServer that listens to connection request and handles registration
    /// requests and de-registration request.
    ///
    /// Param `port_num` is the  RC qp's port number to be created.
    ///
    /// Return an `Arc<ReliableConnectionServer>` for communicating usage. It will be used in the CMServer.
    /// See `reliable_connection` unit test for detailed usage.
    pub fn create(ctx: &Arc<Context>, port_num: u8) -> Arc<Self> {
        let handler = Arc::new(Self {
            ctx: ctx.clone(),
            port_num,
            registered_rc: LinuxMutex::new(HashMap::<u64, Arc<QueuePair>>::default()),
        });
        handler.registered_rc.init();
        handler
    }

    /// Use the pointer of the `ib_cm_id` as the key 
    /// Since the `ib_cm` will create a unique ib_cm_id for each coming requests,
    /// it is safe to use the pointer as the key 
    /// 
    /// Param:
    /// - `cm_id`: the ib_cm_id created by the ib_cm
    #[inline]
    fn encode_rc_key_by_cm(cm_id : &NonNull<ib_cm_id>) -> u64 { 
        cm_id.as_ptr() as _
    }
}

impl CMCallbacker for ReliableConnectionServer {
    /// The `handle_req` will create a QP after receiving a connection request.
    ///
    /// Responsible for creating and registering server side rc-qp on receiving req from the client.
    fn handle_req(&mut self, reply_cm: CMReplyer, event: &ib_cm_event) -> Result<(), CMError> {
        // 1. decode the request
        let data = unsafe { *(event.private_data as *mut RCConnectionData) };
        let req: &ib_cm_req_event_param = &unsafe { event.param.req_rcvd };

        // 2. create the queue_pair
        let mut builder = QueuePairBuilder::new(&self.ctx);
        builder
            .set_rnr_retry(req.rnr_retry_count() as u8)
            .set_port_num(self.port_num)
            .set_max_send_wr(64) // not necessary to use a large WR: server QP is passive
            .allow_remote_rw()
            .allow_remote_atomic();
        let rc_qp = builder.build_rc().map_err(|_| {
            let _ = reply_cm.send_reject(2 as u64);
            CMError::Creation(0)
        })?;

        // directly bring this QP up
        let rc_qp = rc_qp
            .bring_up_rc(data.lid as u32, data.gid, req.remote_qpn, req.starting_psn)
            .map_err(|_| {
                let _ = reply_cm.send_reject(3 as u64);
                CMError::Creation(0)
            })?;         
        
        // 3. prepare the reply data
        let rc_reply_data = RCConnectionData::new(rc_qp.as_ref())?;
        let rc_qpn = rc_qp.qp_num();

        let rep = ib_cm_rep_param {
            qp_num: rc_qpn,
            starting_psn: rc_qpn,
            responder_resources: req.responder_resources,
            initiator_depth: req.initiator_depth,
            flow_control: req.flow_control() as u8,
            rnr_retry_count: req.rnr_retry_count() as u8,
            ..Default::default()
        };

        // 4. store the QP locally
        let rc_key = Self::encode_rc_key_by_cm(reply_cm.raw_cm_id());
        self.registered_rc.lock_f(|rcs| rcs.insert(rc_key, rc_qp));           
        
        // 5. reply to the client
        let _ = reply_cm.send_reply(rep, rc_reply_data)?;
        Ok(())
    }

    /// `handle_rtu` is the ReadyToUse handler responsible for handle rtu from the client side
    fn handle_rtu(&mut self, _reply_cm: CMReplyer, _event: &ib_cm_event) -> Result<(), CMError> {
        Ok(())
    }

    /// `handle_dreq` handle the de-register request from the client, it will de-register the
    /// corresponding earlier created rc-qp in server side and release related resources.
    fn handle_dreq(
        self: &mut Self,
        reply_cm: CMReplyer,
        _event: &ib_cm_event,
    ) -> Result<(), CMError> {
        debug!("RC server handle dreq");
        let rc_key = Self::encode_rc_key_by_cm(reply_cm.raw_cm_id());
        self.registered_rc.lock_f(|rcs| rcs.remove(&rc_key));
        Ok(())
    }
}
