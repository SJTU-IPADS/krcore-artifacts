use crate::bindings::ib_qp_cap;
use crate::context::Context;
use crate::queue_pairs::{QPType, QueuePair};
use crate::{CompletionQueue, ControlpathError};
use crate::rust_kernel_linux_util as log;
use alloc::{boxed::Box, sync::Arc};
use core::ptr::NonNull;
use rust_kernel_rdma_base::*;

/// Builder for different kind of queue pairs (RCQP, UDQP ,etc.).
///
/// Set necessary fields and build the corresponding queue pairs.
pub struct QueuePairBuilder {
    ctx: Arc<Context>,
    max_send_wr: u32,
    max_recv_wr : u32,
    max_cq_entries: u32,
    max_send_sge: u32,
    max_recv_sge: u32,
    max_inline_data: u32,

    // carried along to handshake phase
    access: ib_access_flags::Type,
    timeout: u8,
    retry_count: u8,
    rnr_retry: u8,
    min_rnr_timer: u8,
    pkey_index: u16,
    port_num: u8,
    qkey: u32,
}

impl QueuePairBuilder {
    /// Create a QueuePairBuilder with the given `Context` which stores the device,
    /// memory region and protection domain information.
    ///
    /// Set the builder's necessary fields and build the queue pair that's needed.
    pub fn new(ctx: &Arc<Context>) -> Self {
        Self {
            ctx : ctx.clone(),
            max_send_wr: 64,
            max_recv_wr : 1024,
            max_cq_entries: 64,
            max_send_sge: 1,
            max_recv_sge: 1,
            max_inline_data: 64,
            access: ib_access_flags::IB_ACCESS_LOCAL_WRITE,
            timeout: 4,
            retry_count: 6,
            rnr_retry: 6,
            min_rnr_timer: 16,
            pkey_index: 0,
            port_num: 1,
            qkey: 73,
        }
    }

    /// Set both `max_send_wr` for the new `QueuePair`
    ///
    /// Default value is 64
    pub fn set_max_send_wr(&mut self, max_wr: u32) -> &mut Self {
        self.max_send_wr = max_wr;
        self
    }

    /// Set both and `max_recv_wr` for the new `QueuePair`
    ///
    /// Default value is 1024
    pub fn set_max_recv_wr(&mut self, max_wr: u32) -> &mut Self {
        self.max_recv_wr = max_wr;
        self
    }    

    /// Set the maximum number of completion queue entries for
    /// the new `QueuePair`'s completion queue
    ///
    /// Default value is 64
    pub fn set_max_cq_entries(&mut self, max_cq_entries: u32) -> &mut Self {
        self.max_cq_entries = max_cq_entries;
        self
    }

    /// Set the maximum number of scatter/gather elements in any Work Request
    /// that can be posted to the Send Queue in that Queue Pair.
    ///
    /// Default value is 1
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
    /// Default value is 73
    /// Relevant only for UD QPs
    pub fn set_qkey(&mut self, qkey: u32) -> &mut Self {
        self.qkey = qkey;
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
        self.build_inner(ib_qp, send, recv)
    }

    fn build_inner(
        self,
        qp_ptr: *mut ib_qp,
        send: Box<CompletionQueue>,
        recv: Arc<CompletionQueue>,
    ) -> Result<PreparedQueuePair, ControlpathError> {
        Ok(PreparedQueuePair {
            inner: QueuePair {
                _ctx: self.ctx,
                inner: NonNull::new(qp_ptr)
                    .ok_or(ControlpathError::CreationError("QP", linux_kernel_module::Error::EAGAIN))?,
                send_cq: send,
                recv_cq: recv,
                mode: QPType::UD,
                qkey: self.qkey,
                port_num: self.port_num,
            },
            access: self.access,
            timeout: self.timeout,
            retry_count: self.retry_count,
            rnr_retry: self.rnr_retry,
            min_rnr_timer: self.min_rnr_timer,
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
    access: ib_access_flags::Type,
    timeout: u8,
    retry_count: u8,
    rnr_retry: u8,
    min_rnr_timer: u8,
    pkey_index: u16,
    port_num: u8,
    qkey: u32,
}

impl PreparedQueuePair {
    /// UD, RC & DC have different bring-up process
    pub fn bring_up(self) -> Result<QueuePair, ControlpathError> {
        match self.inner.mode {
            QPType::UD => self.bring_up_ud(),
            _ => unimplemented!(),
        }
    }

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
    fn bring_up_ud(self) -> Result<QueuePair, ControlpathError> {
        let qp_status = self.inner.status()?;
        if qp_status == super::QueuePairStatus::Reset {
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

            let ret = unsafe { ib_modify_qp(self.inner.inner.as_ptr(), &mut attr as *mut _, mask) };
            if ret != 0 {
                log::error!("bring UD INIT error!");
                return Err(ControlpathError::CreationError(
                    "bring UD INIT error",
                    linux_kernel_module::Error::from_kernel_errno(ret),
                ));
            }

            // RTR
            let mut attr = ib_qp_attr {
                qp_state: ib_qp_state::IB_QPS_RTR,
                ..Default::default()
            };

            let mask = ib_qp_attr_mask::IB_QP_STATE;

            let ret = unsafe { ib_modify_qp(self.inner.inner.as_ptr(), &mut attr as *mut _, mask) };
            if ret != 0 {
                log::error!("bring UD RTR error!");
                return Err(ControlpathError::CreationError(
                    "bring UD RTR error",
                    linux_kernel_module::Error::from_kernel_errno(ret),
                ));
            }

            // RTS
            let mut attr = ib_qp_attr {
                qp_state: ib_qp_state::IB_QPS_RTS,
                sq_psn: unsafe { (*self.inner.inner.as_ptr()).qp_num },
                ..Default::default()
            };

            let mask = ib_qp_attr_mask::IB_QP_STATE | ib_qp_attr_mask::IB_QP_SQ_PSN;

            let ret = unsafe { ib_modify_qp(self.inner.inner.as_ptr(), &mut attr as *mut _, mask) };
            if ret != 0 {
                log::error!("bring UD RTS error!");
                return Err(ControlpathError::CreationError(
                    "bring UD RTS error",
                    linux_kernel_module::Error::from_kernel_errno(ret),
                ));
            }
        }
        Ok(self.inner)
    }
}
