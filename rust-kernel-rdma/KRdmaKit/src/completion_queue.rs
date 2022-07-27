use rust_kernel_rdma_base::*;
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
        let mut cq_attr = ib_cq_init_attr {
            cqe: max_cq_entries,
            ..Default::default()
        };

        let cq_raw_ptr: *mut ib_cq = unsafe {
            ib_create_cq(
                context.get_dev_ref().raw_ptr().as_ptr(),
                None, // comp_handler not supported yet
                None,
                null_mut(),
                &mut cq_attr as _,
            )
        };

        return if cq_raw_ptr.is_null() {
            Err(CreationError("CQ", linux_kernel_module::Error::EINVAL))
        } else {
            Ok(Self {
                _ctx: context.clone(),
                cq: unsafe { NonNull::new_unchecked(cq_raw_ptr) },
            })
        };
    }

    /// Poll multiple completions from the CQ
    /// This call taks &self, because the underlying ib_poll_cq
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
            Err(DatapathError::PollCQError(
                linux_kernel_module::Error::EINVAL,
            ))
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
    pub fn create(context: &ContextRef, max_wr: u32, max_sge : u32) -> Result<Self, ControlpathError> {

        let mut srq_attr : ib_srq_init_attr = Default::default();
        srq_attr.attr.max_wr = max_wr;
        srq_attr.attr.max_sge = max_sge;

        let raw_ptr: *mut ib_srq = unsafe {
            ib_create_srq(
                context.get_pd().as_ptr(),
                &mut srq_attr as _,
            )
        };

        return if raw_ptr.is_null() {
            Err(CreationError("SRQ", linux_kernel_module::Error::EINVAL))
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