extern crate alloc;
use crate::{TestError, GRH_SIZE, SERVER_INFO, TRANSFER_SIZE, SERVICE_ID, QD_HINT};
use core::ffi::c_void;
use rust_kernel_linux_util as log;
use rust_kernel_linux_util::kthread;
use rust_kernel_linux_util::timer::RTimer;
use KRdmaKit::comm_manager::*;
use KRdmaKit::memory_region::MemoryRegion;
use KRdmaKit::queue_pairs::builder::QueuePairBuilder;
use KRdmaKit::rust_kernel_rdma_base::*;
use KRdmaKit::services::UnreliableDatagramServer;
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
    // server side
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

    let ud_server = UnreliableDatagramServer::create();
    let _server_cm = CMServer::new(SERVICE_ID, &ud_server, server_ctx.get_dev_ref())
        .map_err(|_| {
            log::error!("Open server ctx error.");
            TestError::Error
        })?;

    let mut builder = QueuePairBuilder::new(&server_ctx);
    builder.allow_remote_rw().set_port_num(server_port);
    let server_qp = builder.build_ud().map_err(|_| {
        log::error!("Failed to build UD QP.");
        TestError::Error
    })?;

    let server_qp = server_qp.bring_up_ud().map_err(|_| {
        log::error!("Failed to query path.");
        TestError::Error
    })?;

    ud_server.reg_ud(QD_HINT, &server_qp);
    // no need to register mr because this runs in kernel and we only need a kernel mr.
    let server_mr = MemoryRegion::new(server_ctx.clone(), 512).map_err(|_| {
        log::error!("Failed to create server MR.");
        TestError::Error
    })?;

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
        .map_err(|_| {
            log::error!("Failed post recv");
            TestError::Error
        })?;

    let gid = server_ctx.get_dev_ref().query_gid(server_port).unwrap();

    SERVER_INFO.wlock_f(|a| *a = Some(gid));

    let mut completions = [Default::default(); 10];
    let timer = RTimer::new();
    loop {
        let res = server_qp
            .poll_recv_cq(&mut completions)
            .map_err(|_| TestError::Error)?;
        if res.len() > 0 {
            log::info!("successfully poll recv cq");
            break;
        } else if timer.passed_as_msec() > 30.0 {
            log::error!("time out while poll recv cq");
            break;
        }
    }
    log::info!("[After]  server value is {}", unsafe {
        *((server_buf as u64 + GRH_SIZE) as *mut i8)
    });
    Ok(())
}
