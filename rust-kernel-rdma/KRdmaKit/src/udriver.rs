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
    pub fn devices(&self) -> &Vec<crate::device::DeviceRef> {
        &self.rnics
    }

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

        let mut ret = Arc::new(Self {
            rnics: Default::default(),
            raw_devices: devices,
        });

        let mut rnics = Vec::new();
        for i in 0..ret.raw_devices.len() {
            rnics.push(crate::device::Device::new(ret.raw_devices[i], &ret)?);
        }

        // modify the ret temp again
        {
            let temp_inner = unsafe { Arc::get_mut_unchecked(&mut ret) };
            temp_inner.rnics = rnics;
        }

        Some(ret)
    }

    /// return the overall wrapped devices
    pub fn iter(&self) -> core::slice::Iter<'_, crate::device::DeviceRef> {
        self.rnics.iter()
    }

    /// get a specific device accroding to the index of the device list
    pub fn get_dev(&self, index: usize) -> Option<&crate::device::DeviceRef> {
        self.rnics.get(index)
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
            .field("num_devices", &self.rnics.len())
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
        let d = d.unwrap();
        assert_eq!(d.rnics.len(), d.raw_devices.len());
    }
}
