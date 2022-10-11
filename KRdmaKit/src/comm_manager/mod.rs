use rdma_shim::{log, ffi::c_types};
use rdma_shim::bindings::*;

use alloc::string::String;
use alloc::sync::{Arc, Weak};

use core::ptr::NonNull;

use crate::device::DeviceRef;

pub use client::CMReplyer;
pub use client::CMSender;
pub use explorer::Explorer;
pub use server::CMServer;

pub use crate::CMError;

mod client;
/// implementations of the send methods of the CM
mod send;

/// server-side CM implementation
mod server;

/// implementation fo explore the `sa_path_rec`
mod explorer;

/// The CMCallback implements various task after receiving CM messages
/// Typically, a CM endpoint will only need to handle partial events
/// e.g.,
/// - Server: request events
/// - Client: response events
/// Therefore, we provide a default implementation for these trait functions.
pub trait CMCallbacker {
    fn handle_req(
        self: &mut Self,
        _reply_cm: CMReplyer,
        _event: &ib_cm_event,
    ) -> Result<(), CMError> {
        Ok(())
    }

    fn handle_rep(
        self: &mut Self,
        _reply_cm: CMReplyer,
        _event: &ib_cm_event,
    ) -> Result<(), CMError> {
        Ok(())
    }

    fn handle_rtu(
        self: &mut Self,
        _reply_cm: CMReplyer,
        _event: &ib_cm_event,
    ) -> Result<(), CMError> {
        Ok(())
    }

    fn handle_rej(
        self: &mut Self,
        _reply_cm: CMReplyer,
        _event: &ib_cm_event,
    ) -> Result<(), CMError> {
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
    callbacker: Weak<T>,
}

impl<T> CMWrapper<T>
where
    T: CMCallbacker,
{
    #[inline]
    pub(crate) fn new(dev: &DeviceRef, context: &Arc<T>, cm_ptr: *mut ib_cm_id) -> Option<Self> {
        Some(Self {
            _dev: dev.clone(),
            inner: NonNull::new(cm_ptr)?,
            callbacker: Arc::downgrade(context),
        })
    }

    #[inline]
    pub fn new_from_callbacker(dev: &DeviceRef, callbacker: &Arc<T>) -> Result<Self, CMError> {
        let cm_id = unsafe { create_raw_cm_id(dev.raw_ptr().as_ptr(), callbacker)? };
        let inner = NonNull::new(cm_id).ok_or(CMError::Creation(0))?;
        Ok(Self {
            _dev: dev.clone(),
            inner,
            callbacker: Arc::downgrade(callbacker),
        })
    }

    #[inline]
    pub unsafe fn raw_ptr(&self) -> &NonNull<ib_cm_id> {
        &self.inner
    }

    #[inline]
    pub(crate) fn callbacker(&self) -> Option<Arc<T>> {
        self.callbacker.upgrade()
    }

    /// Get the current status of the CM
    /// FIXME: should be more friendly to print
    pub fn status(&self) -> ib_cm_state::Type {
        unsafe { self.inner.as_ref() }.state
    }

    /// Whether the cm id has already been destroyed
    /// Note: a non-zero return value from the user-defined cm handler will
    /// cause the cm id to be destroyed.
    /// We will set the context pointer to null before returning non-zero value
    /// in the cm handler as a sign for a destroyed cm id.
    pub fn cm_is_destroyed(&self) -> bool {
        unsafe {
            self.inner.as_ref()
        }.context.is_null()
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
    match (cm_id.as_mut().unwrap().context as *mut T).as_mut() {
        Some(ctx) => {
            // if `context` is a non-null pointer, we can safely call the handler functions here
            let res = match event.event {
                ib_cm_event_type::IB_CM_REQ_RECEIVED => {
                    log::debug!("Handle REQ. CM addr 0x{:X}", cm_id as u64);
                    ctx.handle_req(cm, &event)
                }
                ib_cm_event_type::IB_CM_REP_RECEIVED => {
                    log::debug!("Handle REP. CM addr 0x{:X}", cm_id as u64);
                    ctx.handle_rep(cm, &event)
                }
                ib_cm_event_type::IB_CM_REJ_RECEIVED => {
                    log::debug!("Handle REJ. CM addr 0x{:X}", cm_id as u64);
                    ctx.handle_rej(cm, &event)
                }
                ib_cm_event_type::IB_CM_RTU_RECEIVED => {
                    log::debug!("Handle RTU. CM addr 0x{:X}", cm_id as u64);
                    ctx.handle_rtu(cm, &event)
                }
                ib_cm_event_type::IB_CM_DREQ_RECEIVED => {
                    log::debug!("Handle DREQ. CM addr 0x{:X}", cm_id as u64);
                    ctx.handle_dreq(cm, &event)
                }
                ib_cm_event_type::IB_CM_SIDR_REQ_RECEIVED => ctx.handle_sidr_req(cm, &event),
                ib_cm_event_type::IB_CM_SIDR_REP_RECEIVED => ctx.handle_sidr_rep(cm, &event),
                _ => Err(CMError::CallbackError(event.event)),
            };

            if res.is_err() {
                log::error!("{:?} 0x{:X}", res, cm_id as u64);
                // a non-zero return value from this handler will cause the cm id to be destroyed
                // we will set the `context` to null as a sign for this
                cm_id.as_mut().unwrap().context = 0 as *mut _;
                return -1;
            }
            return 0;
        },
        None => {
            return 0;
        },
    }
}
