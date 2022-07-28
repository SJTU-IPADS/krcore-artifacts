#![no_std]
#![feature(get_mut_unchecked)]
#![warn(non_snake_case)]
extern crate alloc;

use alloc::string::ToString;
use core::ffi::c_void;
use core::ptr::null_mut;
use krdma_test::*;
use krdmakit_macros::*;
use rust_kernel_linux_util as log;
use rust_kernel_linux_util::kthread;
use KRdmaKit::comm_manager::CMServer;
use KRdmaKit::memory_region::MemoryRegion;
use KRdmaKit::rust_kernel_rdma_base::*;
use KRdmaKit::services::ReliableConnectionServer;
use KRdmaKit::KDriver;

mod client;

/// The error type of data plane operations
#[derive(thiserror_no_std::Error, Debug)]
pub enum TestError {
    #[error("Test error {0}")]
    Error(&'static str),
}

#[derive(Clone)]
pub struct RemoteInfo {
    mr_raddr: u64,
    mr_rkey: u32,
    server_gid: ib_gid,
}

impl Default for RemoteInfo {
    fn default() -> Self {
        RemoteInfo {
            mr_raddr: 0,
            mr_rkey: 0,
            server_gid: Default::default(),
        }
    }
}

declare_global!(SERVER_INFO, Option<crate::RemoteInfo>);

const SERVICE_ID: u64 = 13;

fn rdma_read_write_example() -> Result<(), TestError> {
    // server runs in the main thread
    unsafe {
        SERVER_INFO::init(None);
    }
    let driver = unsafe { KDriver::create().unwrap() };
    let server_ctx = driver
        .devices()
        .get(0)
        .ok_or(TestError::Error("server device"))?
        .open_context()
        .map_err(|_| TestError::Error("server context"))?;
    let server_port: u8 = 1;
    let rc_server = ReliableConnectionServer::create(&server_ctx, server_port);
    let server_cm = CMServer::new(SERVICE_ID, &rc_server, server_ctx.get_dev_ref())
        .map_err(|_| TestError::Error("server cm"))?;
    let addr = unsafe { server_cm.inner().raw_ptr() }.as_ptr() as u64;
    log::info!("Server cm addr 0x{:X}", addr);

    // GID related to the server's
    let gid = server_ctx.get_dev_ref().query_gid(server_port).unwrap();
    // server side registers a memory region for one-sided rdma_read and rdma_write
    let server_mr =
        MemoryRegion::new(server_ctx.clone(), 256).map_err(|_| TestError::Error("server mr"))?;

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
    // For simplicity, we use a static variable to pass the server information to the client thread
    // so the client thread can get the information from this shared memory (static variable).
    // Make sure the SERVER_INFO has been correctly set before the client thread starts.
    let server_info: &mut Option<RemoteInfo> = unsafe { SERVER_INFO::get_mut() };
    *server_info = Some(RemoteInfo {
        mr_raddr: raddr,
        mr_rkey: rkey,
        server_gid: gid,
    });

    // client runs in another thread, we should wait for the client thread to exit
    {
        let builder = kthread::Builder::new()
            .set_name("client_thread".to_string())
            .set_parameter(null_mut() as *mut c_void);
        let client_handler = builder.spawn(client::client_kthread);
        if client_handler.is_err() {
            return Err(TestError::Error("client thread spawn"));
        }
        let client_handler = client_handler.unwrap();
        kthread::sleep(1);
        if client_handler.join() != 0 {
            return Err(TestError::Error("client thread join"));
        }
    }

    // After the client thread finished rdma_read and rdma_write, the registered server mr
    // and client mr now have been written correctly.
    Ok(())
}

#[krdma_main]
fn main() {
    match rdma_read_write_example() {
        Ok(_) => log::info!("All OK"),
        Err(e) => log::error!("{:?}", e),
    };
}
