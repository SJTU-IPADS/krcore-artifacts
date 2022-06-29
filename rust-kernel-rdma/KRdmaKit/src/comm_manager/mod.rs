use linux_kernel_module::c_types;
use rust_kernel_rdma_base::*;

use alloc::string::String;
use alloc::sync::Arc;

use core::ptr::NonNull;

use crate::device_v1::DeviceRef;
use crate::log;

pub use client::CMReplyer;
pub use client::CMSender;
pub use explorer::Explorer;
pub use server::CMServer;

mod client;
/// implementations of the send methods of the CM
mod send;

/// server-side CM implementation
mod server;

/// implementation fo explore the `sa_path_rec`
mod explorer;

/// The error type of CM.
#[derive(thiserror_no_std::Error, Debug)]
pub enum CMError {
    #[error("Timeout")]
    Timeout,

    #[error("Creation error with errorno: {0}")]
    Creation(i32),

    #[error("Failed to handle callback: {0}")]
    CallbackError(u32),

    #[error("CM send error")]
    SendError(&'static str, linux_kernel_module::Error),

    #[error("Create server error")]
    ServerError(&'static str, linux_kernel_module::Error),

    #[error("Invalid arg on {0}: {1}")]
    InvalidArg(&'static str, String),
}

pub trait CMCallbacker {
    fn handle_req(self: Arc<Self>, reply_cm: CMReplyer, event: &ib_cm_event)
        -> Result<(), CMError>;

    fn handle_dreq(
        self: Arc<Self>,
        reply_cm: CMReplyer,
        event: &ib_cm_event,
    ) -> Result<(), CMError> {
        Ok(())
    }
}

/// A wrapper over ib_cm_id
/// which provides the common functionalities of the
/// client-server sides CM
#[derive(Debug)]
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

pub(super) unsafe fn create_raw_cm_id<T>(
    dev: *mut ib_device,
    context: &Arc<T>,
) -> Result<*mut ib_cm_id, CMError>
where
    T: CMCallbacker,
{
    let cm = ib_create_cm_id(dev, Some(cm_handler::<T>), Arc::as_ptr(context) as _);
    if !cm.is_null() {
        Ok(cm)
    } else {
        Err(CMError::Creation(0))
    }
}

pub unsafe extern "C" fn cm_handler<T>(
    cm_id: *mut ib_cm_id,
    env: *const ib_cm_event,
) -> c_types::c_int
where
    T: CMCallbacker,
{
    let event = *env;
    let cm = CMReplyer::new(cm_id);
    let ctx: Arc<T> = Arc::from_raw(cm.get_context());

    let res = match event.event {
        ib_cm_event_type::IB_CM_REQ_RECEIVED => ctx.handle_req(cm, &event),
        _ => Err(CMError::CallbackError(event.event)),
    };

    if res.is_err() {
        log::info!("{:?}", res);
        return -1;
    }
    return 0;
}
