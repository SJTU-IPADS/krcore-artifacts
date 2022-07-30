#[allow(unused_imports)]
use rdma_shim::{Error, println, log};
use rdma_shim::bindings::*;

use alloc::{boxed::Box, sync::Arc};
use core::iter::TrustedRandomAccessNoCoerce;
use core::ops::Range;
use core::ptr::{null_mut, NonNull};

use crate::memory_region::MemoryRegion;
use crate::queue_pairs::endpoint::DatagramEndpoint;
use crate::{context::Context, CompletionQueue, DatapathError, SharedReceiveQueue};
use crate::comm_manager::CMError;

/// UD queue pair builder to simplify UD creation
pub mod builder;
pub use builder::{PreparedQueuePair, QueuePairBuilder};

mod callbacks;
/// UD endpoint and queriers
pub mod endpoint;

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
        let ret = unsafe {
            ib_query_qp(
                self.inner_qp.as_ptr(),
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

    /// get the datagram related data, namely:
    /// - gid
    /// - lid
    ///
    /// We mainly query them from the context
    ///
    pub fn get_datagram_meta(
        &self,
    ) -> Result<crate::services::DatagramMeta, CMError> {
        let port_attr = self
            .ctx()
            .get_dev_ref()
            .get_port_attr(self.port_num)
            .map_err(|err| CMError::Creation(err.to_kernel_errno()))?;
        let gid = self
            .ctx()
            .get_dev_ref()
            .query_gid(self.port_num)
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
        unsafe { bd_set_recv_wr_id(&mut wr, wr_id) };
        let err = unsafe {
            bd_ib_post_recv(
                self.inner_qp.as_ptr(),
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
            ib_destroy_qp(self.inner_qp.as_ptr());
            self.rc_comm.as_mut().map(|c| c.explicit_drop());
        }
    }
}

/// Unreliable Datagram
impl QueuePair {
    /// Post a work request (related to UD) to the send queue of the queue pair, add it to the tail of the send queue
    /// without context switch. The RDMA device will handle it (later) in asynchronous way.
    ///
    /// The parameter `range` indexes the input memory region, treats it as
    /// a byte array and manipulates the memory in byte unit.
    ///
    /// `wr_id` is a 64 bits value associated with this WR. If a Work Completion is generated
    /// when this Work Request ends, it will contain this value.
    ///
    /// If you need more information about post_send, please refer to
    /// [RDMAmojo](https://www.rdmamojo.com/2013/01/26/ibv_post_send/) for help.
    pub fn post_datagram(
        &self,
        endpoint: &DatagramEndpoint,
        mr: &MemoryRegion,
        range: Range<u64>,
        wr_id: u64,
        signaled: bool,
    ) -> Result<(), DatapathError> {
        if self.mode != QPType::UD {
            return Err(DatapathError::QPTypeError);
        }
        let mut wr: ib_ud_wr = Default::default();
        let mut bad_wr: *mut ib_send_wr = null_mut();

        // first setup the sge
        let mut sge = ib_sge {
            addr: unsafe { mr.get_rdma_addr() } + range.start,
            length: range.size() as u32,
            lkey: mr.lkey().0,
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
                self.inner_qp.as_ptr(),
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
}

/// Reliable Connection
impl QueuePair {
    #[inline]
    pub fn post_send_send(
        &self,
        mr: &MemoryRegion,
        range: Range<u64>,
        signaled: bool,
        raddr: u64,
        rkey: u32,
    ) -> Result<(), DatapathError> {
        if self.mode != QPType::RC {
            return Err(DatapathError::QPTypeError);
        }
        let send_flag: i32 = if signaled {
            ib_send_flags::IB_SEND_SIGNALED
        } else {
            0
        };
        self.post_send_inner(
            ib_wr_opcode::IB_WR_SEND,
            unsafe { mr.get_rdma_addr() } + range.start,
            raddr,
            mr.lkey().0,
            rkey,
            range.size() as u32,
            0,
            send_flag,
        )
    }

    /// Post a one-sided RDMA read work request to the send queue.
    ///
    /// Param:
    /// - `mr`: Reference to MemoryRegion to store read data from the remote side.
    /// - `range`: Specify which range of mr you want to store the read data. Index the mr in byte dimension.
    /// - `signaled`: Whether signaled while post send read
    /// - `raddr`: Beginning address of remote side to read. Physical address in kernel mode
    /// - `rkey`: Remote memory region key
    #[inline]
    pub fn post_send_read(
        &self,
        mr: &MemoryRegion,
        range: Range<u64>,
        signaled: bool,
        raddr: u64,
        rkey: u32,
    ) -> Result<(), DatapathError> {
        if self.mode != QPType::RC {
            return Err(DatapathError::QPTypeError);
        }
        let send_flag: i32 = if signaled {
            ib_send_flags::IB_SEND_SIGNALED
        } else {
            0
        };
        self.post_send_inner(
            ib_wr_opcode::IB_WR_RDMA_READ,
            unsafe { mr.get_rdma_addr() } + range.start,
            raddr,
            mr.lkey().0,
            rkey,
            range.size() as u32,
            0,
            send_flag,
        )
    }

    /// Post a one-sided RDMA write work request to the send queue.
    ///
    /// Param:
    /// - `mr`: Reference to MemoryRegion to store written data from the remote side.
    /// - `range`: Specify which range of mr you want to store the written data. Index the mr in byte dimension.
    /// - `signaled`: Whether signaled while post send write
    /// - `raddr`: Beginning address of remote side to write. Physical address in kernel mode
    /// - `rkey`: Remote memory region key
    #[inline]
    pub fn post_send_write(
        &self,
        mr: &MemoryRegion,
        range: Range<u64>,
        signaled: bool,
        raddr: u64,
        rkey: u32,
    ) -> Result<(), DatapathError> {
        if self.mode != QPType::RC {
            return Err(DatapathError::QPTypeError);
        }
        let send_flag: i32 = if signaled {
            ib_send_flags::IB_SEND_SIGNALED
        } else {
            0
        };
        self.post_send_inner(
            ib_wr_opcode::IB_WR_RDMA_WRITE,
            unsafe { mr.get_rdma_addr() } + range.start,
            raddr,
            mr.lkey().0,
            rkey,
            range.size() as u32,
            0,
            send_flag,
        )
    }

    #[inline]
    fn post_send_inner(
        &self,
        op: u32,
        laddr: u64, // physical addr
        raddr: u64, // physical addr
        lkey: u32,
        rkey: u32,
        size: u32,      // size in bytes
        imm_data: u32,  // immediate data
        send_flag: i32, // send flags, see `ib_send_flags`
    ) -> Result<(), DatapathError> {
        let mut sge = ib_sge {
            addr: laddr,
            length: size,
            lkey,
        };
        let mut wr: ib_rdma_wr = Default::default();
        wr.wr.opcode = op;
        wr.wr.send_flags = send_flag;
        wr.wr.ex.imm_data = imm_data;
        wr.wr.num_sge = 1;
        wr.wr.sg_list = &mut sge as *mut _;
        wr.remote_addr = raddr;
        wr.rkey = rkey;
        let mut bad_wr: *mut ib_send_wr = null_mut();
        let err = unsafe {
            bd_ib_post_send(
                self.inner_qp.as_ptr(),
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
}

#[cfg(feature = "dct")]
impl QueuePair {     
    /// Really similar to RCQP
    /// except that we need to additinal pass an endpoint argument 
    #[inline]    
    pub fn post_send_dc_read(
        &self,
        endpoint: &DatagramEndpoint,
        mr: &MemoryRegion,
        range: Range<u64>,
        signaled: bool,
        raddr: u64,
        rkey: u32,
    ) -> Result<(), DatapathError> {
        if self.mode != QPType::DC {
            return Err(DatapathError::QPTypeError);
        }
        let send_flag: i32 = if signaled {
            ib_send_flags::IB_SEND_SIGNALED
        } else {
            0
        };
        self.post_send_dc_inner(
            ib_wr_opcode::IB_WR_RDMA_READ,
            endpoint,
            unsafe { mr.get_rdma_addr() } + range.start,
            raddr,
            mr.lkey().0,
            rkey,
            range.size() as u32,
            0,
            send_flag,
        )
    }    

    #[inline]
    pub fn post_send_dc_write(
        &self,
        endpoint: &DatagramEndpoint,
        mr: &MemoryRegion,
        range: Range<u64>,
        signaled: bool,
        raddr: u64,
        rkey: u32,
    ) -> Result<(), DatapathError> {
        if self.mode != QPType::DC {
            return Err(DatapathError::QPTypeError);
        }
        
        let send_flag: i32 = if signaled {
            ib_send_flags::IB_SEND_SIGNALED
        } else {
            0
        };

        self.post_send_dc_inner(
            ib_wr_opcode::IB_WR_RDMA_WRITE,
            endpoint,
            unsafe { mr.get_rdma_addr() } + range.start,
            raddr,
            mr.lkey().0,
            rkey,
            range.size() as u32,
            0,
            send_flag,
        )
    }
    
    #[inline]
    fn post_send_dc_inner(
        &self,
        op: u32,
        endpoint: &DatagramEndpoint,
        laddr: u64, // physical addr
        raddr: u64, // physical addr
        lkey: u32,
        rkey: u32,
        size: u32,      // size in bytes
        imm_data: u32,  // immediate data
        send_flag: i32, // send flags, see `ib_send_flags`
    ) -> Result<(), DatapathError> {

        let mut sge = ib_sge {
            addr: laddr,
            length: size,
            lkey,
        };

        let mut wr: ib_dc_wr = Default::default();
        wr.wr.opcode = op;
        wr.wr.send_flags = send_flag;
        wr.wr.ex.imm_data = imm_data;
        wr.wr.num_sge = 1;
        wr.wr.sg_list = &mut sge as *mut _;
        wr.remote_addr = raddr;
        wr.rkey = rkey;

        wr.ah = endpoint.raw_address_handler_ptr().as_ptr();
        wr.dct_access_key = endpoint.dc_key();
        wr.dct_number = endpoint.dct_num();

        let mut bad_wr: *mut ib_send_wr = null_mut();
        let err = unsafe {
            bd_ib_post_send(
                self.inner_qp.as_ptr(),
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
}