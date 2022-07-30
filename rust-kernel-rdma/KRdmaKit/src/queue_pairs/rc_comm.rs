/// This module must be used in the kernel
use rdma_shim::bindings::*;
use rdma_shim::utils::completion;

use alloc::boxed::Box;
use alloc::sync::Arc;

use crate::comm_manager::{CMCallbacker, CMError, CMWrapper};
use crate::device::DeviceRef;

/// RCComm is used to abstract the handshake with the server.
/// Since the linux `completion` may contain self-reference pointers,
/// we need to create this struct on the heap
/// so it has to be allocated on heap.
pub struct RCCommStruct<T: CMCallbacker> {
    done: completion,
    cm: CMWrapper<T>,
}

impl<T: CMCallbacker> RCCommStruct<T> {
    pub fn new(dev: &DeviceRef, cm_ctx: &Arc<T>) -> Result<Box<Self>, CMError> {
        let cm = CMWrapper::new_from_callbacker(dev, cm_ctx)?;
        let mut rc_struct = Box::new(Self {
            done: Default::default(),
            cm,
        });
        rc_struct.done.init();
        Ok(rc_struct)
    }

    #[inline]
    pub fn wait(
        &mut self,
        timeout_msecs: i32,
    ) -> rdma_shim::kernel::linux_kernel_module::KernelResult<()> {
        self.done.wait(timeout_msecs)
    }

    #[inline]
    pub fn done(&mut self) {
        self.done.done();
    }

    #[allow(dead_code)]
    #[inline]
    pub fn send_rep<S: Sized>(&self, rep: ib_cm_rep_param, pri: S) -> Result<(), CMError> {
        self.cm.send_reply(rep, pri)
    }

    #[inline]
    pub fn send_req<S: Sized>(&self, req: ib_cm_req_param, pri: S) -> Result<(), CMError> {
        self.cm.send_req(req, pri)
    }

    /// We should not let it implement `Drop` trait because the inner `ib_cm_id`'s lifetime was bound
    /// to the inner `ib_cm_id`'s context. But the context's lifetime is unknown and we must not
    /// destroy the inner `ib_cm_id` before the context is destroyed. So the outer struct controlled
    /// by user must manually call the explicit drop method when the `ib_cm_id` should be destroyed.
    pub unsafe fn explicit_drop(&mut self) {
        ib_destroy_cm_id(self.cm.raw_ptr().as_ptr());
    }
}
