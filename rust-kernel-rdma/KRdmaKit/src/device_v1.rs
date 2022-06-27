use rust_kernel_rdma_base::linux_kernel_module::{self, KernelResult};
use rust_kernel_rdma_base::*;

use linux_kernel_module::Error;

use core::option::Option;
use core::ptr::NonNull;

#[allow(missing_copy_implementations)] // This type can not copy
#[repr(transparent)]
pub struct Device(NonNull<ib_device>);

impl Device {
    pub fn new(dev: *mut ib_device) -> Option<Self> {
        Some(Self(NonNull::new(dev)?))
    }

    /// return the raw pointer of the device
    unsafe fn raw_ptr(&self) -> *mut ib_device {
        self.0.as_ptr()
    }
}

impl Device {
    /// get device attr
    pub fn get_device_attr(&self) -> KernelResult<ib_device_attr> {
        let mut data: ib_udata = Default::default();
        let mut dev_attr: ib_device_attr = Default::default();

        // note that here we change the mutablity 
        // it is safe to do so here, because the ib_driver will
        // use lock to protect the mutual accesses
        let hca: &mut ib_device = unsafe { &mut *self.0.as_ptr() };
        let err = unsafe {
            hca.query_device(
                self.0.as_ptr(),
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
    pub fn get_port_attr(&self, port_id: usize) -> KernelResult<ib_port_attr> {
        let mut port_attr: ib_port_attr = Default::default();
        let err = unsafe { ib_query_port(self.raw_ptr(), port_id as u8, &mut port_attr as *mut _) };
        if err != 0 {
            return Err(Error::from_kernel_errno(err));
        }
        Ok(port_attr)
    }

    /// check whether a given port is activate or not
    pub fn port_status(&self, port_id: usize) -> KernelResult<ib_port_state::Type> {
        Ok(self.get_port_attr(port_id)?.state)
    }
}
