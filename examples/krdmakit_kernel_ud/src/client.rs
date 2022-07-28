extern crate alloc;
use core::ffi::c_void;

use crate::{TestError, SERVER_INFO, SERVICE_ID, TRANSFER_SIZE, QD_HINT};
use rust_kernel_linux_util as log;
use rust_kernel_linux_util::kthread;
use rust_kernel_linux_util::rwlock::ReadWriteLock;
use rust_kernel_linux_util::timer::RTimer;

use KRdmaKit::comm_manager::*;
use KRdmaKit::memory_region::MemoryRegion;
use KRdmaKit::queue_pairs::builder::QueuePairBuilder;
use KRdmaKit::queue_pairs::endpoint::DatagramEndpointQuerier;
use KRdmaKit::rust_kernel_rdma_base::*;
use KRdmaKit::KDriver;

pub(crate) extern "C" fn client_kthread(_param: *mut c_void) -> i32 {
    kthread::sleep(1);
    let ret = match client() {
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
    // client side
    let client_ctx = driver
        .devices()
        .get(0)
        .ok_or(TestError::Error("client dev"))?
        .open_context()
        .map_err(|_| TestError::Error("client ctx"))?;

    // GID related to the server's
    let mut gid: ib_gid = Default::default();
    let info: &ReadWriteLock<Option<ib_gid>> = unsafe { SERVER_INFO::get_ref() };
    let received = info.rlock_f(|a| match a {
        None => false,
        Some(info) => {
            gid = info.clone();
            true
        }
    });
    if !received {
        return Err(TestError::Error("SERVER_INFO unset"));
    }

    let client_port: u8 = 1;
    let explorer = Explorer::new(client_ctx.get_dev_ref());
    let path = unsafe { explorer.resolve_inner(SERVICE_ID, client_port, gid) }
        .map_err(|_| TestError::Error("resolve path"))?;

    let querier = DatagramEndpointQuerier::create(&client_ctx, client_port)
        .map_err(|_| TestError::Error("create querier"))?;

    let endpoint = querier
        .query(SERVICE_ID, QD_HINT, path)
        .map_err(|_| TestError::Error("query endpoint"))?;

    let mut builder = QueuePairBuilder::new(&client_ctx);
    builder.set_port_num(client_port).allow_remote_rw();
    let client_qp = builder
        .build_ud()
        .map_err(|_| TestError::Error("create client ud"))?;
    let client_qp = client_qp
        .bring_up_ud()
        .map_err(|_| TestError::Error("bring up client ud"))?;

    // no need to register mr because this runs in kernel and we only need a kernel mr.
    let client_mr = MemoryRegion::new(client_ctx.clone(), 512)
        .map_err(|_| TestError::Error("create client MR"))?;

    // write a value
    let client_buf = client_mr.get_virt_addr() as *mut i8;

    unsafe { (*client_buf) = 127 };
    // The reason why the recv buffer is larger than the send buf is because the global route header
    // takes up 40 bytes of sge. And if the recv buffer is too small, the request received will be
    // dropped directly and you will never successfully poll its corresponding wc from the recv cq.
    let _ = client_qp
        .post_datagram(
            &endpoint,
            &client_mr,
            0..TRANSFER_SIZE,
            client_buf as u64,
            true,
        )
        .map_err(|_| TestError::Error("client post send"))?;

    let mut completions = [Default::default(); 10];
    let timer = RTimer::new();
    loop {
        let ret = client_qp
            .poll_send_cq(&mut completions)
            .map_err(|_| TestError::Error("client poll send cq"))?;
        if ret.len() > 0 {
            break;
        }
        if timer.passed_as_msec() > 15.0 {
            log::error!("time out while poll send cq");
            break;
        }
    }
    Ok(())
}
