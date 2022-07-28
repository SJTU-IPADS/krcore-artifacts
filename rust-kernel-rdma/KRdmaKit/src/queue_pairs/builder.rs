use rust_kernel_linux_util::bindings::*;
use rust_kernel_rdma_base::*;

use alloc::{boxed::Box, sync::Arc};
use core::ptr::NonNull;

use crate::bindings::ib_qp_cap;
use crate::comm_manager::CMError;
use crate::context::Context;
use crate::linux_kernel_module::*;
use crate::queue_pairs::rc_comm::RCCommStruct;
use crate::queue_pairs::{QPType, QueuePair, QueuePairStatus};
use crate::services::rc::RCConnectionData;
use crate::{rust_kernel_linux_util as log, MAX_RD_ATOMIC};
use crate::{CompletionQueue, ControlpathError};

/// Builder for different kind of queue pairs (RCQP, UDQP ,etc.).
/// Store the necessary configuration parameters required
///
/// Set necessary fields and build the corresponding queue pairs.
pub struct QueuePairBuilder {
    pub(super) ctx: Arc<Context>,
    pub(super) max_send_wr: u32,
    pub(super) max_recv_wr: u32,
    pub(super) max_cq_entries: u32,
    pub(super) max_send_sge: u32,
    pub(super) max_recv_sge: u32,
    pub(super) max_inline_data: u32,

    // carried along to handshake phase
    pub(super) access: ib_access_flags::Type,
    pub(super) path_mtu: ib_mtu::Type,
    pub(super) timeout: u8,
    pub(super) retry_count: u8,
    pub(super) rnr_retry: u8,
    pub(super) min_rnr_timer: u8,
    pub(super) max_rd_atomic: u8,
    pub(super) pkey_index: u16,
    pub(super) port_num: u8,
    pub(super) qkey: u32,
}

impl QueuePairBuilder {
    /// Create a QueuePairBuilder with the given `Context` which stores the device,
    /// memory region and protection domain information.
    ///
    /// Set the builder's necessary fields and build the queue pair that's needed.
    pub fn new(ctx: &Arc<Context>) -> Self {
        let mut qkey: [u8; 1] = [0x0; 1];
        let _ = random::getrandom(&mut qkey);
        Self {
            ctx: ctx.clone(),
            max_send_wr: 128,
            max_recv_wr: 2048,
            max_cq_entries: 2048,
            max_send_sge: 16,
            max_recv_sge: 1,
            max_inline_data: 64,
            access: ib_access_flags::IB_ACCESS_LOCAL_WRITE,
            path_mtu: ib_mtu::IB_MTU_512,
            timeout: 4,
            retry_count: 5,
            rnr_retry: 5,
            min_rnr_timer: 16,
            max_rd_atomic: MAX_RD_ATOMIC as u8,
            pkey_index: 0,
            port_num: 1,
            qkey: qkey[0] as u32,
        }
    }

    /// Set both `max_send_wr` for the new `QueuePair`
    ///
    /// Default value is 2048
    pub fn set_max_send_wr(&mut self, max_wr: u32) -> &mut Self {
        self.max_send_wr = max_wr;
        self
    }

    /// Set both and `max_recv_wr` for the new `QueuePair`
    ///
    /// Default value is 4096
    pub fn set_max_recv_wr(&mut self, max_wr: u32) -> &mut Self {
        self.max_recv_wr = max_wr;
        self
    }

    /// Set the maximum number of completion queue entries for
    /// the new `QueuePair`'s completion queue
    ///
    /// Default value is 2048
    pub fn set_max_cq_entries(&mut self, max_cq_entries: u32) -> &mut Self {
        self.max_cq_entries = max_cq_entries;
        self
    }

    /// Set the maximum number of scatter/gather elements in any Work Request
    /// that can be posted to the Send Queue in that Queue Pair.
    ///
    /// Default value is 16
    pub fn set_max_send_sge(&mut self, max_send_sge: u32) -> &mut Self {
        self.max_send_sge = max_send_sge;
        self
    }

