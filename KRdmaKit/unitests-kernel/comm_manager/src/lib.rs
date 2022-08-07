#![no_std]
#![feature(get_mut_unchecked)]
#[warn(non_snake_case)]
#[warn(dead_code)]
extern crate alloc;

use alloc::sync::Arc;

use KRdmaKit::rdma_shim::{rust_kernel_linux_util, linux_kernel_module};
use KRdmaKit::rdma_shim::bindings::*;
use KRdmaKit::rdma_shim::utils::completion;

use KRdmaKit::comm_manager::*;

use rust_kernel_linux_util as log;

use krdma_test::*;

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
    let server_gid = client_ctx.get_dev_ref().query_gid(valid_port, 0).unwrap(); // should never fail

    let path_res = unsafe { explore.resolve_inner(12, valid_port as _, server_gid) };
    log::info!(
        "sanity check the queried path res {:?} for port {}, gid {:?}",
        path_res,
        valid_port,
        server_gid
    );
    if path_res.is_err() {
        log::error!("failed to query path");
        return;
    }

    let path_res = path_res.unwrap();

    let mut c_handler = Arc::new(CMHandlerClient(Default::default()));
    let client_cm = CMSender::new(&c_handler, client_ctx.get_dev_ref());
    log::info!("sanity check client cm: {:?}", client_cm);

    if client_cm.is_err() {
        log::error!("failed to create client CM");
        return;
    }
    let mut client_cm = client_cm.unwrap();

    // send an SIDR request to the server_cm
    let c_handler_ref = unsafe { Arc::get_mut_unchecked(&mut c_handler) };
    c_handler_ref.0.init();
    let req = ib_cm_sidr_req_param {
        path: &path_res as *const _ as _,
        service_id: server_service_id,
        timeout_ms: 20,
        max_cm_retries: 3,
        ..Default::default()
    };

    let send_res = client_cm.send_sidr(req, 73 as usize);
    log::info!("sanity check send res: {:?}", send_res);
    c_handler_ref.0.wait(1000).unwrap();

    log::info!("pass all tests");
}
