use super::{CMCallbacker, CMError, CMWrapper, DeviceRef};

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
    pub fn new(listen_id: u64, dev: &DeviceRef) -> Result<Self, CMError> {
        unimplemented!();
    }
}
