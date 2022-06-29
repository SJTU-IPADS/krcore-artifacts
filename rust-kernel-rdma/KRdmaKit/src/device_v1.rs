use rust_kernel_rdma_base::linux_kernel_module::{self, KernelResult};
use rust_kernel_rdma_base::*;

use linux_kernel_module::Error;

use alloc::sync::Arc;
use core::option::Option;
use core::ptr::NonNull;

use crate::KDriverRef;

#[allow(missing_copy_implementations)] // This type can not copy
#[derive(Debug)]
pub struct Device {
    inner: NonNull<ib_device>,
    // We need to keep a driver reference to prevent
    // the driver being freed while the Device still alive
    _driver: KDriverRef,
}

pub type DeviceRef = Arc<Device>;

use crate::context::Context;

impl Device {
    pub fn open_context(self: &DeviceRef) -> Result<Context, crate::ControlpathError> {
        Context::new(self)
    }
}

impl Device {
    pub(crate) fn new(dev: *mut ib_device, driver: &KDriverRef) -> Option<DeviceRef> {
        Some(Arc::new(Self {
            inner: NonNull::new(dev)?,
            _driver: driver.clone(),
        }))
    }

    /// return the raw pointer of the device
    pub(crate) unsafe fn raw_ptr(&self) -> *mut ib_device {
        self.inner.as_ptr()
    }
}

impl Device {
    /// Wrap myself as a mutable reference
    /// This function should only be used for internal usage  
    unsafe fn get_mut_self(&self) -> &mut ib_device {
        // note that here we change the mutablity
        // it is safe to do so here, because the ib_driver will
        // use lock to protect the mutual accesses
        &mut *self.inner.as_ptr()
    }

    /// get device attr
    pub fn get_device_attr(&self) -> KernelResult<ib_device_attr> {
        let mut data: ib_udata = Default::default();
        let mut dev_attr: ib_device_attr = Default::default();

        let err = unsafe {
            self.get_mut_self().query_device(
                self.inner.as_ptr(),
                &mut dev_attr as *mut ib_device_attr,
                &mut data as *mut ib_udata,
            )
        };

        if err != 0 {
            return Err(Error::from_kernel_errno(err));
        }
        Ok(dev_attr)
    }

    /// get the default port attr
    #[inline]
    pub fn get_port_attr(&self, port_id: u8) -> KernelResult<ib_port_attr> {
        let mut port_attr: ib_port_attr = Default::default();
        let err = unsafe { ib_query_port(self.raw_ptr(), port_id as u8, &mut port_attr as *mut _) };
        if err != 0 {
            return Err(Error::from_kernel_errno(err));
        }
        Ok(port_attr)
    }

    /// check whether a given port is activate or not
    pub fn port_status(&self, port_id: u8) -> KernelResult<ib_port_state::Type> {
        Ok(self.get_port_attr(port_id)?.state)
    }

    /// query the gid of a specific port
    pub fn query_gid(&self, port_id: u8) -> KernelResult<ib_gid> {
        let mut gid: ib_gid = Default::default();
        let err = unsafe {
            self.get_mut_self().query_gid(
                self.inner.as_ptr(),
                port_id as u8,
                0,
                &mut gid as *mut ib_gid,
            )
        };
        if err != 0 {
            return Err(Error::from_kernel_errno(err));
        }
        Ok(gid)
    }

    /// find the first valid port given a range
    pub fn first_valid_port(&self, range: core::ops::Range<u8>) -> core::option::Option<u8> {
        for i in range {
            if let Ok(status) = self.port_status(i as _) {
                if status != ib_port_state::IB_PORT_ACTIVE {
                    continue;
                }
                if let Ok(_gid) = self.query_gid(i as _) {
                    return Some(i);
                }
            }
        }
        None
    }
}
