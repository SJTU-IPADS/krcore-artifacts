use crate::ControlpathError;

use super::{CMCallbacker, CMError, CMWrapper};

use linux_kernel_module::Error;
use rust_kernel_rdma_base::*;

impl<C> CMWrapper<C>
where
    C: CMCallbacker,
{
    /// This call will generate a IB_CM_REP_RECEIVED at the remote end
    /// This is usually called when connection an RCQP.
    /// The assumption: the ib_cm_rep_param has been properly set
    pub fn send_reply<T: Sized>(
        &self,
        mut rep: ib_cm_rep_param,
        mut pri: T,
    ) -> Result<(), CMError> {
        // re-set the private data here
        rep.private_data = ((&mut pri) as *mut T).cast::<linux_kernel_module::c_types::c_void>();
        rep.private_data_len = core::mem::size_of::<T>() as u8;

        let err = unsafe { ib_send_cm_rep(self.raw_ptr(), &mut rep as *mut _) };
        if err != 0 {
            return Err(CMError::SendError("Send reply", Error::EAGAIN));
        }
        Ok(())
    }

    /// This call will generate a IB_CM_REQ_RECEIVED at the remote end
    /// The assumption: the ib_cm_req_param has been properly set    
    pub fn send_req<T: Sized>(
        &mut self,
        mut req: ib_cm_req_param,
        mut pri: T,
    ) -> Result<(), CMError> {
        unimplemented!();
    }

    /// This call will generate a IB_CM_DREQ_RECEIVED at the remote end
    /// This is usually called when an RCQP destroy itself.
    /// The assumption: the ib_cm_req_param has been properly set    
    pub fn send_dreq<T: Sized>(&mut self, mut pri: T) -> Result<(), CMError> {
        unimplemented!();
    }
}
