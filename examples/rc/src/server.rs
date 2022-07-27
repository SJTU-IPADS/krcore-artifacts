extern crate alloc;

use alloc::sync::Arc;
use core::ffi::c_void;
use rust_kernel_linux_util as log;
use rust_kernel_linux_util::timer::RTimer;

use crate::{TestError, RemoteInfo, CLIENT_OK, SERVER_INFO, SERVICE_ID};
use rust_kernel_linux_util::kthread;
use KRdmaKit::comm_manager::*;
use KRdmaKit::memory_region::MemoryRegion;
use KRdmaKit::rust_kernel_rdma_base::*;
use KRdmaKit::services::rc::ReliableConnectionServer;
use KRdmaKit::KDriver;

pub(crate) extern "C" fn server_kthread(_param: *mut c_void) -> i32 {
    let ret: i32 = match server() {
        Ok(_) => 0,
        Err(_) => 1,
    };
    while !kthread::should_stop() {
        kthread::yield_now();
    }
    ret
}

fn server() -> Result<(), TestError> {
    let driver = unsafe { KDriver::create().unwrap() };
    let server_ctx = driver
        .devices()
        .get(0)
        .ok_or(TestError::Error)?
        .open_context()
        .map_err(|_| {
            log::error!("Open server ctx error.");
            TestError::Error
        })?;
    let server_port: u8 = 1;
    let rc_server = ReliableConnectionServer::create(&server_ctx, server_port);
    let server_cm =
        CMServer::new(SERVICE_ID, &rc_server, server_ctx.get_dev_ref()).map_err(|_| {
            log::error!("Open server ctx error.");
            TestError::Error
        })?;
    let addr = unsafe { server_cm.inner().raw_ptr() }.as_ptr() as u64;
    log::info!("Server cm addr 0x{:X}", addr);

    // GID related to the server's
    let gid = server_ctx.get_dev_ref().query_gid(server_port).unwrap();
    // memory region
    let server_mr = MemoryRegion::new(server_ctx.clone(), 256).map_err(|_| {
        log::error!("Failed to create server MR.");
        TestError::Error
    })?;

    let server_slot_0 = unsafe { (server_mr.get_virt_addr() as *mut u64).as_mut().unwrap() };
    let server_slot_1 = unsafe {
        ((server_mr.get_virt_addr() + 8) as *mut u64)
            .as_mut()
            .unwrap()
    };
    *server_slot_0 = 87654321;
    *server_slot_1 = 0;
    log::info!("[Before] server: v0 {} v1 {}", server_slot_0, server_slot_1);

    let raddr = unsafe { server_mr.get_rdma_addr() };
    let rkey = server_mr.rkey().0;
    unsafe {
        SERVER_INFO.wlock_f(|a| {
            *a = Some(RemoteInfo {
                mr_raddr: raddr,
                mr_rkey: rkey,
                server_gid: gid,
            });
        })
    }
    let timer = RTimer::new();
    loop {
        unsafe {
            if CLIENT_OK.rlock_f(|a| *a) {
                break;
            }
        }
        if timer.passed_as_msec() > 5000.0 {
            log::error!("time out while waiting for client status ok");
            return Err(TestError::Error);
        }
    }
    log::info!("[After ] server: v0 {} v1 {}", server_slot_0, server_slot_1);
    Ok(())
}
