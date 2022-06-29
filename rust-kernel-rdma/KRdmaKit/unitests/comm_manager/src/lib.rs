#![no_std]
#![feature(get_mut_unchecked)]
#[warn(non_snake_case)]
#[warn(dead_code)]
extern crate alloc;

use alloc::string::String;
use alloc::sync::Arc;

use KRdmaKit::comm_manager::*;
use KRdmaKit::rust_kernel_rdma_base::*;

use KRdmaKit::ctrl::RCtrl;

use rust_kernel_linux_util as log;

use krdma_test::*;

#[derive(Debug)]
pub struct CMHandler;

impl CMCallbacker for CMHandler {
    fn handle_req(
        self: Arc<Self>,
        _reply_cm: CMReplyer,
        _event: &ib_cm_event,
    ) -> Result<(), CMError> {
        log::info!("in handle req!");
        Ok(())
    }
}

#[krdma_main]
fn test_cm() {
    use KRdmaKit::KDriver;
    let driver = unsafe { KDriver::create().unwrap() };

    /* server-side data structures  */
    let server_ctx = driver
        .devices()
        .into_iter()
        .next()
        .expect("no rdma device available")
        .open_context();

    log::info!("ctx open result: {:?}", server_ctx);
    if server_ctx.is_err() {
        log::error!("open server ctx error.");
        return;
    }
    let server_ctx = server_ctx.unwrap();

    let server_service_id = 73;
    let handler = Arc::new(CMHandler);
    let server_cm = CMServer::new(server_service_id, &handler, server_ctx.get_dev_ref());
    log::info!("check server cm: {:?}", server_cm);

    if server_cm.is_err() {
        log::error!("failed to create server CM");
        return;
    }
    /* server-side data structures  done */

    /* client-side data structures  */
    let client_ctx = driver
        .devices()
        .into_iter()
        .next()
        .expect("no rdma device available")
        .open_context();

    log::info!("client ctx open result: {:?}", client_ctx);
    if client_ctx.is_err() {
        log::error!("open server ctx error.");
        return;
    }
    let client_ctx = client_ctx.unwrap();

    // Explore the path
    let explore = Explorer::new(client_ctx.get_dev_ref());

    // query the activate port
    let valid_port = client_ctx.get_dev_ref().first_valid_port(1..5);
    if valid_port == None {
        log::error!("failed to find a valid port in 1..5");
        return;
    }
    let valid_port = valid_port.unwrap();

    // we use the server_gid for the server & client,
    // since the unittest run on the same machine
    let server_gid = client_ctx.get_dev_ref().query_gid(valid_port).unwrap(); // should never fail

    let path_res = unsafe { explore.resolve_inner(12, valid_port as _, server_gid) };
    log::info!(
        "sanity check the queried path res {:?} for port {}, gid {:?}",
        path_res,
        valid_port,
        server_gid
    );

    // TODO: what if all the gids is wrong? we should report an error

    /*
    let server_ctx = driver
        .devices()
        .into_iter()
        .next()
        .expect("no rdma device available")
        .open()
        .unwrap(); */

    /*
    let server_service_id: u64 = 0;
    let _ctrl = RCtrl::create(server_service_id, &server_ctx);

    let path_res = client_ctx.explore_path(client_ctx.get_gid_as_string(), server_service_id);
    log::info!("check created path res: {:?}", path_res.unwrap()); */

    log::info!("pass all tests");
}
