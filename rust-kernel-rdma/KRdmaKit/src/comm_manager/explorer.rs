use no_std_net::Guid;
use rust_kernel_linux_util::bindings::completion;
use rust_kernel_rdma_base::*;

use core::option::Option;

use super::{Arc, CMError, DeviceRef,String};

/// # Assumption:
/// The path explorer will rarely execute.
/// It will panic upon multiple concurrent execution due to the underlying CM implementation.
///
/// Therefore, it should be only called upon once for each
/// new connection (to a new machine).
pub struct Explorer {
    inner_dev: DeviceRef,

    // methods for waiting for the completion of the explore process
    done: completion,
    result: Option<sa_path_rec>,
}

impl Explorer {
    pub fn new(dev: &DeviceRef) -> Self {
        Self {
            inner_dev: dev.clone(),
            done: Default::default(),
            result: None,
        }
    }
}

impl Explorer {
    /// Convert a user provided string to raw ib_gid.
    /// The string should be something like: fe80:0000:0000:0000:ec0d:9a03:0078:6376
    pub fn string_to_gid(gid: &String) -> Result<ib_gid, CMError> {
        let addr: Guid = (gid as &str)
            .parse()
            .map_err(|_| CMError::InvalidArg("string_to_gid", gid.clone()))?;
        let mut res: ib_gid = Default::default();
        res.raw = addr.get_raw();
        Ok(res)
    }

    /// Core resolve implementation
    pub fn resolve(self, service_id: u64, dst_gid: &String) -> Result<sa_path_rec, CMError> {        
        unimplemented!();
    }
}
