use core::mem::size_of;
use core::ptr::{null_mut, NonNull};

use super::{Arc, CMCallbacker, CMError, CMWrapper, DeviceRef};
use linux_kernel_module::Error;
use rust_kernel_rdma_base::bindings::*;
use rust_kernel_rdma_base::*;

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

    pub fn callbacker(&self) -> Option<Arc<C>> {
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

    #[inline]
    pub fn raw_cm_id(&self) -> &NonNull<ib_cm_id> {
        &self.inner
    }

    /// This call will generate IB_CM_SIDR_REP_RECEIVED at the remote end
    /// The user can freely call it
    pub fn send_sidr_reply<T: Sized>(
        &mut self,
        mut rep: ib_cm_sidr_rep_param,
        mut info: T,
    ) -> Result<(), CMError> {
        rep.info = ((&mut info) as *mut T).cast::<linux_kernel_module::c_types::c_void>();
        rep.info_length = size_of::<T>() as u8;

        let err = unsafe { ib_send_cm_sidr_rep(self.inner.as_ptr(), &mut rep as *mut _) };
        if err != 0 {
            return Err(CMError::SendError(
                "Send SIDR reply",
                Error::from_kernel_errno(err),
            ));
        }
        Ok(())
    }

    pub fn send_reply<T: Sized>(
        &self,
        mut rep: ib_cm_rep_param,
        mut private_data: T,
    ) -> Result<(), CMError> {
        rep.private_data =
            ((&mut private_data) as *mut T).cast::<linux_kernel_module::c_types::c_void>();
        rep.private_data_len = size_of::<T>() as u8;
        let err = unsafe { ib_send_cm_rep(self.inner.as_ptr(), &mut rep as *mut _) };
        return if err != 0 {
            Err(CMError::SendError(
                "Send reply",
                Error::from_kernel_errno(err),
            ))
        } else {
            Ok(())
        };
    }

    pub fn send_reject<T: Sized>(&self, mut private_data: T) -> Result<(), CMError> {
        let err = unsafe {
            ib_send_cm_rej(
                self.inner.as_ptr(),
                0,
                null_mut(),
                0 as u8,
                ((&mut private_data) as *mut T).cast::<linux_kernel_module::c_types::c_void>(),
                size_of::<T>() as u8,
            )
        };
        return if err != 0 {
            Err(CMError::SendError(
                "Send reject",
                Error::from_kernel_errno(err),
            ))
        } else {
            Ok(())
        };
    }

    pub fn send_rtu<T: Sized>(&self, mut private_data: T) -> Result<(), CMError> {
        let inner_cm = self.inner.clone();
        let err = unsafe {
            ib_send_cm_rtu(
                inner_cm.as_ptr(),
                ((&mut private_data) as *mut T).cast::<linux_kernel_module::c_types::c_void>(),
                size_of::<T>() as u8,
            )
        };
        return if err != 0 {
            Err(CMError::SendError(
                "Send rtu",
                Error::from_kernel_errno(err),
            ))
        } else {
            Ok(())
        };
    }
}
