// Credits: the device list part mainly follows:
// https://github.com/jonhoo/rust-ibverbs/blob/master/ibverbs/src/lib.rs

use alloc::sync::Arc;
use alloc::vec::Vec;

/// UDriver abstracts the RDMA device lists on this machine
pub struct UDriver {
    rnics: Vec<crate::device::DeviceRef>,
    raw_devices: &'static mut [*mut rdma_shim::bindings::ib_device],
}

pub type KDriverRef = Arc<UDriver>;

impl UDriver {
    /// Query the device lists on this machine, and recorded in the UDriver
    ///
    /// FIXME: replace Option with some kind of error
    ///
    pub fn create() -> Option<Arc<Self>> {
        let mut n = 0i32;
        let devices = unsafe { rdma_shim::bindings::ibv_get_device_list(&mut n as *mut _) };

        if devices.is_null() {
            return None;
        }

        let devices = unsafe {
            use core::slice;
            slice::from_raw_parts_mut(devices, n as usize)
        };

        Some(Arc::new(Self {
            rnics: Default::default(),
            raw_devices: devices,
        }))
    }
}

impl Drop for UDriver {
    fn drop(&mut self) {
        unsafe { rdma_shim::bindings::ibv_free_device_list(self.raw_devices.as_mut_ptr()) };
    }
}

impl core::fmt::Debug for UDriver {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        f.debug_struct("User-space RDMA device list")
            .field("num_devices", &1)
            .finish()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn has_rdma() {
        let mut n = 0i32;
        let devices = unsafe { rdma_shim::bindings::ibv_get_device_list(&mut n as *mut _) };

        // should have devices
        assert!(!devices.is_null());
    }

    #[test] 
    fn check_device_num() { 
        let d = UDriver::create();
        assert!(d.is_some());
    }
}
