#![no_std]
#![feature(get_mut_unchecked)]
#[warn(non_snake_case)]
#[warn(dead_code)]
extern crate alloc;

use KRdmaKit::rust_kernel_rdma_base::*;

use KRdmaKit::ctrl::RCtrl;

use rust_kernel_linux_util as log;

use krdma_test::*;

#[krdma_main]
fn test_global_context() {
    use KRdmaKit::KDriver;
    let driver = unsafe { KDriver::create().unwrap() };
    let client_ctx = driver
        .devices()
        .into_iter()
        .next()
        .expect("no rdma device available");
        //.open()
        //.unwrap();
    log::info!("{:?} ", client_ctx.get_device_attr());

    for i in 1..5 { 
        log::info!("check port {} res: {:?}", i, client_ctx.get_port_attr(i));
    }

    /* 
    let server_ctx = driver
        .devices()
        .into_iter()
        .next()
        .expect("no rdma device available")
        .open()
        .unwrap(); */

    /* 
    let server_service_id: u64 = 0;
    let _ctrl = RCtrl::create(server_service_id, &server_ctx);

    let path_res = client_ctx.explore_path(client_ctx.get_gid_as_string(), server_service_id);
    log::info!("check created path res: {:?}", path_res.unwrap()); */

    log::info!("pass all tests");
}
