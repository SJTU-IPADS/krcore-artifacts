/// This module should be only used in the kernel space
use no_std_net::Guid;
use rdma_shim::utils::completion;
use rdma_shim::bindings::*;

use alloc::boxed::Box;
use core::option::Option;

use super::{c_types, CMError, DeviceRef, String};
use crate::{alloc::string::ToString, log};

pub type SubnetAdminPathRecord = sa_path_rec;

/// Explore time out. set 5 seconds
pub const EXPLORE_TIMEOUT_MS: rdma_shim::ffi::c_types::c_int = 5000;

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
    result: Option<SubnetAdminPathRecord>,
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

    /// Convert raw ib_gid to string
    /// The string will be like: fe80:0000:0000:0000:ec0d:9a03:0078:6376
    pub fn gid_to_string(gid: &ib_gid) -> String {
        let gid: Guid = Guid::new_u8(unsafe { &gid.raw });
        alloc::format!("{}", gid)
    }

    /// Core resolve implementation
    pub unsafe fn resolve(
        self,
        service_id: u64,
        source_port_id: u8,
        dst_gid: &String,
    ) -> Result<SubnetAdminPathRecord, CMError> {
        let dst_gid = Self::string_to_gid(dst_gid)?;
        self.resolve_inner(service_id, source_port_id, dst_gid)
    }

    /// Core resolve implementation
    pub unsafe fn resolve_inner(
        mut self,
        service_id: u64,
        source_port_id: u8,
        dst_gid: ib_gid,
    ) -> Result<SubnetAdminPathRecord, CMError> {
        self.done.init();

        // init an Subnet Administrator (SA) client
        let mut sa_client = SAClient::new();

        let mut path_request = SubnetAdminPathRecord {
            dgid: dst_gid,
            // FIXME: what if gid_index != 0?
            sgid: self.inner_dev.query_gid(source_port_id, 0).map_err(|_| {
                CMError::InvalidArg("Source port number", source_port_id.to_string())
            })?,
            numb_path: 1, // FIXME
            service_id: service_id,
            ..Default::default()
        };

        let mut sa_query: *mut ib_sa_query = core::ptr::null_mut();
        let ret = ib_sa_path_rec_get(
            sa_client.raw_ptr(),
            self.inner_dev.raw_ptr().as_ptr(),
            source_port_id as _,
            &mut path_request as *mut _,
            path_rec_service_id() | path_rec_dgid() | path_rec_sgid() | path_rec_numb_path(),
            EXPLORE_TIMEOUT_MS as _,
            0,
            rdma_shim::kernel::linux_kernel_module::bindings::GFP_KERNEL,
            Some(explore_complete_handler),
            (&mut self as *mut Self).cast::<c_types::c_void>(),
            &mut sa_query as *mut *mut ib_sa_query,
        );

        if ret < 0 {
            return Err(CMError::Creation(ret));
        }

        // wait for the response
        self.done
            .wait(EXPLORE_TIMEOUT_MS)
            .map_err(|e| match e.to_kernel_errno() {
                0 => CMError::Timeout,
                _ => CMError::Unknown,
            })?;

        self.result.ok_or(CMError::Timeout)
    }
}

struct SAClient(Box<ib_sa_client>);

impl SAClient {
    unsafe fn new() -> Self {
        let mut res = Box::<ib_sa_client>::new_zeroed().assume_init();
        ib_sa_register_client(crate::to_ptr!(*res));
        Self(res)
    }

    fn raw_ptr(&mut self) -> *mut ib_sa_client {
        crate::to_ptr!(*(self.0))
    }
}

impl Drop for SAClient {
    fn drop(&mut self) {
        unsafe { ib_sa_unregister_client(self.raw_ptr()) };
    }
}

pub unsafe extern "C" fn explore_complete_handler(
    status: rdma_shim::ffi::c_types::c_int,
    resp: *mut sa_path_rec,
    context: *mut rdma_shim::ffi::c_types::c_void,
) {
    let e = &mut *(context as *mut Explorer);
    e.result = if status != 0 {
        log::error!("Failed to query the path with error {}", status);
        None
    } else {
        Some(*resp)
    };
    e.done.done();
}
