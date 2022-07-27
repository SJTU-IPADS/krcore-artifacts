#![no_std]
#![feature(get_mut_unchecked)]
#![warn(non_snake_case)]
#![warn(dead_code)]
#[macro_use]
extern crate lazy_static;
extern crate alloc;

use alloc::string::ToString;
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

lazy_static! {
    static ref SERVER_INFO: ReadWriteLock<Option<ib_gid>> = ReadWriteLock::new(None);
}

const SERVICE_ID: u64 = 13;
const QD_HINT: usize = 31;
const GRH_SIZE: u64 = 40;
const TRANSFER_SIZE: u64 = 64;

fn post_datagram_example() -> Result<(), TestError> {
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
    match post_datagram_example() {
        Ok(_) => log::info!("All OK"),
        Err(_) => log::error!("Error"),
    };
}
