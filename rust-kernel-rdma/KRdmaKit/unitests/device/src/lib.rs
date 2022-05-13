#![no_std]
extern crate alloc;

pub mod console_msgs;

use alloc::boxed::Box;
use alloc::vec::Vec;
use KRdmaKit::{KDriver, rust_kernel_rdma_base};
use KRdmaKit::device::RContext;
use KRdmaKit::rust_kernel_rdma_base::linux_kernel_module::println;
// rust_kernel_rdma_base
use rust_kernel_rdma_base::linux_kernel_module;
use krdma_test::*;

static mut KDRIVER: Option<Box<KDriver>> = None;

unsafe fn global_kdriver() -> &'static mut Box<KDriver> {
    match KDRIVER {
        Some(ref mut x) => &mut *x,
        None => panic!()
    }
}


fn test1() {
    let driver = unsafe { global_kdriver() };
    let context_list: Vec<RContext> =
        driver.devices().into_iter().map(|nic| {
            nic.open().unwrap()
        }).collect();
    println!("ctx size:{}", context_list.len());
}


#[krdma_test(test1)]
fn ctx_init() {
    unsafe {
        KDRIVER = KDriver::create();
    }
}