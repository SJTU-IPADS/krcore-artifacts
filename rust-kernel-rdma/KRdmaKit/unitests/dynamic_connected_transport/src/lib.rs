#![no_std]
#![feature(get_mut_unchecked)]
#[warn(non_snake_case)]
#[warn(dead_code)]
extern crate alloc;

use alloc::sync::Arc;

use krdma_test::*;
use rust_kernel_linux_util as log;
use rust_kernel_linux_util::timer::RTimer;

use KRdmaKit::comm_manager::*;
use KRdmaKit::completion_queue::SharedReceiveQueue;
use KRdmaKit::memory_region::MemoryRegion;

use KRdmaKit::queue_pairs::builder::QueuePairBuilder;
use KRdmaKit::queue_pairs::dynamic_connected_transport::DynamicConnectedTargetBuilder;
use KRdmaKit::queue_pairs::endpoint::DatagramEndpointQuerier;

use KRdmaKit::services::dc::DCTargetService;

use KRdmaKit::rust_kernel_rdma_base::*;
use KRdmaKit::KDriver;

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
    log::info!("\nStart test dct builder.");
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

    // create a target
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

    // create a QP
    let dc_qp = DynamicConnectedTargetBuilder::new(&ctx)
        .build_dc()
        .map_err(|e| {
            log::error!("failed to create DCQP with error {:?}", e);
            TestError::Error("DCQP creation error")
        })?
        .bring_up_dc()
        .map_err(|e| {
            log::error!("failed to bring up DCQP with error {:?}", e);
            TestError::Error("DCQP bringup error")
        })?;

    log::info!("check DCQP: {:?}", dc_qp.qp_num());

    Ok(())
}

fn test_dct_query() -> Result<(), TestError> {
    log::info!("\nDCT query service started");

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

    log::info!(
        "Server context's device name {}",
        server_ctx.get_dev_ref().name()
    );

    // start the listening service
    let server_service_id = 73;
    let server = DCTargetService::create();
    let _server_cm = CMServer::new(server_service_id, &server, server_ctx.get_dev_ref()).unwrap();

    // create a target
    let mut builder = DynamicConnectedTargetBuilder::new(&server_ctx);
    builder.allow_remote_rw().allow_remote_atomic();

    let dct_target = Arc::new(builder.build_dynamic_connected_target(73).map_err(|e| {
        log::error!("failed to create the DCT target with error {:?}", e);
        TestError::Error("DCT target creation error")
    })?);

    log::info!(
        "check dct key & num: [{} {}]",
        dct_target.dc_key(),
        dct_target.dct_num()
    );
    server.reg_qp(73, &dct_target);

    // create a QP
    let mut builder = DynamicConnectedTargetBuilder::new(&server_ctx);
    builder.allow_remote_rw().allow_remote_atomic();
    let dc_qp = builder
        .build_dc()
        .map_err(|e| {
            log::error!("failed to create DCQP with error {:?}", e);
            TestError::Error("DCQP creation error")
        })?
        .bring_up_dc()
        .map_err(|e| {
            log::error!("failed to bring up DCQP with error {:?}", e);
            TestError::Error("DCQP bringup error")
        })?;

    log::info!("check DCQP: {:?}", dc_qp.qp_num());

    let gid = server_ctx
        .get_dev_ref()
        .query_gid(dct_target.port_num())
        .unwrap();
    let explorer = Explorer::new(server_ctx.get_dev_ref());
    let path = unsafe { explorer.resolve_inner(server_service_id, dc_qp.port_num(), gid) }
        .map_err(|_| {
            log::error!("Error resolving path.");
            TestError::Error("Error resolving path.")
        })?;

    // now start query the DCT (datagram) endpoint
    let querier = DatagramEndpointQuerier::create(&server_ctx, dc_qp.port_num()).map_err(|_| {
        log::error!("Error creating querier");
        TestError::Error("Error creating querier")
    })?;

    let endpoint = querier.query(server_service_id, 73, path).map_err(|_| {
        log::error!("Error querying endpoint");
        TestError::Error("Error querying endpoint")
    })?;

    log::info!("sanity check the endpoint {:?}", endpoint);

    // send the requests
    // memory region
    let mr = MemoryRegion::new(server_ctx.clone(), 256)
        .map_err(|_| TestError::Error("Failed to create client MR."))?;

    let _ = dc_qp
        .post_send_dc_read(
            &endpoint,
            &mr,
            0..8,
            true,
            unsafe { mr.get_rdma_addr() + 16 },
            mr.rkey().0 + 16,
        )
        .map_err(|_| TestError::Error("Failed to post send"))?;

    let mut completions = [Default::default(); 3];
    let timer = RTimer::new();
    loop {
        let ret = dc_qp
            .poll_send_cq(&mut completions)
            .map_err(|_| TestError::Error("Failed to poll"))?;

        if ret.len() > 0 {
            break;
        }
        if timer.passed_as_msec() > 40.0 {
            log::error!("time out while poll send cq");
            break;
        }
    }

    log::info!(
        "sanity check the polled ib_wc {:?}, op_code : {:?}",
        completions[0].status,
        completions[0].opcode
    );

    Ok(())
}

fn test_wrapper() -> Result<(), TestError> {
    test_srq_construction()?;
    test_dct_builder()?;
    test_dct_query()?;
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