    /// Set the maximum number of scatter/gather elements in any Work Request
    /// that can be posted to the Receive Queue in that Queue Pair.
    ///
    /// Default value is 1
    pub fn set_max_recv_sge(&mut self, max_recv_sge: u32) -> &mut Self {
        self.max_recv_sge = max_recv_sge;
        self
    }

    /// Set the maximum message size (in bytes) that can be posted inline to the Send Queue.
    /// Zero if no inline message is requested
    ///
    /// Default value is 64
    pub fn set_max_inline_data(&mut self, max_inline_data: u32) -> &mut Self {
        self.max_inline_data = max_inline_data;
        self
    }

    /// Set the access flags for the new `QueuePair`.
    ///
    /// Default value is `ib_access_flags::IB_ACCESS_LOCAL_WRITE`.
    pub fn set_access(&mut self, access: ib_access_flags::Type) -> &mut Self {
        self.access = access;
        self
    }

    /// Set the access flags of the new `QueuePair` such that it allows remote reads and writes.
    pub fn allow_remote_rw(&mut self) -> &mut Self {
        self.access = self.access
            | ib_access_flags::IB_ACCESS_REMOTE_WRITE
            | ib_access_flags::IB_ACCESS_REMOTE_READ;
        self
    }

    /// Set the access flags of the new `QueuePair` such that it allows remote atomic.
    pub fn allow_remote_atomic(&mut self) -> &mut Self {
        self.access = self.access | ib_access_flags::IB_ACCESS_REMOTE_ATOMIC;
        self
    }

    /// Set the minimum timeout that the new `QueuePair` waits for ACK/NACK from remote QP before
    /// retransmitting the packet.
    ///
    /// Default value is 4 (65.536µs).
    /// Relevant only to RC QPs.
    pub fn set_timeout(&mut self, timeout: u8) -> &mut Self {
        self.timeout = timeout;
        self
    }

    /// Set the number of times that the new `QueuePair` tries to resend the packets
    /// before reporting an error.
    ///
    /// Default value is 6
    pub fn set_retry_count(&mut self, retry_count: u8) -> &mut Self {
        self.retry_count = retry_count;
        self
    }

    /// Set the number of times that the new `QueuePair` tries to resend the packets when
    /// an RNR NACK was sent by the remote QP before reporting an error.
    ///
    /// Default value is 6
    pub fn set_rnr_retry(&mut self, rnr_retry: u8) -> &mut Self {
        self.rnr_retry = rnr_retry;
        self
    }

    pub fn set_path_mtu(&mut self, path_mtu: ib_mtu::Type) -> &mut Self {
        self.path_mtu = path_mtu;
        self
    }

    /// Set the minimum RNR NAK Timer Field Value for the new `QueuePair`.
    ///
    /// Default value is 16 (2.56 ms delay).
    /// Relevant only for RC QPs.
    pub fn set_min_rnr_timer(&mut self, min_rnr_timer: u8) -> &mut Self {
        self.min_rnr_timer = min_rnr_timer;
        self
    }

    pub fn set_pkey_index(&mut self, pkey_index: u16) -> &mut Self {
        self.pkey_index = pkey_index;
        self
    }

    /// Set the primary physical port number associated with this QP
    ///
    /// Default value is 1
    pub fn set_port_num(&mut self, port_num: u8) -> &mut Self {
        self.port_num = port_num;
        self
    }

    /// Set the Q_Key that incoming messages are check against and
    /// possibly used as the outgoing Q_Key.
    ///
    /// Default value is a random value with range 0~255
    ///
    /// Relevant only for UD QPs
    pub fn set_qkey(&mut self, qkey: u32) -> &mut Self {
        self.qkey = qkey;
        self
    }

    pub fn set_max_rd_atomic(&mut self, max_rd_atomic: u8) -> &mut Self {
        self.max_rd_atomic = max_rd_atomic;
        self
    }

