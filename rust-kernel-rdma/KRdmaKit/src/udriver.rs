// Credits: the device list part mainly follows: 
// https://github.com/jonhoo/rust-ibverbs/blob/master/ibverbs/src/lib.rs

use alloc::sync::Arc;

/// UDriver abstracts the RDMA device lists on this machine
pub struct UDriver {
}

pub type KDriverRef = Arc<KDriver>;

#[cfg(test)]
mod tests {
    #[test]
    fn has_rdma() {
        let mut n = 0i32;
        let devices = unsafe { rdma_shim::bindings::ibv_get_device_list(&mut n as *mut _) };
        
        // should have devices
        assert!(!devices.is_null());    
    }
}
