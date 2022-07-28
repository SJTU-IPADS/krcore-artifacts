#![no_std]
#![feature(get_mut_unchecked)]
#![warn(non_snake_case)]
#![warn(dead_code)]
extern crate alloc;

use alloc::string::ToString;
use core::ffi::c_void;
use core::ptr::null_mut;
use krdma_test::*;
use krdmakit_macros::*;
use rust_kernel_linux_util as log;
use rust_kernel_linux_util::kthread;
use rust_kernel_linux_util::rwlock::ReadWriteLock;
use rust_kernel_linux_util::timer::RTimer;
use KRdmaKit::comm_manager::CMServer;
use KRdmaKit::memory_region::MemoryRegion;
use KRdmaKit::queue_pairs::QueuePairBuilder;
use KRdmaKit::rust_kernel_rdma_base::*;
use KRdmaKit::services::UnreliableDatagramAddressService;
use KRdmaKit::KDriver;

mod client;

/// The error type of data plane operations
#[derive(thiserror_no_std::Error, Debug)]
pub enum TestError {
    #[error("Test error {0}")]
    Error(&'static str),
}

declare_global!(SERVER_INFO, crate::ReadWriteLock<Option<crate::ib_gid>>);

const SERVICE_ID: u64 = 13;
const QD_HINT: usize = 31;
const GRH_SIZE: u64 = 40;
const TRANSFER_SIZE: u64 = 64;

fn post_datagram_example() -> Result<(), TestError> {
    // server runs in the main thread
    unsafe {
        SERVER_INFO::init(ReadWriteLock::new(None));
    }
    let driver = unsafe { KDriver::create().unwrap() };
    let server_ctx = driver
        .devices()
        .get(0)
        .ok_or(TestError::Error("device"))?
        .open_context()
        .map_err(|_| TestError::Error("server ctx"))?;
    let server_port: u8 = 1;

    let ud_server = UnreliableDatagramAddressService::create();
    let _server_cm = CMServer::new(SERVICE_ID, &ud_server, server_ctx.get_dev_ref())
        .map_err(|_| TestError::Error("cm server"))?;

    let mut builder = QueuePairBuilder::new(&server_ctx);
    builder.allow_remote_rw().set_port_num(server_port);
    let server_qp = builder
        .build_ud()
        .map_err(|_| TestError::Error("build ud qp"))?;

    let server_qp = server_qp
        .bring_up_ud()
        .map_err(|_| TestError::Error("query path"))?;

    ud_server.reg_qp(QD_HINT, &server_qp);
    // no need to register mr because this runs in kernel and we only need a kernel mr.
    let server_mr =
        MemoryRegion::new(server_ctx.clone(), 512).map_err(|_| TestError::Error("server MR"))?;

    // write a value
    let server_buf = server_mr.get_virt_addr() as *mut i8;

    log::info!("[Before] server value is {}", unsafe {
        *((server_buf as u64 + GRH_SIZE) as *mut i8)
    });
    // The reason why the recv buffer is larger than the send buf is because the global route header
    // takes up 40 bytes of sge. And if the recv buffer is too small, the request received will be
    // dropped directly and you will never successfully poll its corresponding wc from the recv cq.
    let _ = server_qp
        .post_recv(&server_mr, 0..(TRANSFER_SIZE + GRH_SIZE), server_buf as u64)
        .map_err(|_| TestError::Error("server post recv"))?;

    let gid = server_ctx.get_dev_ref().query_gid(server_port).unwrap();

    let server_info: &mut ReadWriteLock<Option<ib_gid>> = unsafe { SERVER_INFO::get_mut() };
    server_info.wlock_f(|info| *info = Some(gid));

    // The client runs in another thread. The main thread waits for this thread to exit.
    // Make sure the SERVER_INFO must be correctly set before the client threads starts.
    {
        let builder = kthread::Builder::new()
            .set_name("client_thread".to_string())
            .set_parameter(null_mut() as *mut c_void);
        let client_handler = builder.spawn(client::client_kthread);
        kthread::sleep(1);
        if client_handler.is_err() {
            return Err(TestError::Error("failed to spawn client thread"));
        }
        let client_handler = client_handler.unwrap();
        let client_ret = client_handler.join();
        if client_ret != 0 {
            return Err(TestError::Error("Client join error"));
        }
    }

    let mut completions = [Default::default(); 10];
    let timer = RTimer::new();
    loop {
        let res = server_qp
            .poll_recv_cq(&mut completions)
            .map_err(|_| TestError::Error("server recv cq"))?;
        if res.len() > 0 {
            break;
        } else if timer.passed_as_msec() > 40.0 {
            log::error!("time out while poll recv cq");
            break;
        }
    }
    log::info!("[After]  server value is {}", unsafe {
        *((server_buf as u64 + GRH_SIZE) as *mut i8)
    });
    Ok(())
}

#[krdma_main]
fn main() {
    match post_datagram_example() {
        Ok(_) => log::info!("All OK"),
        Err(e) => log::error!("{:?}", e),
    };
}