    /// Build an unreliable datagram queue pair with the set parameters.
    ///
    /// The built queue pair needs to be brought up to be used.
    ///
    /// # Errors:
    /// - `CreationError` : error creating send completion queue
    /// or recv completion queue or ud queue pair
    ///
    pub fn build_ud(self) -> Result<PreparedQueuePair, ControlpathError> {
        let send = Box::new(CompletionQueue::create(&self.ctx, self.max_cq_entries)?);
        let recv = Arc::new(CompletionQueue::create(&self.ctx, self.max_cq_entries)?);

        let mut qp_attr = ib_qp_init_attr {
            cap: ib_qp_cap {
                max_send_wr: self.max_send_wr,
                max_recv_wr: self.max_recv_wr,
                max_recv_sge: self.max_recv_sge,
                max_send_sge: self.max_send_sge,
                max_inline_data: self.max_inline_data,
                ..Default::default()
            },
            sq_sig_type: ib_sig_type::IB_SIGNAL_REQ_WR,
            qp_type: ib_qp_type::IB_QPT_UD as u32,
            send_cq: send.raw_ptr().as_ptr(),
            recv_cq: recv.raw_ptr().as_ptr(),
            ..Default::default()
        };

        let ib_qp: *mut ib_qp = unsafe {
            ib_create_qp(
                self.ctx.get_pd().as_ptr(),
                &mut qp_attr as *mut ib_qp_init_attr,
            )
        };
        self.build_inner(ib_qp, send, recv, QPType::UD, None)
    }

    pub fn build_rc(self) -> Result<PreparedQueuePair, ControlpathError> {
        let send = Box::new(CompletionQueue::create(&self.ctx, self.max_cq_entries)?);
        let recv = Arc::new(CompletionQueue::create(&self.ctx, self.max_cq_entries)?);

        let mut qp_attr = ib_qp_init_attr {
            cap: ib_qp_cap {
                max_send_wr: self.max_send_wr,
                max_recv_wr: self.max_recv_wr,
                max_recv_sge: self.max_recv_sge,
                max_send_sge: self.max_send_sge,
                max_inline_data: self.max_inline_data,
                ..Default::default()
            },
            sq_sig_type: ib_sig_type::IB_SIGNAL_REQ_WR,
            qp_type: ib_qp_type::IB_QPT_RC as u32,
            send_cq: send.raw_ptr().as_ptr(),
            recv_cq: recv.raw_ptr().as_ptr(),
            ..Default::default()
        };

        let ib_qp: *mut ib_qp = unsafe {
            ib_create_qp(
                self.ctx.get_pd().as_ptr(),
                &mut qp_attr as *mut ib_qp_init_attr,
            )
        };
        self.build_inner(ib_qp, send, recv, QPType::RC, None)
    }

    pub(super) fn build_inner(
        self,
        qp_ptr: *mut ib_qp,
        send: Box<CompletionQueue>,
        recv: Arc<CompletionQueue>,
        qp_type: QPType,
        srq: Option<Box<crate::SharedReceiveQueue>>,
    ) -> Result<PreparedQueuePair, ControlpathError> {
        Ok(PreparedQueuePair {
            inner: QueuePair {
                _ctx: self.ctx,
                inner_qp: NonNull::new(qp_ptr)
                    .ok_or(ControlpathError::CreationError("QP", Error::EAGAIN))?,
                rc_comm: None,
                send_cq: send,
                recv_cq: recv,
                srq: srq,
                mode: qp_type,

                // the following is just borrowed from the builder
                // as the QP may require them during connections
                port_num: self.port_num,
                qkey: self.qkey,
                access: self.access,
                timeout: self.timeout,
                retry_count: self.retry_count,
                rnr_retry: self.rnr_retry,
                max_rd_atomic: self.max_rd_atomic,
                min_rnr_timer: self.min_rnr_timer,
                pkey_index: self.pkey_index,
                path_mtu: self.path_mtu,
            },
            retry_count: self.retry_count,
            rnr_retry: self.rnr_retry,
            pkey_index: self.pkey_index,
            port_num: self.port_num,
            qkey: self.qkey,
        })
    }
}

/// A PrepareQueuePair is used to create a queue pair
pub struct PreparedQueuePair {
    inner: QueuePair,

