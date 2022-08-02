use rdma_shim::bindings::*;
use rdma_shim::Error;

use alloc::sync::Arc;
use core::ptr::{null_mut, NonNull};

use crate::context::{Context, ContextRef};
use crate::ControlpathError::CreationError;
use crate::{ControlpathError, DatapathError};

/// An abstraction completion queue (CQ)
#[derive(Debug)]
pub struct CompletionQueue {
    _ctx: Arc<Context>,
    cq: NonNull<ib_cq>,
}

/// An abstraction shared receive queue (SRQ)
#[derive(Debug)]
pub struct SharedReceiveQueue {
    _ctx: Arc<Context>,
    srq: NonNull<ib_srq>,
}

impl CompletionQueue {
    /// Currently, we don't need complex CQ handler as the callback
    /// Thus, we simplify the creation process.
    ///
    /// `max_cq_entries` is the maximum size of the completion queue
    ///
    /// # Errors:
    /// - `CreationError` : This error meaning there is something wrong
    /// when creating completion queue with the given context and arguments.
    /// Check them carefully if they are valid and legal.
    ///
    pub fn create(context: &ContextRef, max_cq_entries: u32) -> Result<Self, ControlpathError> {
        #[cfg(feature = "kernel")]
        let cq_raw_ptr = {
            let mut cq_attr = ib_cq_init_attr {
                cqe: max_cq_entries,
                ..Default::default()
            };

            unsafe {
                ib_create_cq(
                    context.get_dev_ref().raw_ptr().as_ptr(),
                    None, // comp_handler not supported yet
                    None,
                    null_mut(),
                    &mut cq_attr as _,
                )
            }
        };

        // FIXME: the user-space CQ creation needs more values to be set
        #[cfg(feature = "user")]
        let cq_raw_ptr = {
            unsafe {
                ibv_create_cq(
                    context.raw_ptr().as_ptr(),
                    max_cq_entries,
                    core::null_mut(),
                    core::null_mut(),
                    0,
                )
            }
        };

        return if cq_raw_ptr.is_null() {
            Err(CreationError("CQ", Error::EINVAL))
        } else { 
            Ok(Self {
                _ctx: context.clone(),
                cq: unsafe { NonNull::new_unchecked(cq_raw_ptr) },
            })
        };
    }

    /// Poll multiple completions from the CQ
    /// This call takes &self, because the underlying ib_poll_cq
    /// will ensure thread safety.
    #[inline]
    pub fn poll<'c>(&self, completions: &'c mut [ib_wc]) -> Result<&'c mut [ib_wc], DatapathError> {
        let n: i32 = unsafe {
            bd_ib_poll_cq(
                self.cq.as_ptr(),
                completions.len() as i32,
                completions.as_mut_ptr() as _,
            )
        };
        if n < 0 {
            Err(DatapathError::PollCQError(Error::EINVAL))
        } else {
            Ok(&mut completions[0..n as usize])
        }
    }

    pub fn get_ctx(&self) -> &Arc<Context> {
        // not clone, as the clone takes time
        &self._ctx
    }

    pub fn raw_ptr(&self) -> &NonNull<ib_cq> {
        &self.cq
    }
}

impl SharedReceiveQueue {
    /// `max_cq_entries` is the maximum size of the completion queue
    ///
    /// # Errors:
    /// - `CreationError` : This error meaning there is something wrong
    /// when creating completion queue with the given context and arguments.
    /// Check them carefully if they are valid and legal.
    ///
    pub fn create(
        context: &ContextRef,
        max_wr: u32,
        max_sge: u32,
    ) -> Result<Self, ControlpathError> {
        let mut srq_attr: ib_srq_init_attr = Default::default();
        srq_attr.attr.max_wr = max_wr;
        srq_attr.attr.max_sge = max_sge;

        let raw_ptr: *mut ib_srq =
            unsafe { ib_create_srq(context.get_pd().as_ptr(), &mut srq_attr as _) };

        return if raw_ptr.is_null() {
            Err(CreationError("SRQ", rdma_shim::Error::EINVAL))
        } else {
            Ok(Self {
                _ctx: context.clone(),
                srq: unsafe { NonNull::new_unchecked(raw_ptr) },
            })
        };
    }

    pub fn raw_ptr(&self) -> &NonNull<ib_srq> {
        &self.srq
    }
}

impl Drop for CompletionQueue {
    fn drop(&mut self) {
        unsafe {
            ib_free_cq(self.cq.as_ptr());
        }
    }
}

impl Drop for SharedReceiveQueue {
    fn drop(&mut self) {
        unsafe {
            ib_destroy_srq(self.srq.as_ptr());
        }
    }
}

#[cfg(feature = "user")]
#[cfg(test)]
mod tests {
    #[test]
    fn cq_can_create() {
        let ctx = crate::UDriver::create()
            .expect("failed to query device")
            .devices()
            .into_iter()
            .next()
            .expect("no rdma device available")
            .open_context()
            .expect("failed to create RDMA context");
        unimplemented!();
    }
}
