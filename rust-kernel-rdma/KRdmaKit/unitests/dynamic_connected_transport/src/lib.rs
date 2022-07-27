#![no_std]
#![feature(get_mut_unchecked)]
#[warn(non_snake_case)]
#[warn(dead_code)]
extern crate alloc;

use krdma_test::*;
use rust_kernel_linux_util as log;
use rust_kernel_linux_util::bindings::completion;
use rust_kernel_linux_util::timer::RTimer;

use KRdmaKit::comm_manager::*;
use KRdmaKit::completion_queue::SharedReceiveQueue;
use KRdmaKit::memory_region::MemoryRegion;
use KRdmaKit::queue_pairs::builder::QueuePairBuilder;
use KRdmaKit::queue_pairs::dynamic_connected_transport::DynamicConnectedTargetBuilder;
use KRdmaKit::queue_pairs::endpoint::UnreliableDatagramEndpointQuerier;
use KRdmaKit::rust_kernel_rdma_base::*;
use KRdmaKit::services::UnreliableDatagramServer;
use KRdmaKit::KDriver;

#[derive(Debug)]
struct CMHandler;

struct CMHandlerClient(completion);

impl core::fmt::Debug for CMHandlerClient {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        f.debug_struct("CMHandlerClient").finish()
    }
}

impl CMCallbacker for CMHandler {
    fn handle_req(
        self: &mut Self,
        _reply_cm: CMReplyer,
        _event: &ib_cm_event,
    ) -> Result<(), CMError> {
        log::info!("in handle req!");
        Ok(())
    }

    fn handle_sidr_req(
        self: &mut Self,
        mut reply_cm: CMReplyer,
        event: &ib_cm_event,
    ) -> Result<(), CMError> {
        let conn_arg = unsafe { *(event.private_data as *mut usize) };
        log::info!("In handle SIDR request {}", conn_arg);

        // send a response
        let rep: ib_cm_sidr_rep_param = Default::default();
        reply_cm.send_sidr_reply(rep, 0 as usize)
    }
}

impl CMCallbacker for CMHandlerClient {
    fn handle_req(
        self: &mut Self,
        _reply_cm: CMReplyer,
        _event: &ib_cm_event,
    ) -> Result<(), CMError> {
        Ok(())
    }

    fn handle_sidr_rep(
        self: &mut Self,
        mut _reply_cm: CMReplyer,
        event: &ib_cm_event,
    ) -> Result<(), CMError> {
        let rep_param = unsafe { event.param.sidr_rep_rcvd }; // sidr_rep_rcvd
        log::info!(
            "client sidr reponse received, event {:?}, private data: {:?}, rep_param {:?}",
            event.event,
            event.private_data,
            rep_param
        );

        if rep_param.status != ib_cm_sidr_status::IB_SIDR_SUCCESS {
            log::error!("failed to send SIDR {:?}", rep_param);
        }
        self.0.done();
        Ok(())
    }
}