    // carried from builder, necessary to bringup
    retry_count: u8,
    rnr_retry: u8,
    pkey_index: u16,
    port_num: u8,
    qkey: u32,
}

impl PreparedQueuePair {
    /// Bring up UD by modifying attributes of a queue pair. The returned
    /// queue pair is able to be used for communication.
    ///
    /// Attributes modified in RESET -> INIT:
    /// - qp_state : set to `ib_qp_state::IB_QPS_INIT`
    /// - pkey_index : set to the value in the builder's corresponding field
    /// - port_num : set to the value in the builder's corresponding field
    /// - qkey : set to the value in the builder's corresponding field
    ///
    /// Attributes modified in INIT -> RTR
    /// - qp_state : set to `ib_qp_state::IB_QPS_RTR`
    ///
    /// Attributes modified in RTR -> RTS:
    /// - qp_state : set to `ib_qp_state::IB_QPS_RTS`
    /// - sq_psn : set to inner `ib_qp`'s current qp_num
    ///
    pub fn bring_up_ud(self) -> Result<Arc<QueuePair>, ControlpathError> {
        if self.inner.mode != QPType::UD {
            log::error!("bring up type check error!");
            return Err(ControlpathError::CreationError(
                "bring up type check error!",
                Error::from_kernel_errno(0),
            ));
        }
        let qp_status = self.inner.status()?;
        if qp_status == QueuePairStatus::Reset {
            let mut attr = ib_qp_attr {
                qp_state: ib_qp_state::IB_QPS_INIT,
                pkey_index: self.pkey_index,
                port_num: self.port_num,
                qkey: self.qkey,
                ..Default::default()
            };

            let mask = ib_qp_attr_mask::IB_QP_STATE
                | ib_qp_attr_mask::IB_QP_PKEY_INDEX
                | ib_qp_attr_mask::IB_QP_PORT
                | ib_qp_attr_mask::IB_QP_QKEY;

            let ret =
                unsafe { ib_modify_qp(self.inner.inner_qp.as_ptr(), &mut attr as *mut _, mask) };
            if ret != 0 {
                log::error!("bring UD INIT error!");
                return Err(ControlpathError::CreationError(
                    "bring UD INIT error",
                    Error::from_kernel_errno(ret),
                ));
            }

            // RTR
            let mut attr = ib_qp_attr {
                qp_state: ib_qp_state::IB_QPS_RTR,
                ..Default::default()
            };

            let mask = ib_qp_attr_mask::IB_QP_STATE;

            let ret =
                unsafe { ib_modify_qp(self.inner.inner_qp.as_ptr(), &mut attr as *mut _, mask) };
            if ret != 0 {
                log::error!("bring UD RTR error!");
                return Err(ControlpathError::CreationError(
                    "bring UD RTR error",
                    Error::from_kernel_errno(ret),
                ));
            }

            // RTS
            let mut attr = ib_qp_attr {
                qp_state: ib_qp_state::IB_QPS_RTS,
                sq_psn: unsafe { (*self.inner.inner_qp.as_ptr()).qp_num },
                ..Default::default()
            };

            let mask = ib_qp_attr_mask::IB_QP_STATE | ib_qp_attr_mask::IB_QP_SQ_PSN;

            let ret =
                unsafe { ib_modify_qp(self.inner.inner_qp.as_ptr(), &mut attr as *mut _, mask) };
            if ret != 0 {
                log::error!("bring UD RTS error!");
                return Err(ControlpathError::CreationError(
                    "bring UD RTS error",
                    Error::from_kernel_errno(ret),
                ));
            }
        }
        Ok(Arc::new(self.inner))
    }

    /// Bring up Rc-QP : RESET => INIT => RTR => RTS
    ///
    /// A qp must be set to RTS before being used to post-send something.
    ///
    /// Param:
    /// - `lid`: lid of remote qp to communicate with.
    /// - `gid`: gid of remote qp to communicate with.
    /// - `remote_qpn`: remote qp's queue pair number
    /// - `psn`: remote qp's send queue sequence number
    ///
    /// Return an `Arc<QueuePair>` ready to send if all ok and control path error otherwise.
    ///
    #[inline]
    pub(crate) fn bring_up_rc(
        self,
        lid: u32,
        gid: ib_gid,
        remote_qpn: u32,
        psn: u32,
    ) -> Result<Arc<QueuePair>, ControlpathError> {
        let mut qp = Arc::new(self.inner);
        let qp_ref = unsafe { Arc::get_mut_unchecked(&mut qp) };
        let _ = qp_ref.bring_up_rc_inner(lid, gid, remote_qpn, psn)?;
        Ok(qp)
    }

