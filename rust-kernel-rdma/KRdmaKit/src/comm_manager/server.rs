use alloc::sync::Arc;

use super::{CMCallbacker, CMError, CMWrapper, DeviceRef};
use rust_kernel_rdma_base::*;

/// The CM server is analogy to the UDP server,
/// which listens on a port (listen_id).
pub struct CMServer<T: CMCallbacker> {
    inner: CMWrapper<T>,
    listen_id: u64,
}

impl<T> CMServer<T>
where
    T: CMCallbacker,
{
    /// CMServer is created by:
    /// 1. create a CMWrapper
    /// 2. listen this CMServer on `listen_id`
    pub fn new(listen_id: u64, context: &Arc<T>, dev: &DeviceRef) -> Result<Self, CMError> {
        let raw_cm = unsafe { super::create_raw_cm_id(dev.raw_ptr(), context)? };
        // step #1.
        let res = Self {
            inner: CMWrapper::new(dev, context, raw_cm).unwrap(), // cannot fail
            listen_id: listen_id,
        };

        // step #2.
        let err = unsafe { ib_cm_listen(raw_cm, listen_id, 0) };
        if err != 0 {
            return Err(CMError::Creation(err));
        }
        Ok(res)
    }

    pub fn listen_id(&self) -> u64 {
        self.listen_id
    }
}
