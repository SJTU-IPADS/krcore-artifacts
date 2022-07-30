use rdma_shim::bindings::*;
use rdma_shim::{Error, log, println};

use crate::comm_manager::{CMCallbacker, CMError, CMReplyer};
use crate::queue_pairs::{QPType, QueuePair, QueuePairStatus};
use crate::services::rc::RCConnectionData;
use crate::ControlpathError;

impl CMCallbacker for QueuePair {
    /// Reply handler for rc communication
    fn handle_rep(&mut self, reply_cm: CMReplyer, event: &ib_cm_event) -> Result<(), CMError> {
        let data = unsafe { *(event.private_data as *mut RCConnectionData) };
        let rep: &ib_cm_rep_event_param = &unsafe { event.param.rep_rcvd };
        let _ = self
            .bring_up_rc_inner(data.lid as u32, data.gid, rep.remote_qpn, rep.starting_psn)
            .map_err(|_| CMError::Creation(0))?;

        self.rc_comm.as_mut().unwrap().done();
        let _ = reply_cm.send_rtu(0);
        log::debug!("In rep_handler, send rtu OK");
        Ok(())
    }

    /// Reject handler for rc communication
    fn handle_rej(
        self: &mut Self,
        _reply_cm: CMReplyer,
        event: &ib_cm_event,
    ) -> Result<(), CMError> {
        let errno = unsafe { *(event.private_data as *mut u64) };
        self.rc_comm.as_mut().unwrap().done();
        log::debug!("In rej_handler, set rcstatus ERROR, errno {}", errno);
        Ok(())
    }

    /// De-registration request handler for rc communication
    fn handle_dreq(
        self: &mut Self,
        _reply_cm: CMReplyer,
        _event: &ib_cm_event,
    ) -> Result<(), CMError> {
        log::debug!("RC qp handle dreq");
        Ok(())
    }
}

impl QueuePair {
    /// Bring ip rc inner method, used in PreparedQueuePair and RC Server.
    ///
    /// See `bring_up_rc` in PreparedQueuePair for more details about parameters.
    pub(crate) fn bring_up_rc_inner(
        &mut self,
        lid: u32,
        gid: ib_gid,
        remote_qpn: u32,
        psn: u32,
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
                rq_psn: psn,
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
