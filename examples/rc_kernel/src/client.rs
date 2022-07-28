extern crate alloc;

use core::ffi::c_void;
use rust_kernel_linux_util as log;
use rust_kernel_linux_util::timer::RTimer;

use crate::{RemoteInfo, TestError, SERVER_INFO, SERVICE_ID};
use rust_kernel_linux_util::kthread;
use KRdmaKit::comm_manager::*;
use KRdmaKit::memory_region::MemoryRegion;
use KRdmaKit::queue_pairs::builder::QueuePairBuilder;
use KRdmaKit::rust_kernel_rdma_base::*;
use KRdmaKit::KDriver;

pub(crate) extern "C" fn client_kthread(_param: *mut c_void) -> i32 {
    kthread::sleep(1);
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
        .ok_or(TestError::Error("client device"))?
        .open_context()
        .map_err(|_| {
            log::error!("client context");
            TestError::Error("client context")
        })?;

    // make sure the static `SERVER_INFO` has been correctly set before the client thread starts
    let server_info: &Option<RemoteInfo> = unsafe { SERVER_INFO::get_ref() };
    let server_info = match server_info {
        None => {
            log::error!("server info unset");
            return Err(TestError::Error("server info unset"));
        }
        Some(info) => info.clone(),
    };
    let client_port: u8 = 1; // Should choose according to the network setting
    let mut builder = QueuePairBuilder::new(&client_ctx);
    builder
        .allow_remote_rw()
        .allow_remote_atomic()
        .set_port_num(client_port);
        
    let client_qp = builder.build_rc().map_err(|_| {
        log::error!("build client rc");
        TestError::Error("build client rc")
    })?;

    // GID related to the server's
    let gid = server_info.server_gid;
    let explorer = Explorer::new(client_ctx.get_dev_ref());
    let path = unsafe { explorer.resolve_inner(SERVICE_ID, client_port, gid) }.map_err(|_| {
        log::error!("client resolve path");
        TestError::Error("client resolve path")
    })?;
    let client_qp = client_qp.handshake(SERVICE_ID, path).map_err(|_| {
        log::error!("client handshake");
        TestError::Error("client handshake")
    })?;

    // memory region
    let client_mr = MemoryRegion::new(client_ctx.clone(), 256).map_err(|_| {
        log::error!("client mr");
        TestError::Error("client mr")
    })?;

    let client_slot_0 = unsafe { (client_mr.get_virt_addr() as *mut u64).as_mut().unwrap() };
    let client_slot_1 = unsafe {
        ((client_mr.get_virt_addr() + 8) as *mut u64)
            .as_mut()
            .unwrap()
    };
    *client_slot_0 = 0;
    *client_slot_1 = 12345678;
    log::info!(
        "[Before] client: [0] {} [1] {}",
        client_slot_0,
        client_slot_1
    );

    let raddr = server_info.mr_raddr;
    let rkey = server_info.mr_rkey;
    let _ = client_qp
        .post_send_read(&client_mr, 0..8, false, raddr, rkey)
        .map_err(|_| {
            log::error!("rdma read");
            TestError::Error("rdma read")
        })?;
    let _ = client_qp
        .post_send_write(&client_mr, 8..16, true, raddr + 8, rkey)
        .map_err(|_| {
            log::error!("rdma write");
            TestError::Error("rdma write")
        })?;

    let mut completions = [Default::default(); 5];
    let timer = RTimer::new();
    loop {
        let ret = client_qp.poll_send_cq(&mut completions).map_err(|_| {
            log::error!("poll client send cq");
            TestError::Error("poll client send cq")
        })?;
        if ret.len() > 0 {
            break;
        }
        if timer.passed_as_msec() > 15.0 {
            log::error!("poll client send cq time out");
            return Err(TestError::Error("poll client send cq time out"));
        }
    }
    log::info!(
        "[After ] client: [0] {} [1] {}",
        client_slot_0,
        client_slot_1
    );
    Ok(())
}
