use alloc::sync::Arc;
use core::ptr::NonNull;
use rust_kernel_rdma_base::*;

use linux_kernel_module::Error;

use crate::{device::DeviceRef, ControlpathError};

/// Context abstract the protection domain and memory registeration
/// in the kernel space
#[derive(Debug)]
pub struct Context {
    inner_device: DeviceRef,
    // kernel has little support for MR
    // so a global KMR is sufficient
    kmr: NonNull<ib_mr>,

    pd: NonNull<ib_pd>,
}

pub type ContextRef = Arc<Context>;

#[derive(Debug)]
pub struct AddressHandler {
    _ctx: ContextRef,
    inner: NonNull<ib_ah>,
}

const IB_PD_UNSAFE_GLOBAL_RKEY: u32 = 0x01;

impl Context {
    pub fn new(dev: &DeviceRef) -> Result<ContextRef, ControlpathError> {
        let mr_flags = ib_access_flags::IB_ACCESS_LOCAL_WRITE
            | ib_access_flags::IB_ACCESS_REMOTE_READ
            | ib_access_flags::IB_ACCESS_REMOTE_WRITE
            | ib_access_flags::IB_ACCESS_REMOTE_ATOMIC;
        Self::new_from_flags(dev, mr_flags as i32)
    }

    pub fn new_from_flags(
        dev: &DeviceRef,
        mr_flags: i32,
    ) -> Result<ContextRef, ControlpathError> {
        // first allocate a PD
        let pd =
            NonNull::new(unsafe { ib_alloc_pd(dev.raw_ptr().as_ptr(), IB_PD_UNSAFE_GLOBAL_RKEY) })
                .ok_or(ControlpathError::ContextError("pd", Error::EAGAIN))?;

        // then MR
        let kmr = NonNull::new(unsafe { ib_get_dma_mr(pd.as_ptr(), mr_flags) })
            .ok_or(ControlpathError::ContextError("mr", Error::EAGAIN))?;

        Ok(Arc::new(Self {
            inner_device: dev.clone(),
            kmr,
            pd,
        }))
    }

    /// Get the lkey of this context
    pub fn lkey(&self) -> u32 {
        unsafe { self.kmr.as_ref().lkey }
    }

    /// Get the rkey of this context
    pub fn rkey(&self) -> u32 {
        unsafe { self.kmr.as_ref().rkey }
    }

    pub fn get_dev_ref(&self) -> &DeviceRef {
        &self.inner_device
    }

    pub fn get_pd(&self) -> &NonNull<ib_pd> {
        &self.pd
    }

    // TODO @Haotian : Just for test to use, remove it later
    pub fn get_kmr(&self) -> &NonNull<ib_mr> {
        &self.kmr
    }

    /// Create an address handler of this context
    pub fn create_address_handler(
        self: &Arc<Self>,
        port_num: u8,
        lid: u32,
        gid: ib_gid,
    ) -> Result<AddressHandler, ControlpathError> {
        let mut ah_attr: rdma_ah_attr = Default::default();
        ah_attr.type_ = rdma_ah_attr_type::RDMA_AH_ATTR_TYPE_IB;

        unsafe {
            bd_rdma_ah_set_dlid(&mut ah_attr, lid as u32);
        }

        ah_attr.sl = 0;
        ah_attr.port_num = port_num;

        unsafe {
            ah_attr.grh.dgid.global.subnet_prefix = gid.global.subnet_prefix;
            ah_attr.grh.dgid.global.interface_id = gid.global.interface_id;
        }

        let ptr = unsafe { rdma_create_ah_wrapper(self.get_pd().as_ptr(), &mut ah_attr as _) };

        if unsafe { ptr_is_err(ptr as _) } > 0 {
            Err(ControlpathError::CreationError(
                "AddressHandler",
                Error::EAGAIN,
            ))
        } else {
            Ok(AddressHandler {
                _ctx: self.clone(),
                // we have checked the pointer!
                inner: unsafe { NonNull::new_unchecked(ptr) },
            })
        }
    }
}

impl AddressHandler { 
    pub fn raw_ptr(&self) -> &NonNull<ib_ah> { 
        &self.inner
    }
}

impl Drop for AddressHandler {
    fn drop(&mut self) {
        unsafe {
            rdma_destroy_ah(self.inner.as_ptr());
        }
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