    /// The method `handshake` serves for RC control path, establishing communication between client
    /// and server. After the handshake phase, client side RC qp's `rc_comm` field will be filled. This
    /// stores a C struct `ib_cm_id` responsible for exchanging communication message and keep the connection.
    ///
    /// Param:
    /// - `remote_service_id` : Predefined service id that you want to query
    /// - `sa_path_rec` : path resolved by `Explorer`, attention that the resolved path must be consistent with the remote_service_id
    ///
    pub fn handshake(
        self,
        remote_service_id: u64,
        mut path: sa_path_rec,
    ) -> Result<Arc<QueuePair>, ControlpathError> {
        // check qp type
        // must be rc and never initialized
        if self.inner.mode != QPType::RC {
            assert!(self.inner.rc_comm.is_some());
            log::error!("bring up type check error!");
            return Err(ControlpathError::CreationError(
                "QP mode that is not RC does not need handshake to bring it up!",
                Error::from_kernel_errno(0),
            ));
        }

        let mut rc_qp = Arc::new(self.inner);

        // 1. create the CM for handling the communication
        let rc_comm = RCCommStruct::new(rc_qp._ctx.get_dev_ref(), &rc_qp).map_err(|_| {
            ControlpathError::CreationError("ib_cm_id", Error::from_kernel_errno(0))
        })?;

        let rc_qp_ref = unsafe { Arc::get_mut_unchecked(&mut rc_qp) };
        rc_qp_ref.rc_comm = Some(rc_comm);

        // 2. construct the handshake data
        let data = RCConnectionData::new(rc_qp_ref).map_err(|err| {
            let errno = match err {
                CMError::Creation(errno) => errno,
                _ => 0,
            };
            ControlpathError::CreationError("RC queue pair", Error::from_kernel_errno(errno))
        })?;

        let req = ib_cm_req_param {
            primary_path: &mut path as *mut sa_path_rec,
            service_id: remote_service_id,
            qp_type: ib_qp_type::IB_QPT_RC,
            responder_resources: 16,
            initiator_depth: 16,
            remote_cm_response_timeout: 20,
            local_cm_response_timeout: 20,
            max_cm_retries: 15,
            rnr_retry_count: self.rnr_retry,
            retry_count: self.retry_count,
            flow_control: 1,
            qp_num: rc_qp_ref.qp_num(),
            starting_psn: rc_qp_ref.qp_num(),
            ..Default::default()
        };

        let rc_comm_ref = rc_qp_ref.rc_comm.as_mut().unwrap();

        let _ = rc_comm_ref.send_req(req, data).map_err(|_| {
            log::error!("RC qp send request error");
            ControlpathError::QueryError("send request error", Error::from_kernel_errno(0))
        })?;

        let _ = rc_comm_ref
            .wait(crate::CONNECT_TIME_OUT_MS)
            .map_err(|err: Error| ControlpathError::ContextError("wait completion error", err))?;
        if rc_qp_ref.status()? == QueuePairStatus::ReadyToSend {
            log::debug!("Handshake OK");
            Ok(rc_qp)
        } else {
            log::debug!("Handshake Error");
            Err(ControlpathError::CreationError(
                "qp status error",
                Error::from_kernel_errno(0),
            ))
        }
    }

