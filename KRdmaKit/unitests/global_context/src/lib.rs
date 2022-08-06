#![no_std]
#![feature(get_mut_unchecked)]
#[warn(non_snake_case)]
#[warn(dead_code)]
extern crate alloc;

use rust_kernel_linux_util as log;

use krdma_test::*;
use KRdmaKit::rdma_shim::linux_kernel_module;

#[krdma_main]
fn test_global_context() {
    use KRdmaKit::KDriver;
    let driver = unsafe { KDriver::create().unwrap() };
    let client_dev = driver
        .devices()
        .into_iter()
        .next()
        .expect("no rdma device available");
        //.open()
        //.unwrap();
    log::info!("{:?} ", client_dev.get_device_attr());

    for i in 1..5 { 
        log::info!("check port {} res: {:?}", i, client_dev.get_port_attr(i));
    }
    
    let client_ctx = client_dev.open_context();
    log::info!("ctx open result: {:?}", client_ctx);

    log::info!("pass all tests");
}
