use core::ptr::NonNull;
use rust_kernel_rdma_base::*;
use linux_kernel_module::Error;

use super::{Arc, CMCallbacker, CMError, CMWrapper, DeviceRef};

/// CM sender is used at the client to initiate
/// communication with the server
/// The callback needs to handle the server reply
#[derive(Debug)]
pub struct CMSender<T: CMCallbacker> {
    inner: CMWrapper<T>,
}

impl<C> CMSender<C>
where
    C: CMCallbacker,
{
    pub fn new(context: &Arc<C>, dev: &DeviceRef) -> Result<Self, CMError> {
        let raw_cm = unsafe { super::create_raw_cm_id(dev.raw_ptr().as_ptr(), context)? };
        Ok(Self {
            inner: CMWrapper::new(dev, context, raw_cm).unwrap(),
        })
    }

    pub fn send_sidr<T: Sized>(
        &mut self,
        req: ib_cm_sidr_req_param,
        pri: T,
    ) -> Result<(), CMError> {
        self.inner.send_sidr(req, pri)
    }

    pub fn send_dreq<T: Sized>(&mut self, pri: T) -> Result<(), CMError> {
        self.inner.send_dreq(pri)
    }

    pub fn callbacker(&self) -> &Arc<C> {
        self.inner.callbacker()
    }
}

impl<T> Drop for CMSender<T>
where
    T: CMCallbacker,
{
    fn drop(&mut self) {
        unsafe { ib_destroy_cm_id(self.inner.raw_ptr().as_ptr()) };
    }
}

/// Unlike CMSender and CMServer,
/// CMReplyer are dynamically constructed by the underlying CM.
/// Thus, it only servers a temporal wrapper for ib_cm_id
pub struct CMReplyer {
    inner: NonNull<ib_cm_id>,
}

impl CMReplyer {
    /// The CMServer will dynamically construct the CMReplyer.
    /// So we limit the scope of it only to the module
    ///
    /// # Note
    /// - We don't need to free the ib_cm_id, since
    /// the CM library will be responsible to free a CMReplyer
    pub(super) unsafe fn new(inner: *mut ib_cm_id) -> Self {
        Self {
            inner: NonNull::new_unchecked(inner),
        }
    }

    /// Return the context binded to the CMServer.
    /// The context should be determined by the programmer.
    /// Since it is unsafe to expose this API to the user,
    /// we limit the scope of this function to this module
    pub(super) unsafe fn get_context<T>(&self) -> *mut T {
        self.inner.as_ref().context as _
    }

    /// This call will generate IB_CM_SIDR_REP_RECEIVED at the remote end
    /// The user can freely call it
    pub fn send_sidr_reply<T: Sized>(
        &mut self,
        mut rep: ib_cm_sidr_rep_param,
        mut info: T,
    ) -> Result<(), CMError> {
        rep.info = ((&mut info) as *mut T).cast::<linux_kernel_module::c_types::c_void>();
        rep.info_length = core::mem::size_of::<T>() as u8;

        let err = unsafe { ib_send_cm_sidr_rep(self.inner.as_ptr(), &mut rep as *mut _) };
        if err != 0 {
            return Err(CMError::SendError(
                "Send SIDR reply",
                Error::from_kernel_errno(err),
            ));
        }
        Ok(())
    }
}