    #[cfg(feature = "dct")]
    /// Bring up DC by modifying attributes of a queue pair. The returned
    /// queue pair is able to be used for communication.
    ///
    /// Attributes modified in RESET -> INIT:
    /// - qp_state : set to `ib_qp_state::IB_QPS_INIT`
    /// - pkey_index : set to the value in the builder's corresponding field
    /// - port_num : set to the value in the builder's corresponding field
    /// - qkey : set to the value in the builder's corresponding field
    ///
    /// Attributes modified in INIT -> RTR
    /// - qp_state : set to `ib_qp_state::IB_QPS_RTR`
    ///
    /// Attributes modified in RTR -> RTS:
    /// - qp_state : set to `ib_qp_state::IB_QPS_RTS`
    /// - sq_psn : set to inner `ib_qp`'s current qp_num
    ///
    pub fn bring_up_dc(self) -> Result<Arc<QueuePair>, ControlpathError> {
        if self.inner.mode != QPType::DC {
            return Err(ControlpathError::CreationError(
                "bring up type check error!",
                Error::from_kernel_errno(0),
            ));
        }

        let qp_status = self.inner.status()?;
        if qp_status == QueuePairStatus::Reset {
            let mut attr = ib_qp_attr {
                qp_state: ib_qp_state::IB_QPS_INIT,
                pkey_index: self.pkey_index,
                port_num: self.port_num,
                ..Default::default()
            };

            let mask = ib_qp_attr_mask::IB_QP_STATE
                | ib_qp_attr_mask::IB_QP_PKEY_INDEX
                | ib_qp_attr_mask::IB_QP_PORT
                | ib_qp_attr_mask::IB_QP_DC_KEY;

            let ret =
                unsafe { ib_modify_qp(self.inner.inner_qp.as_ptr(), &mut attr as *mut _, mask) };
            if ret != 0 {
                log::error!("bring DC INIT error!");
                return Err(ControlpathError::CreationError(
                    "bring DC INIT error",
                    Error::from_kernel_errno(ret),
                ));
            }
        }

        let qp_status = self.inner.status()?;
        if qp_status == QueuePairStatus::Init {
            // RTR
            let mut qp_attr: ib_qp_attr = Default::default();
            let mut mask: linux_kernel_module::c_types::c_int = ib_qp_attr_mask::IB_QP_STATE;

            qp_attr.qp_state = ib_qp_state::IB_QPS_RTR;

            qp_attr.path_mtu = self.inner.path_mtu();
            mask = mask | ib_qp_attr_mask::IB_QP_PATH_MTU;

            qp_attr.ah_attr.port_num = self.port_num;

            qp_attr.ah_attr.sl = 0;
            qp_attr.ah_attr.ah_flags = 0;
            mask = mask | ib_qp_attr_mask::IB_QP_AV;

            let ret =
                unsafe { ib_modify_qp(self.inner.inner_qp.as_ptr(), &mut qp_attr as *mut _, mask) };
            if ret != 0 {
                log::error!("bring DC RTR error!");
                return Err(ControlpathError::CreationError(
                    "bring DC RTR error",
                    Error::from_kernel_errno(ret),
                ));
            }
        }

        let qp_status = self.inner.status()?;
        if qp_status == QueuePairStatus::ReadyToRecv {
            // RTS
            let mut attr = ib_qp_attr {
                qp_state: ib_qp_state::IB_QPS_RTS,
                timeout: self.inner.timeout(),
                retry_cnt: self.retry_count,
                rnr_retry: self.rnr_retry,
                max_rd_atomic: self.inner.max_rd_atomic(),
                ..Default::default()
            };

            let mask = ib_qp_attr_mask::IB_QP_STATE
                | ib_qp_attr_mask::IB_QP_TIMEOUT
                | ib_qp_attr_mask::IB_QP_RETRY_CNT
                | ib_qp_attr_mask::IB_QP_RNR_RETRY
                | ib_qp_attr_mask::IB_QP_MAX_QP_RD_ATOMIC;

            let ret =
                unsafe { ib_modify_qp(self.inner.inner_qp.as_ptr(), &mut attr as *mut _, mask) };
            if ret != 0 {
                log::error!("bring DC RTS error!");
                return Err(ControlpathError::CreationError(
                    "bring DC RTS error",
                    Error::from_kernel_errno(ret),
                ));
            }
        }
        Ok(Arc::new(self.inner))
    }
}
