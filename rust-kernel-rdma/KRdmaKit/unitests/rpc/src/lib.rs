#![no_std]
#![feature(get_mut_unchecked)]
#[warn(non_snake_case)]
#[warn(dead_code)]
extern crate alloc;

use core::ptr::null_mut;
use KRdmaKit::cm::{EndPoint, SidrCM};
use KRdmaKit::rust_kernel_rdma_base::*;

use KRdmaKit::ctrl::RCtrl;
use KRdmaKit::qp::UD;
use KRdmaKit::rpc::data::{Header, ReqType};
use KRdmaKit::rpc::RPCClient;
use nostd_async::{Runtime, Task};

use krdma_test::*;
use crate::linux_kernel_module::println;

#[krdma_main]
fn test_rpc() {
    use KRdmaKit::KDriver;
    let remote_service_id: u64 = 50;
    let qd_hint = 73;
    let driver = unsafe { KDriver::create().unwrap() };
    let ctx = driver
        .devices()
        .into_iter()
        .next()
        .expect("no rdma device available")
        .open()
        .unwrap();
    let path_res = ctx.explore_path(ctx.get_gid_as_string(), remote_service_id);
    let mut ctrl = RCtrl::create(remote_service_id, &ctx).unwrap();
    let mut ud = UD::new(&ctx).unwrap();
    let server_ud = UD::new(&ctx).unwrap();
    ctrl.reg_ud(qd_hint, server_ud);
    let mut sidr_cm = SidrCM::new(&ctx, core::ptr::null_mut()).unwrap();
    let remote_info = sidr_cm.sidr_connect(
        path_res.unwrap(), remote_service_id as u64, qd_hint as u64);
    if remote_info.is_err() {
        return;
    }
    // ================= start to send message =====================
    let point = remote_info.unwrap();

    let runtime = Runtime::new();
    let dct_num = ctrl.get_dc_num();
    let mr = ctrl.get_self_test_mr();
    let mut rpc_node_c =
        RPCClient::<1>::create(&ctx, &ud,
                               dct_num, &mr);
    let mut rpc_node_s =
        RPCClient::<16>::create(&ctx,
                                ctrl.get_ud(qd_hint).unwrap(),
                                dct_num, &mr);
    let mut num = 32 as u64;

    let mut server_task = Task::new(async {
        rpc_node_s.poll_all().await
    });

    let mut client_task = Task::new(async {
        for i in 0..1 {
            let mut header = Header::new(ReqType::Dummy, i);
            let res = rpc_node_c.call::<u64, u64>(
                &point,
                &mut header,
                &mut num,
                5 * 1000).await;
        }
        println!("return client task");
        0 as u64
    });

    let s_handler = server_task.spawn(&runtime);
    let c_handler = client_task.spawn(&runtime);

    // s_handler.join();
    c_handler.join();
}
