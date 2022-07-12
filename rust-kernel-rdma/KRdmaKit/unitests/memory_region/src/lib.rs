#![no_std]
#![feature(get_mut_unchecked)]
#[warn(non_snake_case)]
#[warn(dead_code)]
extern crate alloc;

use alloc::sync::Arc;

use KRdmaKit::memory_region::*;
use KRdmaKit::rust_kernel_rdma_base::*;

use rust_kernel_linux_util as log;

use krdma_test::*;

fn test_memory_region() {
    // TODO
    log::info!("Start test whether memory region can work");

    let driver = unsafe { KRdmaKit::KDriver::create().unwrap() };

    let ctx = driver
        .devices()
        .into_iter()
        .next()
        .expect("no rdma device available")
        .open_context()
        .unwrap(); // TODO: error handling

    let mr = MemoryRegion::new(ctx.clone(), 1024 * 1024);
    assert_eq!(mr.is_ok(), true);

    let mr = mr.unwrap();

    log::info!(
        "sanity check RDMA addr {}, rkey {:?} and lkey {:?}",
        unsafe { mr.get_rdma_addr() },
        mr.rkey(),
        mr.lkey()
    );

    let mr = MemoryRegion::new(ctx.clone(), 8 * 1024 * 1024);
    assert_eq!(mr.is_err(), true);

    log::info!("All test has passed");
}

#[krdma_test(test_memory_region)]
fn ctx_init() {}
