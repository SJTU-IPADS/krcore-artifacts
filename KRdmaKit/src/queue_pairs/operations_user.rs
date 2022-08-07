use rdma_shim::ffi::c_types::c_int;
use crate::ControlpathError;

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

// reliable connection post send requests 
impl QueuePair {     

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
        let send_flag = if signaled {
            ibv_send_flags::IBV_SEND_SIGNALED
        } else {
            0
        };
        self.post_send_inner(
            ibv_wr_opcode::IBV_WR_RDMA_WRITE,
            unsafe { mr.get_rdma_addr() } + range.start,
            raddr,
            mr.lkey().0,
            rkey,
            range.size() as u32,
            0,
            send_flag as _,
        )
    }

    #[inline]
    fn post_send_inner(
        &self,
        op: u32,
        laddr: u64, // virtual addr
        raddr: u64, // virtual addr
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

        let mut wr: ibv_send_wr = Default::default();

        wr.opcode = op;
        wr.send_flags = send_flag;
        wr.imm_data = imm_data;
        wr.num_sge = 1;
        wr.sg_list = &mut sge as *mut _;

        // rdma related requests
        unsafe { 
            wr.wr.rdma.as_mut().remote_addr = raddr;
            wr.wr.rdma.as_mut().rkey = rkey;  
        };


        let mut bad_wr: *mut ib_send_wr = null_mut();

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

// reliable connection bringups
impl QueuePair {
    /// Bring ip rc inner method, used in PreparedQueuePair and RC Server.
    ///
    /// See `bring_up_rc` in PreparedQueuePair for more details about parameters.
    pub fn bring_up_rc_inner(
        &mut self,
        lid: u32,
        gid: ib_gid,
        remote_qpn: u32,
        rq_psn: u32,
    ) -> Result<(), ControlpathError> {
        if self.mode != QPType::RC {
            log::error!("Bring up rc inner, type check error");
            return Err(ControlpathError::CreationError(
                "bring up type check log::error!",
                Error::from_kernel_errno(0),
            ));
        }
        let qp_status = self.status()?;
        if qp_status == QueuePairStatus::Reset {
            // INIT
            let mask = ib_qp_attr_mask::IB_QP_STATE
                | ib_qp_attr_mask::IB_QP_PKEY_INDEX
                | ib_qp_attr_mask::IB_QP_PORT
                | ib_qp_attr_mask::IB_QP_ACCESS_FLAGS;
                
            let mut init_attr = ib_qp_attr {
                qp_state: ib_qp_state::IB_QPS_INIT,
                pkey_index: self.pkey_index,
                port_num: self.port_num,
                qp_access_flags: self.access as _,
                ..Default::default()
            };
            let ret =
                unsafe { ib_modify_qp(self.inner_qp.as_ptr(), &mut init_attr as *mut _, mask as _) };
            if ret != 0 {
                log::error!("Bring up rc inner, reset=>init error");
                return Err(ControlpathError::CreationError(
                    "bringRC INIT error",
                    Error::from_kernel_errno(ret),
                ));
            }

            // RTR
            let mask = ib_qp_attr_mask::IB_QP_STATE
                | ib_qp_attr_mask::IB_QP_AV
                | ib_qp_attr_mask::IB_QP_PATH_MTU
                | ib_qp_attr_mask::IB_QP_DEST_QPN
                | ib_qp_attr_mask::IB_QP_RQ_PSN
                | ib_qp_attr_mask::IB_QP_MAX_DEST_RD_ATOMIC
                | ib_qp_attr_mask::IB_QP_MIN_RNR_TIMER;

            let mut rtr_attr = ib_qp_attr {
                qp_state: ib_qp_state::IB_QPS_RTR,
                path_mtu: self.path_mtu,
                dest_qp_num: remote_qpn,
                rq_psn: rq_psn,
                max_dest_rd_atomic: self.max_rd_atomic,
                min_rnr_timer: self.min_rnr_timer,
                ah_attr : ibv_ah_attr { 
                    port_num : self.port_num, 
                    dlid : lid as _, 
                    is_global : 1,
                    grh : ibv_global_route { 
                        dgid : gid, 
                        hop_limit : 255, 
                        ..Default::default()
                    }, 
                    ..Default::default()
                }, 
                ..Default::default()
            };
            let ret =
                unsafe { ib_modify_qp(self.inner_qp.as_ptr(), &mut rtr_attr as *mut _, mask as _) };
            if ret != 0 {
                log::error!("Bring up rc inner, init=>rtr error");
                return Err(ControlpathError::CreationError(
                    "bring RC RTR error",
                    Error::from_kernel_errno(ret),
                ));
            }

            // RTS
            let mask = ib_qp_attr_mask::IB_QP_STATE
                | ib_qp_attr_mask::IB_QP_TIMEOUT
                | ib_qp_attr_mask::IB_QP_RETRY_CNT
                | ib_qp_attr_mask::IB_QP_RNR_RETRY
                | ib_qp_attr_mask::IB_QP_SQ_PSN
                | ib_qp_attr_mask::IB_QP_MAX_QP_RD_ATOMIC;
            let mut rts_attr = ib_qp_attr {
                qp_state: ib_qp_state::IB_QPS_RTS,
                timeout: self.timeout,
                retry_cnt: self.retry_count,
                rnr_retry: self.rnr_retry,
                sq_psn: self.qp_num(),
                max_rd_atomic: self.max_rd_atomic,
                ..Default::default()
            };
            let ret =
                unsafe { ib_modify_qp(self.inner_qp.as_ptr(), &mut rts_attr as *mut _, mask as _) };
            if ret != 0 {
                log::error!("Bring up rc inner, rtr=>rts error");
                return Err(ControlpathError::CreationError(
                    "bring RC RTS error",
                    Error::from_kernel_errno(ret),
                ));
            }
        }
        log::debug!("Bring up rc inner, return OK");
        Ok(())
    }
}