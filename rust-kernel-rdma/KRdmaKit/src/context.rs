use core::ptr::NonNull;
use rust_kernel_rdma_base::*;

use linux_kernel_module::Error;

use crate::{device_v1::DeviceRef, ControlpathError};

#[derive(Debug)]
pub struct Context {
    inner_device: crate::device_v1::DeviceRef,
    // kernel has little support for MR
    // so a global KMR is sufficient
    kmr: NonNull<ib_mr>,

    pd: NonNull<ib_pd>,
}

const IB_PD_UNSAFE_GLOBAL_RKEY: u32 = 0x01;

impl Context {
    pub fn new(dev: &crate::device_v1::DeviceRef) -> Result<Self, ControlpathError> {
        let mr_flags = ib_access_flags::IB_ACCESS_LOCAL_WRITE
            | ib_access_flags::IB_ACCESS_REMOTE_READ
            | ib_access_flags::IB_ACCESS_REMOTE_WRITE
            | ib_access_flags::IB_ACCESS_REMOTE_ATOMIC;
        Self::new_from_flags(dev, mr_flags as i32)
    }

    pub fn new_from_flags(
        dev: &crate::device_v1::DeviceRef,
        mr_flags: i32,
    ) -> Result<Self, ControlpathError> {
        // first allocate a PD
        let pd = NonNull::new(unsafe { ib_alloc_pd(dev.raw_ptr(), IB_PD_UNSAFE_GLOBAL_RKEY) })
            .ok_or(ControlpathError::ContextError("pd", Error::EAGAIN))?;

        // then MR
        let kmr = NonNull::new(unsafe { ib_get_dma_mr(pd.as_ptr(), mr_flags) })
            .ok_or(ControlpathError::ContextError("mr", Error::EAGAIN))?;

        Ok(Self {
            inner_device: dev.clone(),
            kmr: kmr,
            pd: pd,
        })
    }

    pub fn get_dev_ref(&self) -> &DeviceRef {
        &self.inner_device
    }    
}

impl Drop for Context {
    fn drop(&mut self) {
        unsafe {
            ib_dereg_mr(self.kmr.as_ptr());
            ib_dealloc_pd(self.pd.as_ptr());
        }
    }
}