/// The error type of data plane operations
#[derive(thiserror_no_std::Error, Debug)]
pub enum TestError {
    #[error("Test error {0}")]
    Error(&'static str),
}

fn test_srq_construction() -> Result<(), TestError> {
    log::info!("Start test srq construction.");

    let driver = unsafe { KDriver::create().unwrap() };

    let ctx = driver
        .devices()
        .into_iter()
        .next()
        .expect("no rdma device available")
        .open_context()
        .map_err(|e| {
            log::error!("create context error {:?}", e);
            TestError::Error("CQ cons context")
        })?;

    log::info!("The context's device name {}", ctx.get_dev_ref().name());

    // create a QP
    let _srq = SharedReceiveQueue::create(&ctx, 32, 1).map_err(|e| {
        log::error!("create shared receive queue error {:?}", e);
        TestError::Error("Failed to create shared receive queue")
    })?;
    Ok(())
}

fn test_dct_builder() -> Result<(), TestError> {
    log::info!("Start test dct builder.");
    let driver = unsafe { KDriver::create().unwrap() };
    let ctx = driver
        .devices()
        .into_iter()
        .next()
        .expect("no rdma device available")
        .open_context()
        .map_err(|_| {
            log::error!("Open server ctx error.");
            TestError::Error("Server context error.")
        })?;

    log::info!("The context's device name {}", ctx.get_dev_ref().name());

    // create a QP
    let builder = DynamicConnectedTargetBuilder::new(&ctx);
    let dct_target = builder.build_dynamic_connected_target(73).map_err(|e| {
        log::error!("failed to create the DCT target with error {:?}", e);
        TestError::Error("DCT target creation error")
    })?;

    log::info!(
        "check dct key & num: [{} {}]",
        dct_target.dc_key(),
        dct_target.dct_num()
    );

    Ok(())
}

fn test_dct_query() -> Result<(), TestError> {
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
    let qd_hint = 37 as usize;
    let ud_server = UnreliableDatagramServer::create();
    let _server_cm = CMServer::new(server_service_id, &ud_server, server_ctx.get_dev_ref())
        .map_err(|_| {
            log::error!("Open server ctx error.");
            TestError::Error("Server context error.")
        })?;

    let mut builder = QueuePairBuilder::new(&server_ctx);
    builder.allow_remote_rw().set_port_num(server_port);
    let server_qp = builder.build_ud().map_err(|_| {
        log::error!("Failed to build UD QP.");
        TestError::Error("Build UD error.")
    })?;

    let server_qp = server_qp.bring_up_ud().map_err(|_| {
        log::error!("Failed to query path.");
        TestError::Error("Query path error.")
    })?;

    ud_server.reg_ud(qd_hint, &server_qp);

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

    // GID related to the server's
    let client_port: u8 = 1;
    let gid = server_ctx.get_dev_ref().query_gid(server_port).unwrap();
    let explorer = Explorer::new(client_ctx.get_dev_ref());
    let path =
        unsafe { explorer.resolve_inner(server_service_id, client_port, gid) }.map_err(|_| {
            log::error!("Error resolving path.");
            TestError::Error("Resolve path error.")
        })?;

    let querier =
        UnreliableDatagramEndpointQuerier::create(&client_ctx, client_port).map_err(|_| {
            log::error!("Error creating querier");
            TestError::Error("Create querier error")
        })?;

    let endpoint = querier
        .query(server_service_id, qd_hint, path)
        .map_err(|_| {
            log::error!("Error querying endpoint");
            TestError::Error("Query endpoint error")
        })?;

    log::info!("{:?}", endpoint);

    let mut builder = QueuePairBuilder::new(&client_ctx);
    builder.set_port_num(client_port).allow_remote_rw();
    let client_qp = builder.build_ud().map_err(|_| {
        log::error!("Error creating client ud");
        TestError::Error("Create client ud error")
    })?;
    let client_qp = client_qp.bring_up_ud().map_err(|_| {
        log::error!("Error bringing client ud");
        TestError::Error("Bring client ud error")
    })?;

    // no need to register mr because this runs in kernel and we only need a kernel mr.
    let server_mr = MemoryRegion::new(server_ctx.clone(), 512).map_err(|_| {
        log::error!("Failed to create server MR.");
        TestError::Error("Create mr error.")
    })?;

    let client_mr = MemoryRegion::new(client_ctx.clone(), 512).map_err(|_| {
        log::error!("Failed to create client MR.");
        TestError::Error("Create mr error.")
    })?;

    // write a value
    const GRH_SIZE: u64 = 40;
    let client_buf = client_mr.get_virt_addr() as *mut i8;
    let server_buf = server_mr.get_virt_addr() as *mut i8;

    unsafe { (*client_buf) = 127 };
    log::info!("[Before] client value is {}", unsafe { *client_buf });
    log::info!("[Before] server value is {}", unsafe {
        *((server_buf as u64 + GRH_SIZE) as *mut i8)
    });
    // The reason why the recv buffer is larger than the send buf is because the global route header
    // takes up 40 bytes of sge. And if the recv buffer is too small, the request received will be
    // dropped directly and you will never successfully poll its corresponding wc from the recv cq.
    let _ = server_qp
        .post_recv(&server_mr, 0..104, server_buf as u64)
        .map_err(|_| {
            log::error!("Failed post recv");
            TestError::Error("Post recv error.")
        })?;

    let _ = client_qp
        .post_datagram(&endpoint, &client_mr, 0..64, client_buf as u64, true)
        .map_err(|_| {
            log::error!("Failed post send");
            TestError::Error("Post send error.")
        })?;

    let mut completions = [Default::default()];

    let mut timer = RTimer::new();
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

    timer.reset();
    loop {
        let res = server_qp
            .poll_recv_cq(&mut completions)
            .map_err(|_| TestError::Error("Poll cq error"))?;
        if res.len() > 0 {
            log::info!("successfully poll recv cq");
            break;
        } else if timer.passed_as_msec() > 15.0 {
            log::info!("time out while poll recv cq");
            break;
        }
    }

    log::info!("[After]  server value is {}", unsafe {
        *((server_buf as u64 + GRH_SIZE) as *mut i8)
    });
    Ok(())
}

fn test_wrapper() -> Result<(), TestError> {
    test_srq_construction()?;
    test_dct_builder()?;
    // test_dct_query()?;
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
