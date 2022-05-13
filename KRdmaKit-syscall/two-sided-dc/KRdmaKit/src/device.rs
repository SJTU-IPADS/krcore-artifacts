#![allow(clippy::not_unsafe_ptr_arg_deref)]

use rust_kernel_rdma_base::*;

// linux kernel module use explicitly
use rust_kernel_rdma_base::linux_kernel_module;

use linux_kernel_module::{Error, KernelResult, println};
use core::option::Option;
use alloc::string::String;
use crate::net_util::gid_to_str;

/// The type `RNIC` supplies the abstraction of `ib_device`, `ib_gid`, `ib_device_attr`
/// information
pub struct RNIC {
    hca: *mut ib_device,
    attr: ib_device_attr,
    default_gid: ib_gid,
    port: u16,
    lid: u16,
}

impl core::fmt::Debug for RNIC {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        f.debug_struct("RNIC")
            .field("hca", &self.hca)
            .field("gid", &self.default_gid)
            .field("port", &self.port)
            .field("lid", &self.lid)
            .finish()
    }
}

impl RNIC {
    // note: we assume that the nic has only one port
    pub fn create(hca_ptr: *mut ib_device, port: u16) -> KernelResult<Self> {
        // 1. query the port
        let mut port_attr: ib_port_attr = Default::default();
        let err =
            unsafe { ib_query_port(hca_ptr, port as u8, &mut port_attr as *mut ib_port_attr) };
        if err != 0 {
            return Err(Error::from_kernel_errno(err));
        }

        if port_attr.state != ib_port_state::IB_PORT_ACTIVE {
            return Err(Error::from_kernel_errno(err));
        }
        // 2. init dev attr
        let mut data: ib_udata = Default::default();
        let mut dev_attr: ib_device_attr = Default::default();
        let hca: &mut ib_device = &mut unsafe { *hca_ptr };
        let err = unsafe {
            hca.query_device(
                hca_ptr,
                &mut dev_attr as *mut ib_device_attr,
                &mut data as *mut ib_udata,
            )
        };

        if err != 0 {
            return Err(Error::from_kernel_errno(err));
        }

        // 3. init gid
        let mut gid: ib_gid = Default::default();
        let err =
            unsafe { hca.query_gid(hca_ptr, port as u8, 0, &mut gid as *mut ib_gid) };
        if err != 0 {
            return Err(Error::from_kernel_errno(err));
        }

        // 4. setup RNIC properties
        Ok(Self {
            hca: hca_ptr,
            default_gid: gid,
            attr: dev_attr,
            port: port,
            lid: port_attr.lid as u16,
        })
    }

    #[inline]
    pub fn get_raw_device(&self) -> *mut ib_device {
        self.hca
    }

    #[inline]
    pub fn get_dev_attr(&self) -> ib_device_attr {
        self.attr
    }
}

/// RDMA Context serves for the abstraction of created `ib_pd`, `ib_mr` information and
/// RNIC information.
///
/// The creation and the reference of the RContext should indicate the life cycle. During the
/// creation, the context alloc the `pd` at first, which serves to be the handler of this context;
/// The kernel mr is fetched in the next, forming the whole context of kernel RDMA.
#[derive(Debug)]
pub struct RContext<'a> {
    hca: &'a mut RNIC,

    kmr: *mut ib_mr,
    // kernel global DMA MR to access the physical memory
    pd: *mut ib_pd,
}


impl<'a> RContext<'a> {
    pub fn create(hca: &'a mut RNIC) -> Option<Self> {
        //1. allocate the PD
        let pd = unsafe {
            ib_alloc_pd(
                hca.get_raw_device(),
                if crate::K_REG_ALWAYS {
                    0
                } else {
                    crate::IB_PD_UNSAFE_GLOBAL_RKEY
                } as u32,
            )
        };
        if pd == core::ptr::null_mut() {
            return None;
        }
        let mr_flags = ib_access_flags::IB_ACCESS_LOCAL_WRITE
            | ib_access_flags::IB_ACCESS_REMOTE_READ
            | ib_access_flags::IB_ACCESS_REMOTE_WRITE
            | ib_access_flags::IB_ACCESS_REMOTE_ATOMIC;
        let kmr = unsafe { ib_get_dma_mr(pd, mr_flags as i32) };
        if kmr.is_null() {
            println!("Failed to get kernel DMA mr");
            return None;
        }
        Some(Self { hca, pd, kmr })
    }

    #[inline]
    pub fn get_raw_dev(&self) -> *mut ib_device {
        self.hca.get_raw_device()
    }

    #[inline]
    pub fn get_gid(&self) -> ib_gid {
        self.hca.default_gid
    }

    #[inline]
    pub fn get_gid_as_string(&self) -> String {
        gid_to_str(self.hca.default_gid)
    }

    #[inline]
    pub fn get_port(&self) -> u8 {
        self.hca.port as u8
    }

    #[inline]
    pub fn get_pd(&self) -> *mut ib_pd {
        self.pd
    }

    #[inline]
    pub fn get_kmr(&self) -> *mut ib_mr { self.kmr }

    /// fetch lid; used for UD interactions
    #[inline]
    pub fn get_lid(&self) -> u16 { self.hca.lid }

    /// fetch subnet prefix; used for UD interactions
    #[inline]
    pub fn get_subnet_prefix(&self) -> u64 { unsafe { self.hca.default_gid.global.subnet_prefix as u64 } }

    /// fetch interface id; used for UD interactions
    #[inline]
    pub fn get_interface_id(&self) -> u64 { unsafe { self.hca.default_gid.global.interface_id as u64 } }

    /// remote key in kmr
    #[inline]
    pub unsafe fn get_rkey(&self) -> u32 {
        (*self.get_kmr()).rkey
    }

    /// local key in kmr
    #[inline]
    pub unsafe fn get_lkey(&self) -> u32 {
        (*self.get_kmr()).lkey
    }

    /// free the pd
    #[inline]
    pub fn reset(&mut self) {
        unsafe { ib_dereg_mr(self.kmr) };
        unsafe { ib_dealloc_pd(self.pd) };
        self.pd = core::ptr::null_mut();
        println!("finish reset one device");
    }
}

unsafe impl Send for RNIC {}

unsafe impl Sync for RNIC {}

unsafe impl<'a> Send for RContext<'a> {}

unsafe impl<'a> Sync for RContext<'a> {}

impl<'a> Drop for RContext<'a> {
    fn drop(&mut self) {
        self.reset();
    }
}
