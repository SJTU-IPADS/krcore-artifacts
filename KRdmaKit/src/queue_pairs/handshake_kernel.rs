use crate::queue_pairs::rc_comm::RCCommStruct;
use crate::services::rc::RCConnectionData;
use crate::comm_manager::CMError;

#[cfg(feature = "kernel")]
impl PreparedQueuePair { 
    /// The method `handshake` serves for RC control path, establishing communication between client
    /// and server. After the handshake phase, client side RC qp's `rc_comm` field will be filled. This
    /// stores a C struct `ib_cm_id` responsible for exchanging communication message and keep the connection.
    ///
    /// Param:
    /// - `remote_service_id` : Predefined service id that you want to query
    /// - `sa_path_rec` : path resolved by `Explorer`, attention that the resolved path must be consistent with the remote_service_id
    ///
    pub fn handshake(
        self,
        remote_service_id: u64,
        mut path: sa_path_rec,
    ) -> Result<Arc<QueuePair>, ControlpathError> {
        // check qp type
        // must be rc and never initialized
        if self.inner.mode != QPType::RC {
            assert!(self.inner.rc_comm.is_some());
            log::error!("bring up type check error!");
            return Err(ControlpathError::CreationError(
                "QP mode that is not RC does not need handshake to bring it up!",
                Error::from_kernel_errno(0),
            ));
        }

        let mut rc_qp = Arc::new(self.inner);

        // 1. create the CM for handling the communication
        let rc_comm = RCCommStruct::new(rc_qp._ctx.get_dev_ref(), &rc_qp).map_err(|_| {
            ControlpathError::CreationError("ib_cm_id", Error::from_kernel_errno(0))
        })?;

        let rc_qp_ref = unsafe { Arc::get_mut_unchecked(&mut rc_qp) };
        rc_qp_ref.rc_comm = Some(rc_comm);

        // 2. construct the handshake data
        let data = RCConnectionData::new(rc_qp_ref).map_err(|err| {
            let errno = match err {
                CMError::Creation(errno) => errno,
                _ => 0,
            };
            ControlpathError::CreationError("RC queue pair", Error::from_kernel_errno(errno))
        })?;

        let req = ib_cm_req_param {
            primary_path: &mut path as *mut sa_path_rec,
            service_id: remote_service_id,
            qp_type: ib_qp_type::IB_QPT_RC,
            responder_resources: 16,
            initiator_depth: 16,
            remote_cm_response_timeout: 20,
            local_cm_response_timeout: 20,
            max_cm_retries: 15,
            rnr_retry_count: self.rnr_retry,
            retry_count: self.retry_count,
            flow_control: 1,
            qp_num: rc_qp_ref.qp_num(),
            starting_psn: rc_qp_ref.qp_num(),
            ..Default::default()
        };

        let rc_comm_ref = rc_qp_ref.rc_comm.as_mut().unwrap();

        let _ = rc_comm_ref.send_req(req, data).map_err(|_| {
            log::error!("RC qp send request error");
            ControlpathError::QueryError("send request error", Error::from_kernel_errno(0))
        })?;

        let _ = rc_comm_ref
            .wait(crate::CONNECT_TIME_OUT_MS)
            .map_err(|err: Error| ControlpathError::ContextError("wait completion error", err))?;
        if rc_qp_ref.status()? == QueuePairStatus::ReadyToSend {
            log::debug!("Handshake OK");
            Ok(rc_qp)
        } else {
            log::debug!("Handshake Error");
            Err(ControlpathError::CreationError(
                "qp status error",
                Error::from_kernel_errno(0),
            ))
        }
    }    
}