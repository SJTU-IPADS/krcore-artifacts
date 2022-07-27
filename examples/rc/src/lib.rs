#![no_std]
#![feature(get_mut_unchecked)]
#![warn(non_snake_case)]
#[macro_use]
extern crate lazy_static;
extern crate alloc;

use alloc::string::ToString;
use alloc::sync::Arc;
use core::ffi::c_void;
use core::ptr::null_mut;
use krdma_test::*;
use rust_kernel_linux_util as log;
use rust_kernel_linux_util::kthread;
use rust_kernel_linux_util::rwlock::ReadWriteLock;
use KRdmaKit::rust_kernel_rdma_base::*;

mod client;
mod server;

/// The error type of data plane operations
#[derive(thiserror_no_std::Error, Debug)]
pub enum TestError {
    #[error("Test error")]
    Error,
}

#[derive(Clone)]
struct RemoteInfo {
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

lazy_static!(
    static ref SERVER_INFO: ReadWriteLock<Option<RemoteInfo>> = ReadWriteLock::new(None);
    static ref CLIENT_OK: ReadWriteLock<bool> = ReadWriteLock::new(false);
);

const SERVICE_ID: u64 = 13;

fn rdma_read_write_example() -> Result<(), TestError> {
    // server
    let builder = kthread::Builder::new()
        .set_name("server_thread".to_string())
        .set_parameter(null_mut() as *mut c_void);
    let server_handler = builder.spawn(server::server_kthread);
    if server_handler.is_err() {
        log::error!("failed to spawn server thread");
        return Err(TestError::Error);
    }
    let server_handler = server_handler.unwrap();

    // client
    let builder = kthread::Builder::new()
        .set_name("client_thread".to_string())
        .set_parameter(null_mut() as *mut c_void);
    let client_handler = builder.spawn(client::client_kthread);
    if client_handler.is_err() {
        log::error!("failed to spawn client thread");
        return Err(TestError::Error);
    }
    let client_handler = client_handler.unwrap();
    let server_ret = server_handler.join();
    let client_ret = client_handler.join();
    if server_ret != 0 || client_ret != 0 {
        Err(TestError::Error)
    } else {
        Ok(())
    }
}

#[krdma_main]
fn main() {
    match rdma_read_write_example() {
        Ok(_) => log::info!("All OK"),
        Err(_) => log::error!("Error"),
    };
}
