use rust_user_rdma::*;

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