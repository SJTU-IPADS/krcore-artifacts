use core::ptr::NonNull;
use rust_kernel_rdma_base::*;

/// CM sender is used at the client to initiate
/// communication with the server
pub struct CMSender {}

/// Unlike CMSender and CMServer,
/// CMReplyer are dynamically constructed by the underlying CM.
/// Thus, it only servers a temporal wrapper for ib_cm_id
pub struct CMReplyer {
    inner: NonNull<ib_cm_id>,
}

impl CMReplyer {
    pub(super) unsafe fn new(inner: *mut ib_cm_id) -> Self {
        Self {
            inner: NonNull::new_unchecked(inner),
        }
    }

    pub(super) unsafe fn get_context<T>(&self) -> *const T {
        self.inner.as_ref().context as _
    }
}
