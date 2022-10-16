use crate::ControlpathError;


#[cfg(feature = "kernel")]
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

#[cfg(feature = "kernel")]
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

#[cfg(all(feature = "kernel", feature = "dct"))]
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

#[cfg(feature = "kernel")]
impl QueuePair {
    /// Bring ip rc inner method, used in PreparedQueuePair and RC Server.
    ///
    /// See `bring_up_rc` in PreparedQueuePair for more details about parameters.
    pub(crate) fn bring_up_rc_inner(
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
                qp_access_flags: self.access as i32,
                ..Default::default()
            };
            let ret =
                unsafe { ib_modify_qp(self.inner_qp.as_ptr(), &mut init_attr as *mut _, mask) };
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
            let mut ah_attr = rdma_ah_attr {
                type_: rdma_ah_attr_type::RDMA_AH_ATTR_TYPE_IB,
                sl: 0,
                port_num: self.port_num,
                ..Default::default()
            };
            unsafe { bd_rdma_ah_set_dlid(&mut ah_attr, lid) };
            ah_attr.grh.sgid_index = 0;
            ah_attr.grh.flow_label = 0;
            ah_attr.grh.hop_limit = 255;
            unsafe {
                ah_attr.grh.dgid.global.subnet_prefix = gid.global.subnet_prefix;
                ah_attr.grh.dgid.global.interface_id = gid.global.interface_id;
            }
            let mut rtr_attr = ib_qp_attr {
                qp_state: ib_qp_state::IB_QPS_RTR,
                path_mtu: self.path_mtu,
                dest_qp_num: remote_qpn,
                rq_psn: rq_psn,
                max_dest_rd_atomic: self.max_rd_atomic,
                min_rnr_timer: self.min_rnr_timer,
                ah_attr,
                ..Default::default()
            };
            let ret =
                unsafe { ib_modify_qp(self.inner_qp.as_ptr(), &mut rtr_attr as *mut _, mask) };
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
                unsafe { ib_modify_qp(self.inner_qp.as_ptr(), &mut rts_attr as *mut _, mask) };
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