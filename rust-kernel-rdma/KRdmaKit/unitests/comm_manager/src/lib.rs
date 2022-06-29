#![no_std]
#![feature(get_mut_unchecked)]
#[warn(non_snake_case)]
#[warn(dead_code)]

extern crate alloc;

use alloc::sync::Arc;

use KRdmaKit::rust_kernel_rdma_base::*;
use KRdmaKit::comm_manager::*;

use KRdmaKit::ctrl::RCtrl;

use rust_kernel_linux_util as log;

use krdma_test::*;

#[derive(Debug)]
pub struct CMHandler; 

impl CMCallbacker for CMHandler { 
    fn handle_req(self: Arc<Self>, _reply_cm: CMReplyer, _event: &ib_cm_event)
        -> Result<(), CMError> { 
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
        .expect("no rdma device available").open_context();    

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

    let server_cm = server_cm.unwrap();
    /* server-side data structures  done */

    
    /* client-side data structures  */
    let client_ctx = driver
        .devices()
        .into_iter()
        .next()
        .expect("no rdma device available").open_context();    

    log::info!("client ctx open result: {:?}", client_ctx);
    if client_ctx.is_err() { 
        log::error!("open server ctx error.");
        return;
    }
    let client_ctx = client_ctx.unwrap();

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
