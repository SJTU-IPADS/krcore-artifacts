use rdma_shim::ffi::c_types::c_int;

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
    /// 
    pub fn post_datagram(
        &self,
        endpoint: &DatagramEndpoint,
        mr: &MemoryRegion,
        range: Range<u64>,
        wr_id: u64,
        signaled: bool,
    ) -> Result<(), DatapathError> {
        self.post_datagram_w_flags(
            endpoint,
            mr,
            range,
            wr_id,
            if signaled {
                rdma_shim::bindings::ibv_send_flags::IBV_SEND_SIGNALED
            } else {
                0
            } as _,
        )
    }

    #[inline]
    pub fn post_datagram_w_flags(
        &self,
        endpoint: &DatagramEndpoint,
        mr: &MemoryRegion,
        range: Range<u64>,
        wr_id: u64,
        flags: c_int,
    ) -> Result<(), DatapathError> {
        if self.mode != QPType::UD {
            return Err(DatapathError::QPTypeError);
        }
        let mut wr: ib_send_wr = Default::default();
        let mut bad_wr: *mut ib_send_wr = null_mut();

        // first setup the sge
        let mut sge = ib_sge {
            addr: unsafe { mr.get_rdma_addr() } + range.start,
            length: range.size() as u32,
            lkey: mr.lkey().0,
        };

        unsafe {
            wr.wr.ud.as_mut().remote_qpn = endpoint.qpn();
            wr.wr.ud.as_mut().remote_qkey = endpoint.qkey();
            wr.wr.ud.as_mut().ah = endpoint.raw_address_handler_ptr().as_ptr();
        };

        wr.wr_id = wr_id;
        wr.sg_list = &mut sge as _;
        wr.num_sge = 1;
        wr.send_flags = flags;
        wr.opcode = ibv_wr_opcode::IBV_WR_SEND;

        let err = unsafe {
            (self.post_send_op)(
                self.inner_qp.as_ptr(),
                &mut wr as *mut _,
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
