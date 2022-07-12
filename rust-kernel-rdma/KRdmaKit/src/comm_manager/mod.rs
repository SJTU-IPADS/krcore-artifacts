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

    #[error("Unknown error")]
    Unknown,
}

/// The CMCallback implements various task after receiving CM messages
/// Typically, a CM endpoint will only need to handle partial events
/// e.g., 
/// - Server: request events 
/// - Client: response events
/// Therefore, we provide a default implementation for these trait functions. 
pub trait CMCallbacker {
    fn handle_req(self: &mut Self, _reply_cm: CMReplyer, _event: &ib_cm_event)
        -> Result<(), CMError> { 
        Ok(())
    }

    fn handle_dreq(
        self: &mut Self,
        _reply_cm: CMReplyer,
        _event: &ib_cm_event,
    ) -> Result<(), CMError> {
        // the user can not implement this
        Ok(())
    }

    fn handle_sidr_req(
        self: &mut Self,
        _reply_cm: CMReplyer,
        _event: &ib_cm_event,
    ) -> Result<(), CMError> {
        // the user can not implement this
        Ok(())
    }

    fn handle_sidr_rep(
        self: &mut Self,
        _reply_cm: CMReplyer,
        _event: &ib_cm_event,
    ) -> Result<(), CMError> {
        // the user can not implement this
        Ok(())
    }
}

/// A wrapper over ib_cm_id
/// which provides the common functionalities of the CM
#[allow(dead_code)]
#[derive(Debug)]
pub struct CMWrapper<T: CMCallbacker> {
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
    ) -> Option<Self> {
        Some(Self {
            _dev: dev.clone(),
            inner: NonNull::new(cm_ptr)?,
            callbacker: context.clone(),
        })
    }

    pub(crate) unsafe fn raw_ptr(&self) -> &NonNull<ib_cm_id> {
        &self.inner
    }

    pub(crate) fn callbacker(&self) -> &Arc<T>{
        &self.callbacker
    }

    /// Get the current status of the CM
    /// FIXME: should be more friendly to print
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
    // let mut ctx: Arc<T> = Arc::from_raw(cm.get_context());
    let ctx: &mut T = &mut (*cm.get_context());

    let res = match event.event {
        ib_cm_event_type::IB_CM_REQ_RECEIVED => ctx.handle_req(cm, &event),
        ib_cm_event_type::IB_CM_SIDR_REQ_RECEIVED => ctx.handle_sidr_req(cm, &event),
        ib_cm_event_type::IB_CM_SIDR_REP_RECEIVED => ctx.handle_sidr_rep(cm, &event),
        _ => Err(CMError::CallbackError(event.event)),
    };

    if res.is_err() {
        log::error!("{:?}", res);
        return -1;
    }
    return 0;
}
