use rdma_shim::bindings::*;

#[allow(unused_imports)]
use rdma_shim::{log, Error};

use crate::comm_manager::{CMCallbacker, CMError, CMReplyer};
use crate::queue_pairs::QueuePair;
use crate::services::rc::RCConnectionData;

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
