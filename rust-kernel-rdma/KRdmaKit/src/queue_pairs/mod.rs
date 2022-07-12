use crate::{CompletionQueue, context::Context};
use alloc::{boxed::Box, sync::Arc};
use core::ptr::NonNull;
use rust_kernel_rdma_base::*;

/// UD queue pair builder to simplify UD creation
pub mod builder;

/// UD endpoint and queriers
pub mod endpoint;

/// services for simplify UD connections
pub mod ud_services;

#[allow(dead_code)]
enum QPType {
    RC, // reliable connection
    UD, // unreliable datagram
    WC, // unreliable conenction
    DC, // dynamica connected transprot
}

#[derive(Debug, PartialEq)]
#[repr(u32)]
enum QueuePairStatus {
    Reset = ib_qp_state::IB_QPS_RESET,
    Init = ib_qp_state::IB_QPS_INIT,
    ReadyToSend = ib_qp_state::IB_QPS_RTS,
    ReadyToRecv = ib_qp_state::IB_QPS_RTR,
    // There is some other fields, but seems not necessary
    Error = ib_qp_state::IB_QPS_ERR,
}

/// Abstraction for a queue pair.
/// A QueuePair must be bringup, which is able to post_send & poll_cq
pub struct QueuePair {
    _ctx: Arc<Context>,
    inner: NonNull<ib_qp>,

    /// the send_cq must be exclusively used by a QP
    /// thus, it is an Box
    send_cq: Box<CompletionQueue>,

    /// the recv_cq can be shared between QPs
    /// so it is an Arc
    recv_cq: Arc<CompletionQueue>,

    mode: QPType,

    /// sidr request handler need this field
    // FIXME : duplicate with the field in PreparedQueuePair
    qkey: u32,
    port_num: u8,
}

impl QueuePair {
    /// query the current status of the QP
    fn status(&self) -> Result<QueuePairStatus, crate::ControlpathError> {
        let mut attr: ib_qp_attr = Default::default();
        let mut init_attr: ib_qp_init_attr = Default::default();
        let ret = unsafe {
            ib_query_qp(
                self.inner.as_ptr(),
                &mut attr as *mut ib_qp_attr,
                ib_qp_attr_mask::IB_QP_STATE,
                &mut init_attr as *mut ib_qp_init_attr,
            )
        };

        if ret != 0 {
            Err(crate::ControlpathError::QueryError(
                "QP status",
                linux_kernel_module::Error::from_kernel_errno(ret),
            ))
        } else {
            match attr.qp_state {
                ib_qp_state::IB_QPS_RESET => Ok(QueuePairStatus::Reset),
                ib_qp_state::IB_QPS_INIT => Ok(QueuePairStatus::Init),
                ib_qp_state::IB_QPS_RTS => Ok(QueuePairStatus::ReadyToSend),
                ib_qp_state::IB_QPS_RTR => Ok(QueuePairStatus::ReadyToRecv),
                _ => Ok(QueuePairStatus::Error),
            }
        }
    }

    pub fn qp_num(&self) -> u32 {
        unsafe { self.inner.as_ref().qp_num }
    }

    pub fn port_num(&self) -> u8 {
        self.port_num
    }

    pub fn qkey(&self) -> u32 {
        self.qkey
    }

    pub fn ctx(&self) -> &Arc<Context> {
        &self._ctx
    }
}

impl Drop for QueuePair {
    fn drop(&mut self) {
        unsafe {
            ib_destroy_qp(self.inner.as_ptr());
        }
    }
}
