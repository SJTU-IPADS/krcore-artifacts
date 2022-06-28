use rust_kernel_rdma_base::*;

use alloc::sync::Arc;
use core::{fmt::Write, ptr::NonNull};

use crate::device_v1::DeviceRef;

pub use client::{CMReplyer, CMSender};
pub use server::CMServer;

mod client;
mod send;
mod server;

/// The error type of CM.
#[derive(thiserror_no_std::Error, Debug)]
pub enum CMError {
    #[error("Timeout")]
    Timeout,

    #[error("CM send error")]
    SendError(&'static str, linux_kernel_module::Error),

    #[error("Create server error")]
    ServerError(&'static str, linux_kernel_module::Error),
}

pub trait CMCallbacker {
    fn handle_req(self: Arc<Self>, reply_cm: CMReplyer, event: &ib_cm_event)
        -> Result<(), CMError>;
}

/// A wrapper over ib_cm_id
/// which provides the common functionalities of the
/// client-server sides CM
struct CMWrapper<T: CMCallbacker> {
    _dev: DeviceRef, // prevent usage error
    inner: NonNull<ib_cm_id>,
    callbacker: Arc<T>,
}

impl<T> CMWrapper<T>
where
    T: CMCallbacker,
{
    #[inline]
    pub(crate) fn new(
        dev: &DeviceRef,
        context: &Arc<T>,
        cm_ptr: *mut ib_cm_id,
    ) -> core::option::Option<Self> {
        Some(Self {
            _dev: dev.clone(),
            inner: NonNull::new(cm_ptr)?,
            callbacker: context.clone(),
        })
    }

    pub(crate) unsafe fn raw_ptr(&self) -> *mut ib_cm_id {
        self.inner.as_ptr()
    }

    /// Get the current status of the CM
    pub fn status(&self) -> ib_cm_state::Type {
        unsafe { self.inner.as_ref() }.state
    }
}

pub unsafe extern "C" fn cm_handler() {
    unimplemented!();
}
