#![allow(deref_nullptr)] // TODO(fxbug.dev/74605): Remove once bindgen is fixed.
#![allow(non_snake_case, non_camel_case_types, non_upper_case_globals)]

#[allow(unused_imports)]
use libc::*;

include!(concat!(env!("OUT_DIR"), "/bindings.rs"));

#[cfg(test)]
mod tests {
    #[test]
    fn has_rdma() {
        let mut n = 0i32;
        let devices = unsafe { super::ibv_get_device_list(&mut n as *mut _) };
        
        // should have devices
        assert!(!devices.is_null());    
    }
}
