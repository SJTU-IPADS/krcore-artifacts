#[cfg(feature = "kernel")]
use rdma_shim::KernelResult;
#[cfg(feature = "kernel")]
use rdma_shim::Error;

use rdma_shim::bindings::*;

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
    pub fn open_context(self: &DeviceRef) -> Result<Arc<Context>, crate::ControlpathError> {
        Context::new(self)
    }

    pub fn name(&self) -> alloc::string::String {
        crate::utils::convert_c_str_to_string(&unsafe { self.inner.as_ref().name })
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
    pub(crate) fn raw_ptr(&self) -> &NonNull<ib_device> {
        &self.inner
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

    #[cfg(feature = "kernel")]
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

    #[cfg(feature = "kernel")]
    /// get the default port attr
    #[inline]
    pub fn get_port_attr(&self, port_id: u8) -> KernelResult<ib_port_attr> {
        let mut port_attr: ib_port_attr = Default::default();
        let err = unsafe {
            ib_query_port(
                self.raw_ptr().as_ptr(),
                port_id as u8,
                &mut port_attr as *mut _,
            )
        };
        if err != 0 {
            return Err(Error::from_kernel_errno(err));
        }
        Ok(port_attr)
    }

    #[cfg(feature = "kernel")]
    /// check whether a given port is activate or not
    pub fn port_status(&self, port_id: u8) -> KernelResult<ib_port_state::Type> {
        Ok(self.get_port_attr(port_id)?.state)
    }

    #[cfg(feature = "kernel")]
    /// query the gid of a specific port
    pub fn query_gid(&self, port_id: u8, gid_idx : usize) -> KernelResult<ib_gid> {
        let mut gid: ib_gid = Default::default();
        let err = unsafe {
            self.get_mut_self().query_gid(
                self.inner.as_ptr(),
                port_id as u8,
                gid_idx as _, // index
                &mut gid as *mut ib_gid,
            )
        };
        if err != 0 {
            return Err(Error::from_kernel_errno(err));
        }
        Ok(gid)
    }

    #[cfg(feature = "kernel")]
    /// find the first valid port given a range
    pub fn first_valid_port(&self, range: core::ops::Range<u8>) -> core::option::Option<u8> {
        for i in range {
            if let Ok(status) = self.port_status(i as _) {
                if status != ib_port_state::IB_PORT_ACTIVE {
                    continue;
                }
                if let Ok(_gid) = self.query_gid(i as _, 0) {
                    return Some(i);
                }
            }
        }
        None
    }
}

#[cfg(feature = "user")]
#[cfg(test)]
mod tests {
    use super::*;
    use rdma_shim::log;

    #[test]
    fn test_device_name() {
        let udriver = crate::UDriver::create().unwrap();
        let dev_ref = udriver.get_dev(0).unwrap();
        log::error!("{}", dev_ref.name());

        for i in 0..udriver.iter().len() { 
            let dev = udriver.get_dev(i).unwrap();            
            dev.open_context().unwrap();
        }
    }
}