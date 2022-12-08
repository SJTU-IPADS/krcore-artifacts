use crate::context::Context;
use crate::QueuePair;
#[allow(unused_imports)]
use rdma_shim::bindings::*;
use std::ptr::NonNull;
use std::sync::Arc;

/// When allocating a MemoryWindow, it is bound to a MR but not bound to a QP.
/// To bind a MW to a specific QP, you should use `post_bind_mw` method in QP.
#[allow(dead_code)]
pub struct MemoryWindow {
    ctx: Arc<Context>,
    mw: NonNull<ibv_mw>,
}

unsafe impl Send for MemoryWindow {}
unsafe impl Sync for MemoryWindow {}

impl MemoryWindow {
    /// Create a TYPE_2 MW, unbound
    pub fn new(context: Arc<Context>) -> Result<Self, crate::ControlpathError> {
        let mw = NonNull::new(unsafe {
            ibv_alloc_mw(context.get_pd().as_ptr(), ibv_mw_type::IBV_MW_TYPE_2)
        })
        .ok_or(crate::ControlpathError::CreationError(
            "Failed to create MR",
            rdma_shim::Error::EFAULT,
        ))?;

        Ok(Self {
            ctx: context,
            mw,
            bound_qp: None,
        })
    }

    #[inline]
    pub fn inner(&self) -> &NonNull<ibv_mw> {
        &self.mw
    }
}

impl Drop for MemoryWindow {
    fn drop(&mut self) {
        let errno = unsafe { ibv_dealloc_mw(self.mw.as_ptr()) };
        if errno != 0 {
            eprintln!("Dealloc MW error : {}", errno)
        }
    }
}
