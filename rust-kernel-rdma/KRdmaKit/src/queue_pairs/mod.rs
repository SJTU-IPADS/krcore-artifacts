use core::iter::TrustedRandomAccessNoCoerce;
use core::ops::Range;
use core::ptr::NonNull;

use crate::memory_region::MemoryRegion;
use crate::queue_pairs::endpoint::UnreliableDatagramEndpoint;
use crate::{context::Context, CompletionQueue, DatapathError};
use alloc::{boxed::Box, sync::Arc};

use linux_kernel_module::Error;
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
                Error::from_kernel_errno(ret),
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

    #[inline]
    pub fn qp_num(&self) -> u32 {
        unsafe { self.inner.as_ref().qp_num }
    }

    #[inline]
    pub fn port_num(&self) -> u8 {
        self.port_num
    }

    #[inline]
    pub fn qkey(&self) -> u32 {
        self.qkey
    }

    #[inline]
    pub fn ctx(&self) -> &Arc<Context> {
        &self._ctx
    }

    /// Post a work request (related to UD) to the send queue of the queue pair, add it to the tail of the send queue
    /// without context switch. The RDMA device will handle it (later) in asynchronous way.
    ///
    /// The parameter `range` indexes the input memory region, treat it as
    /// a byte array and manipulate the memory in byte unit.
    ///
    /// `wr_id` is a 64 bits value associated with this WR. If a Work Completion is generated
    /// when this Work Request ends, it will contain this value.
    ///
    /// If you need more information about post_send, please refer to
    /// [RDMAmojo](https://www.rdmamojo.com/2013/01/26/ibv_post_send/) for help.
    pub fn post_datagram(
        &self,
        endpoint: &UnreliableDatagramEndpoint,
        mr: &MemoryRegion,
        range: Range<u64>,
        wr_id: u64,
        signaled: bool,
    ) -> Result<(), DatapathError> {
        let mut wr: ib_ud_wr = Default::default();
        let mut bad_wr: *mut ib_send_wr = core::ptr::null_mut();

        // first setup the sge
        let mut sge = ib_sge { 
            addr : unsafe { mr.get_rdma_addr() } + range.start, 
            length : range.size() as u32, 
            lkey : mr.lkey().0
        };        

        // then set the wr
        wr.remote_qpn = endpoint.qpn();
        wr.remote_qkey = endpoint.qkey();
        wr.ah = endpoint.raw_address_handler_ptr().as_ptr();
        wr.wr.opcode = ib_wr_opcode::IB_WR_SEND;
        wr.wr.send_flags = if signaled {
            ib_send_flags::IB_SEND_SIGNALED
        } else {
            0
        };
        wr.wr.sg_list = &mut sge as *mut _;
        wr.wr.num_sge = 1;
        wr.wr.__bindgen_anon_1.wr_id = wr_id;
        let err = unsafe {
            bd_ib_post_send(
                self.inner.as_ptr(),
                &mut wr.wr as *mut _,
                &mut bad_wr as *mut _,
            )
        };
        if err != 0 {
            Err(DatapathError::PostSendError(Error::from_kernel_errno(err)))
        } else {
            Ok(())
        }
    }

    /// Post a work request to the receive queue of the queue pair, add it to the tail of the
    /// receive queue without context switch. The RDMA device will take one
    /// of those Work Requests as soon as an incoming opcode to that QP consumes a Receive
    /// Request.
    ///
    /// The parameter `range` indexes the input memory region, treat it as
    /// a byte array and manipulate the memory in byte unit.
    ///
    /// Please refer to
    /// [RDMAmojo](https://www.rdmamojo.com/2013/02/02/ibv_post_recv/) for more information.
    pub fn post_recv(
        &self,
        mr: &MemoryRegion,
        range: Range<u64>,
        wr_id: u64,
    ) -> Result<(), DatapathError> {
        let mut wr: ib_recv_wr = Default::default();
        let mut bad_wr: *mut ib_recv_wr = core::ptr::null_mut();

        let mut sge = ib_sge { 
            addr : unsafe { mr.get_rdma_addr() } + range.start, 
            length : range.size() as u32, 
            lkey : mr.lkey().0
        };

        wr.sg_list = &mut sge as *mut _;
        wr.num_sge = 1;
        unsafe { bd_set_recv_wr_id(&mut wr, wr_id) };
        let err = unsafe {
            bd_ib_post_recv(
                self.inner.as_ptr(),
                &mut wr as *mut _,
                &mut bad_wr as *mut _,
            )
        };
        if err != 0 {
            Err(DatapathError::PostRecvError(Error::from_kernel_errno(err)))
        } else {
            Ok(())
        }
    }

    /// Poll send_cq. Returned slice length is 0 if no work completion polled
    #[inline]
    pub fn poll_send_cq<'c>(
        &self,
        completions: &'c mut [ib_wc],
    ) -> Result<&'c mut [ib_wc], DatapathError> {
        self.send_cq.poll(completions)
    }

    /// Poll recv_cq. Returned slice length is 0 if no work completion polled
    #[inline]
    pub fn poll_recv_cq<'c>(
        &self,
        completions: &'c mut [ib_wc],
    ) -> Result<&'c mut [ib_wc], DatapathError> {
        self.recv_cq.poll(completions)
    }
}

impl Drop for QueuePair {
    fn drop(&mut self) {
        unsafe {
            ib_destroy_qp(self.inner.as_ptr());
        }
    }
}
