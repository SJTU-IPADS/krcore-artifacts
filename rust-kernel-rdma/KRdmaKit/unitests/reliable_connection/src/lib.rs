#![no_std]
#![feature(get_mut_unchecked)]
#[warn(non_snake_case)]
#[warn(dead_code)]
extern crate alloc;

use alloc::string::ToString;
use core::ffi::c_void;
use krdma_test::*;
use rust_kernel_linux_util as log;
use rust_kernel_linux_util::timer::RTimer;
use rust_kernel_linux_util::kthread;

use KRdmaKit::comm_manager::*;
use KRdmaKit::memory_region::MemoryRegion;
use KRdmaKit::queue_pairs::builder::QueuePairBuilder;
use KRdmaKit::rust_kernel_rdma_base::*;
use KRdmaKit::services::rc::ReliableConnectionServer;
use KRdmaKit::KDriver;

/// The error type of data plane operations
#[derive(thiserror_no_std::Error, Debug)]
pub enum TestError {
    #[error("Test error {0}")]
    Error(&'static str),
}

fn test_rc_handshake() -> Result<(), TestError> {
    let driver = unsafe { KDriver::create().unwrap() };
    // server side
    let server_ctx = driver
        .devices()
        .get(0)
        .ok_or(TestError::Error("Not valid device"))?
        .open_context()
        .map_err(|_| {
            log::error!("Open server ctx error.");
            TestError::Error("Server context error.")
        })?;
    let server_port: u8 = 1;
    log::info!(
        "Server context's device name {}",
        server_ctx.get_dev_ref().name()
    );
    let server_service_id = 73;
    let rc_server = ReliableConnectionServer::create(&server_ctx, server_port);
    let server_cm = CMServer::new(server_service_id, &rc_server, server_ctx.get_dev_ref())
        .map_err(|_| {
            log::error!("Open server ctx error.");
            TestError::Error("Server context error.")
        })?;
    let addr = unsafe { server_cm.inner().raw_ptr() }.as_ptr() as u64;
    log::info!("Server cm addr 0x{:X}", addr);

    // client side
    let client_ctx = driver
        .devices()
        .get(1)
        .ok_or(TestError::Error("Not valid device"))?
        .open_context()
        .map_err(|_| {
            log::error!("Open client ctx error.");
            TestError::Error("Client context error.")
        })?;
    log::info!(
        "Client context's device name {}",
        client_ctx.get_dev_ref().name()
    );

    let client_port: u8 = 1;
    let mut builder = QueuePairBuilder::new(&client_ctx);
    builder
        .allow_remote_rw()
        .allow_remote_atomic()
        .set_port_num(client_port);
    let client_qp = builder.build_rc().map_err(|_| {
        log::error!("Failed to build RC QP.");
        TestError::Error("Build RC error.")
    })?;

    // GID related to the server's
    let gid = server_ctx.get_dev_ref().query_gid(server_port).unwrap();
    let explorer = Explorer::new(client_ctx.get_dev_ref());
    let path =
        unsafe { explorer.resolve_inner(server_service_id, client_port, gid) }.map_err(|_| {
            log::error!("Error resolving path.");
            TestError::Error("Resolve path error.")
        })?;
    let _client_qp = client_qp
        .handshake(server_service_id, path)
        .map_err(|_| {
            log::error!("Handshake error.");
            TestError::Error("Handshake error.")
        })?;
    log::info!("handshake succeeded");
    Ok(())
}

fn test_rc_duplicate_handshake() -> Result<(), TestError> {
    let driver = unsafe { KDriver::create().unwrap() };
    // server side
    let server_ctx = driver
        .devices()
        .get(0)
        .ok_or(TestError::Error("Not valid device"))?
        .open_context()
        .map_err(|_| {
            log::error!("Open server ctx error.");
            TestError::Error("Server context error.")
        })?;
    let server_port: u8 = 1;
    log::info!(
        "Server context's device name {}",
        server_ctx.get_dev_ref().name()
    );
    let server_service_id = 73;
    let rc_server = ReliableConnectionServer::create(&server_ctx, server_port);
    let server_cm = CMServer::new(server_service_id, &rc_server, server_ctx.get_dev_ref())
        .map_err(|_| {
            log::error!("Open server ctx error.");
            TestError::Error("Server context error.")
        })?;
    let addr = unsafe { server_cm.inner().raw_ptr() }.as_ptr() as u64;
    log::info!("Server cm addr 0x{:X}", addr);

    // client side
    let client_ctx = driver
        .devices()
        .get(1)
        .ok_or(TestError::Error("Not valid device"))?
        .open_context()
        .map_err(|_| {
            log::error!("Open client ctx error.");
            TestError::Error("Client context error.")
        })?;
    log::info!(
        "Client context's device name {}",
        client_ctx.get_dev_ref().name()
    );

    let client_port: u8 = 1;
    let mut builder = QueuePairBuilder::new(&client_ctx);
    builder
        .allow_remote_rw()
        .allow_remote_atomic()
        .set_port_num(client_port);
    let client_qp = builder.build_rc().map_err(|_| {
        log::error!("Failed to build RC QP.");
        TestError::Error("Build RC error.")
    })?;

    // GID related to the server's
    let gid = server_ctx.get_dev_ref().query_gid(server_port).unwrap();
    let explorer = Explorer::new(client_ctx.get_dev_ref());
    let path =
        unsafe { explorer.resolve_inner(server_service_id, client_port, gid) }.map_err(|_| {
            log::error!("Error resolving path.");
            TestError::Error("Resolve path error.")
        })?;
    let _client_qp_1 = client_qp
        .handshake(server_service_id, path)
        .map_err(|_| {
            log::error!("Handshake error.");
            TestError::Error("Handshake error.")
        })?;

    let mut builder = QueuePairBuilder::new(&client_ctx);
    builder
        .allow_remote_rw()
        .allow_remote_atomic()
        .set_port_num(client_port);
    let client_qp = builder.build_rc().map_err(|_| {
        log::error!("Failed to build RC QP.");
        TestError::Error("Build RC error.")
    })?;

    // GID related to the server's
    let gid = server_ctx.get_dev_ref().query_gid(server_port).unwrap();
    let explorer = Explorer::new(client_ctx.get_dev_ref());
    let path =
        unsafe { explorer.resolve_inner(server_service_id, client_port, gid) }.map_err(|_| {
            log::error!("Error resolving path.");
            TestError::Error("Resolve path error.")
        })?;
    let _client_qp_2 = client_qp
        .handshake(server_service_id, path)
        .map_err(|_| {
            log::error!("Handshake error.");
            TestError::Error("Handshake error.")
        })?;
    log::info!("handshake succeeded");
    Ok(())
}

fn test_rc_read_write() -> Result<(), TestError> {
    let driver = unsafe { KDriver::create().unwrap() };
    // server side
    let server_ctx = driver
        .devices()
        .get(0)
        .ok_or(TestError::Error("Not valid device"))?
        .open_context()
        .map_err(|_| {
            log::error!("Open server ctx error.");
            TestError::Error("Server context error.")
        })?;
    let server_port: u8 = 1;
    log::info!(
        "Server context's device name {}",
        server_ctx.get_dev_ref().name()
    );
    let server_service_id = 73;
    let rc_server = ReliableConnectionServer::create(&server_ctx, server_port);
    let server_cm = CMServer::new(server_service_id, &rc_server, server_ctx.get_dev_ref())
        .map_err(|_| {
            log::error!("Open server ctx error.");
            TestError::Error("Server context error.")
        })?;
    let addr = unsafe { server_cm.inner().raw_ptr() }.as_ptr() as u64;
    log::info!("Server cm addr 0x{:X}", addr);

    // client side
    let client_ctx = driver
        .devices()
        .get(1)
        .ok_or(TestError::Error("Not valid device"))?
        .open_context()
        .map_err(|_| {
            log::error!("Open client ctx error.");
            TestError::Error("Client context error.")
        })?;
    log::info!(
        "Client context's device name {}",
        client_ctx.get_dev_ref().name()
    );

    let client_port: u8 = 1;
    let mut builder = QueuePairBuilder::new(&client_ctx);
    builder
        .allow_remote_rw()
        .allow_remote_atomic()
        .set_port_num(client_port);
    let client_qp = builder.build_rc().map_err(|_| {
        log::error!("Failed to build RC QP.");
        TestError::Error("Build RC error.")
    })?;

    // GID related to the server's
    let gid = server_ctx.get_dev_ref().query_gid(server_port).unwrap();
    let explorer = Explorer::new(client_ctx.get_dev_ref());
    let path =
        unsafe { explorer.resolve_inner(server_service_id, client_port, gid) }.map_err(|_| {
            log::error!("Error resolving path.");
            TestError::Error("Resolve path error.")
        })?;
    let client_qp = client_qp
        .handshake(server_service_id, path)
        .map_err(|_| {
            log::error!("Handshake error.");
            TestError::Error("Handshake error.")
        })?;
    log::info!("handshake succeeded");

    // memory region
    let server_mr = MemoryRegion::new(server_ctx.clone(), 256).map_err(|_| {
        log::error!("Failed to create server MR.");
        TestError::Error("Create mr error.")
    })?;
    let client_mr = MemoryRegion::new(client_ctx.clone(), 256).map_err(|_| {
        log::error!("Failed to create client MR.");
        TestError::Error("Create mr error.")
    })?;

    let server_slot_0 = unsafe { (server_mr.get_virt_addr() as *mut u64).as_mut().unwrap() };
    let client_slot_0 = unsafe { (client_mr.get_virt_addr() as *mut u64).as_mut().unwrap() };
    let server_slot_1 = unsafe {
        ((server_mr.get_virt_addr() + 8) as *mut u64)
            .as_mut()
            .unwrap()
    };
    let client_slot_1 = unsafe {
        ((client_mr.get_virt_addr() + 8) as *mut u64)
            .as_mut()
            .unwrap()
    };
    *client_slot_0 = 0;
    *client_slot_1 = 12345678;
    *server_slot_0 = 87654321;
    *server_slot_1 = 0;
    log::info!("[Before] client: v0 {} v1 {}", client_slot_0, client_slot_1);
    log::info!("[Before] server: v0 {} v1 {}", server_slot_0, server_slot_1);

    let raddr = unsafe { server_mr.get_rdma_addr() };
    let rkey = server_mr.rkey().0;
    let _ = client_qp
        .post_send_read(&client_mr, 0..8, false, raddr, rkey)
        .map_err(|_| {
            log::error!("Failed to read remote mr");
            TestError::Error("RC read error.")
        })?;
    let _ = client_qp
        .post_send_write(&client_mr, 8..16, true, raddr + 8, rkey)
        .map_err(|_| {
            log::error!("Failed to write remote mr");
            TestError::Error("RC read error.")
        })?;

    let mut completions = [Default::default(); 5];
    let timer = RTimer::new();
    loop {
        let ret = client_qp
            .poll_send_cq(&mut completions)
            .map_err(|_| TestError::Error("Poll cq error"))?;
        if ret.len() > 0 {
            log::info!("successfully poll send cq");
            break;
        }
        if timer.passed_as_msec() > 15.0 {
            log::info!("time out while poll send cq");
            break;
        }
    }

    log::info!("[After ] client: v0 {} v1 {}", client_slot_0, client_slot_1);
    log::info!("[After ] server: v0 {} v1 {}", server_slot_0, server_slot_1);
    Ok(())
}

extern "C" fn joinable_kthread(param: *mut c_void) -> i32 {
    log::info!("test kthread::should_stop()");
    while !kthread::should_stop() {
        kthread::yield_now();
    }
    log::info!("this kthread should stop, exiting...");
    param as i32
}

fn test_kthread() -> Result<(), TestError> {
    let arg: u64 = 73;
    let builder = kthread::Builder::new()
        .set_name("joinable_kthread".to_string())
        .set_parameter(arg as *mut c_void);
    let handler = builder.spawn(joinable_kthread);
    if handler.is_err() {
        log::error!("failed to spawn test thread");
        return Err(TestError::Error("kthread error"))
    }
    let handler = handler.unwrap();
    kthread::sleep(2);
    let ret = handler.join();
    if ret != (arg as i32) {
        log::error!("kthread join returns {}, expected {}", ret, arg);
    }
    log::info!("kthread joined successfully");
    Ok(())
}

fn test_wrapper() -> Result<(), TestError> {
    test_rc_handshake()?;
    test_rc_duplicate_handshake()?;
    test_rc_read_write()?;
    test_kthread()?;
    Ok(())
}

#[krdma_main]
fn main() {
    match test_wrapper() {
        Ok(_) => {
            log::info!("pass all tests")
        }
        Err(e) => {
            log::error!("test error {:?}", e)
        }
    };
}
