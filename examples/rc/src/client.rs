extern crate alloc;

use alloc::sync::Arc;
use core::ffi::c_void;
use rust_kernel_linux_util as log;
use rust_kernel_linux_util::timer::RTimer;

use crate::{TestError, RemoteInfo, CLIENT_OK, SERVER_INFO, SERVICE_ID};
use rust_kernel_linux_util::kthread;
use KRdmaKit::comm_manager::*;
use KRdmaKit::memory_region::MemoryRegion;
use KRdmaKit::queue_pairs::builder::QueuePairBuilder;
use KRdmaKit::rust_kernel_rdma_base::*;
use KRdmaKit::KDriver;

pub(crate) extern "C" fn client_kthread(_param: *mut c_void) -> i32 {
    let ret: i32 = match client() {
        Ok(_) => 0,
        Err(_) => 1,
    };
    while !kthread::should_stop() {
        kthread::yield_now();
    }
    ret
}

fn client() -> Result<(), TestError> {
    let driver = unsafe { KDriver::create().unwrap() };
    let client_ctx = driver
        .devices()
        .get(0)
        .ok_or(TestError::Error)?
        .open_context()
        .map_err(|_| {
            log::error!("Open client ctx error.");
            TestError::Error
        })?;

    let mut server_info = RemoteInfo::default();
    let timer = RTimer::new();
    loop {
        unsafe {
            if SERVER_INFO.rlock_f(|a| match a {
                None => false,
                Some(info) => {
                    server_info = info.clone();
                    true
                }
            }) {
                break;
            };
            if timer.passed_as_msec() > 5000.0 {
                log::error!("time out while waiting for server info");
                return Err(TestError::Error);
            }
        }
    }

    let client_port: u8 = 1; // Should choose according to the network setting
    let mut builder = QueuePairBuilder::new(&client_ctx);
    builder
        .allow_remote_rw()
        .allow_remote_atomic()
        .set_port_num(client_port);
        
    let client_qp = builder.build_rc().map_err(|_| {
        log::error!("Failed to build RC QP.");
        TestError::Error
    })?;

    // GID related to the server's
    let gid = server_info.server_gid;
    let explorer = Explorer::new(client_ctx.get_dev_ref());
    let path = unsafe { explorer.resolve_inner(SERVICE_ID, client_port, gid) }.map_err(|_| {
        log::error!("Error resolving path.");
        TestError::Error
    })?;
    let client_qp = client_qp.handshake(SERVICE_ID, path).map_err(|_| {
        log::error!("Handshake error.");
        TestError::Error
    })?;

    // memory region
    let client_mr = MemoryRegion::new(client_ctx.clone(), 256).map_err(|_| {
        log::error!("Failed to create client MR.");
        TestError::Error
    })?;

    let client_slot_0 = unsafe { (client_mr.get_virt_addr() as *mut u64).as_mut().unwrap() };
    let client_slot_1 = unsafe {
        ((client_mr.get_virt_addr() + 8) as *mut u64)
            .as_mut()
            .unwrap()
    };
    *client_slot_0 = 0;
    *client_slot_1 = 12345678;
    log::info!("[Before] client: v0 {} v1 {}", client_slot_0, client_slot_1);

    let raddr = server_info.mr_raddr;
    let rkey = server_info.mr_rkey;
    let _ = client_qp
        .post_send_read(&client_mr, 0..8, false, raddr, rkey)
        .map_err(|_| {
            log::error!("Failed to read remote mr");
            TestError::Error
        })?;
    let _ = client_qp
        .post_send_write(&client_mr, 8..16, true, raddr + 8, rkey)
        .map_err(|_| {
            log::error!("Failed to write remote mr");
            TestError::Error
        })?;

    let mut completions = [Default::default(); 5];
    let timer = RTimer::new();
    loop {
        let ret = client_qp
            .poll_send_cq(&mut completions)
            .map_err(|_| TestError::Error)?;
        if ret.len() > 0 {
            break;
        }
        if timer.passed_as_msec() > 15.0 {
            log::error!("time out while poll send cq");
            break;
        }
    }
    unsafe { CLIENT_OK.wlock_f(|a| *a = true) };
    log::info!("[After ] client: v0 {} v1 {}", client_slot_0, client_slot_1);
    Ok(())
}
