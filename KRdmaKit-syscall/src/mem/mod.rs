use KRdmaKit::rust_kernel_rdma_base::linux_kernel_module;
use linux_kernel_module::println;
use KRdmaKit::rust_kernel_rdma_base::bindings::{vm_fault, vm_area_struct, vm_operations_struct};
use KRdmaKit::rust_kernel_rdma_base::bindings::{bd_virt_to_page, bd_get_page};


static mut MY_VM_OP: vm_operations_struct =
    unsafe { core::mem::transmute([0u8; core::mem::size_of::<vm_operations_struct>()]) };

unsafe extern "C" fn open_handler(_area: *mut vm_area_struct) {}

unsafe extern "C" fn fault_handler(vmf: *mut vm_fault) -> c_int {
    let handler: *mut MemHandler = (*(*vmf).vma).vm_private_data as *mut _;
    (*handler).handle_fault(vmf)
}

use alloc::sync::Arc;

pub struct MemHandler {
    // temporal for test
    mem: core::option::Option<Arc<RMemPhy>>,
}


impl MemHandler {
    #[inline]
    unsafe fn handle_fault(&mut self, vmf: *mut vm_fault) -> c_int {
        if (*vmf).pgoff > 4096 {
            // FIXME: currently mem-handler only supports fixed 4KB sz
        } else {
            let page = bd_virt_to_page(
                Arc::get_mut_unchecked(self.mem.as_mut().unwrap())
                    .get_ptr()      // va
                    .cast::<c_void>(),
            );
            (*vmf).page = page;

            bd_get_page(page);
        }
        0
    }
}

impl Drop for MemHandler {
    fn drop(&mut self) {
        println!("MemHandler dropped");
    }
}


pub struct GlobalInit;


use core::option::Option;
use KRdmaKit::mem::{RMemPhy, Memory};
use KRdmaKit::rust_kernel_rdma_base::linux_kernel_module::c_types::{c_int, c_void};

impl GlobalInit {
    pub fn new() -> Option<Self> {
        unsafe {
            MY_VM_OP = Default::default();
            MY_VM_OP.open = Some(open_handler);
            MY_VM_OP.fault = Some(fault_handler);
        };
        Some(Self)
    }

    #[allow(dead_code)]
    pub fn null() -> Self {
        Self
    }
}

impl Drop for GlobalInit {
    fn drop(&mut self) {}
}