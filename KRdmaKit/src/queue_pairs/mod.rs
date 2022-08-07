use rdma_shim::bindings::*;
#[allow(unused_imports)]
use rdma_shim::{log, Error, KernelResult};

#[allow(unused_imports)]
use rdma_shim::ffi::c_types;

use alloc::{boxed::Box, sync::Arc};
use core::iter::TrustedRandomAccessNoCoerce;
use core::ops::Range;
use core::ptr::{null_mut, NonNull};

use crate::memory_region::MemoryRegion;
use crate::{context::Context, CompletionQueue, DatapathError, SharedReceiveQueue};

pub use crate::queue_pairs::endpoint::DatagramEndpoint;

#[allow(unused_imports)]
use crate::CMError;

/// Queue pair builders
pub mod builder;
pub use builder::{PreparedQueuePair, QueuePairBuilder};

#[cfg(feature = "kernel")]
mod callbacks;

/// UD endpoint and queriers
pub mod endpoint;

#[cfg(feature = "kernel")]
/// Abstract the communication manager related data structures
/// related to reliable QP connections
mod rc_comm;

/// All the DCT related implementatations are encauplasted in this module
#[cfg(feature = "dct")]
pub mod dynamic_connected_transport;

#[cfg(feature = "dct")]
pub use dynamic_connected_transport::DynamicConnectedTarget;

#[allow(dead_code)]
#[derive(PartialEq)]
enum QPType {
    RC, // reliable connection
    UD, // unreliable datagram
    WC, // unreliable conenction
    DC, // dynamica connected transprot
}

#[derive(Debug, PartialEq)]
#[repr(u32)]
pub enum QueuePairStatus {
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
    inner_qp: NonNull<ib_qp>,

    /// data structures related to remote connections
    /// is only need for RC, so it is an Option
    #[cfg(feature = "kernel")]
    rc_comm: Option<Box<rc_comm::RCCommStruct<Self>>>,

    /// the send_cq must be exclusively used by a QP
    /// thus, it is an Box
    send_cq: Box<CompletionQueue>,

    /// the recv_cq can be shared between QPs
    /// so it is an Arc
    recv_cq: Arc<CompletionQueue>,

    #[allow(dead_code)]
    srq: Option<Box<SharedReceiveQueue>>,

    mode: QPType,

    #[cfg(feature = "user")]
    post_send_op: unsafe extern "C" fn(
        qp: *mut ib_qp,
        wr: *mut ib_send_wr,
        bad_wr: *mut *mut ib_send_wr,
    ) -> c_types::c_int,

    #[cfg(feature = "user")]
    post_recv_op: unsafe extern "C" fn(
        qp: *mut ib_qp,
        wr: *mut ib_recv_wr,
        bad_wr: *mut *mut ib_recv_wr,
    ) -> c_types::c_int,

    /// sidr request handler need this field
    port_num: u8,
    qkey: u32,

    access: ib_access_flags::Type,

    timeout: u8,
    retry_count: u8,
    rnr_retry: u8,
    max_rd_atomic: u8,
    min_rnr_timer: u8,
    pkey_index: u16,
    path_mtu: ib_mtu::Type,
}

impl QueuePair {
    /// query the current status of the QP
    pub fn status(&self) -> Result<QueuePairStatus, crate::ControlpathError> {
        let mut attr: ib_qp_attr = Default::default();
        let mut init_attr: ib_qp_init_attr = Default::default();

        #[cfg(feature = "kernel")]
        let ret = unsafe {
            ib_query_qp(
                self.inner_qp.as_ptr(),
                &mut attr as *mut ib_qp_attr,
                ib_qp_attr_mask::IB_QP_STATE,
                &mut init_attr as *mut ib_qp_init_attr,
            )
        };

        #[cfg(feature = "user")]
        let ret = unsafe {
            ib_query_qp(
                self.inner_qp.as_ptr(),
                &mut attr as *mut ib_qp_attr,
                ib_qp_attr_mask::IBV_QP_STATE as _,
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

    pub fn path_mtu(&self) -> ib_mtu::Type {
        self.path_mtu
    }

    #[inline]
    pub fn qp_num(&self) -> u32 {
        unsafe { self.inner_qp.as_ref().qp_num }
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
    pub fn timeout(&self) -> u8 {
        self.timeout
    }

    #[inline]
    pub fn max_rd_atomic(&self) -> u8 {
        self.max_rd_atomic
    }

    #[inline]
    pub fn ctx(&self) -> &Arc<Context> {
        &self._ctx
    }

    #[inline]
    pub fn lid(&self) -> KernelResult<u32> {
        Ok(self.ctx().get_port_attr(self.port_num)?.lid as _)
    }

    #[inline]
    pub fn gid(&self) -> KernelResult<ib_gid> {
        // FIXME: what if gid index is not 0?
        Ok(self.ctx().query_gid(self.port_num, 0)?)
    }

    #[cfg(feature = "kernel")]
    /// get the datagram related data, namely:
    /// - gid
    /// - lid
    ///
    /// We mainly query them from the context
    ///
    pub fn get_datagram_meta(&self) -> Result<crate::services::DatagramMeta, CMError> {
        let port_attr = self
            .ctx()
            .get_dev_ref()
            .get_port_attr(self.port_num)
            .map_err(|err| CMError::Creation(err.to_kernel_errno()))?;
        let gid = self
            .ctx()
            .get_dev_ref()
            // FIXME: what if gid_index != 0?
            .query_gid(self.port_num, 0)
            .map_err(|err| CMError::Creation(err.to_kernel_errno()))?;

        let lid = port_attr.lid as u16;
        Ok(crate::services::DatagramMeta { lid, gid })
    }

    /// Post a work request to the receive queue of the queue pair, add it to the tail of the
    /// receive queue without context switch. The RDMA device will take one
    /// of those Work Requests as soon as an incoming opcode to that QP consumes a Receive
    /// Request.
    ///
    /// The parameter `range` indexes the input memory region, treats it as
    /// a byte array and manipulates the memory in byte unit.
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
        let mut bad_wr: *mut ib_recv_wr = null_mut();

        let mut sge = ib_sge {
            addr: unsafe { mr.get_rdma_addr() } + range.start,
            length: range.size() as u32,
            lkey: mr.lkey().0,
        };

        wr.sg_list = &mut sge as *mut _;
        wr.num_sge = 1;

        #[cfg(feature = "kernel")]
        unsafe {
            bd_set_recv_wr_id(&mut wr, wr_id)
        };

        #[cfg(feature = "user")]
        {
            wr.wr_id = wr_id;
        }

        #[cfg(feature = "kernel")]
        let err = unsafe {
            bd_ib_post_recv(
                self.inner_qp.as_ptr(),
                &mut wr as *mut _,
                &mut bad_wr as *mut _,
            )
        };

        #[cfg(feature = "user")]
        let err =
            unsafe { (self.post_recv_op)(self.inner_qp.as_ptr(), &mut wr as _, &mut bad_wr as _) };

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
            ib_destroy_qp(self.inner_qp.as_ptr());

            #[cfg(feature = "kernel")]
            self.rc_comm.as_mut().map(|c| c.explicit_drop());
        }
    }
}

// post-send operation implementations in the kernel space
#[cfg(feature = "kernel")]
include!("./operations_kernel.rs");

// post-send operation implementations in the user space
#[cfg(feature = "user")]
include!("./operations_user.rs");
