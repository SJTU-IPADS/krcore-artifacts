#![no_std]

use krdma_test::{krdma_test, krdma_drop};

use rust_kernel_linux_util::linux_kernel_module;
use rust_kernel_linux_util as log;

fn test_0() {
    log::info!("test case 0!");
}

fn test_1() {
    log::info!("test case 1!");
}

#[krdma_test(test_0,test_1)]
fn init() { 
    log::info!("in init"); 
}

#[krdma_drop]
fn drop() {
    log::info!("in drop");
}
