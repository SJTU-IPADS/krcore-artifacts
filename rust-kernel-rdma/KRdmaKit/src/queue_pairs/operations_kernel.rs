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

        #[cfg(feature = "user")]
        let err = unsafe {
            (self.post_send_op)(
                self.inner_qp.as_ptr(),
                &mut wr.wr as *mut _,
                &mut bad_wr as *mut _,
            )
        };

        #[cfg(feature = "kernel")]
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

        #[cfg(feature = "user")]
        let err = unsafe {
            (self.post_send_op)(
                self.inner_qp.as_ptr(),
                &mut wr.wr as *mut _,
                &mut bad_wr as *mut _,
            )
        };

        #[cfg(feature = "kernel")]
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

        #[cfg(feature = "user")]
        let err = unsafe {
            (self.post_send_op)(
                self.inner_qp.as_ptr(),
                &mut wr.wr as *mut _,
                &mut bad_wr as *mut _,
            )
        };

        #[cfg(feature = "kernel")]
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
