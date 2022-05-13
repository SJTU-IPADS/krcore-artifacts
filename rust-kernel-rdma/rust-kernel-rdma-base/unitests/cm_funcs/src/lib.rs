#![no_std]

extern crate alloc;

use rust_kernel_rdma_base::linux_kernel_module;
use rust_kernel_rdma_base::*;

struct CMFuncsModule {}

impl CMFuncsModule {
    pub fn new() -> Self {
        // just check function is there
        Some(&mut ib_create_cm_id as *mut _);
        Some(&mut ib_destroy_cm_id as *mut _);
        Some(&mut ib_cm_listen as *mut _);
        Some(&mut ib_send_cm_rep as *mut _);
        Some(&mut ib_send_cm_req as *mut _);
        Some(&mut ib_send_cm_dreq as *mut _);
        Some(&mut ib_send_cm_drep as *mut _);
        Some(&mut ib_send_cm_rtu as *mut _);
        Some(&mut ib_sa_register_client as *mut _);
        Some(&mut ib_sa_unregister_client as *mut _);

        Self {}
    }
}

impl linux_kernel_module::KernelModule for CMFuncsModule {
    fn init() -> linux_kernel_module::KernelResult<Self> {
        let res = CMFuncsModule::new();
        Ok(res)
    }
}

linux_kernel_module::kernel_module!(
    CMFuncsModule,
    author: b"xmm",
    description: b"A module for testing creating/deleting qps",
    license: b"GPL"
);
