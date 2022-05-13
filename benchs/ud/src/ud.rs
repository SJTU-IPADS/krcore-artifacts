#[warn(non_snake_case)]
#[warn(dead_code)]
#[warn(unused_must_use)]
use KRdmaKit::rust_kernel_rdma_base;
// rust_kernel_rdma_base
use rust_kernel_rdma_base::*;
use rust_kernel_rdma_base::linux_kernel_module;
// linux_kernel_module
use linux_kernel_module::{println};
use KRdmaKit::device::{RContext};
use KRdmaKit::qp::{RC, RCOp, UD, UDOp, Config, get_qp_status};
use KRdmaKit::ib_path_explorer::IBExplorer;
use KRdmaKit::ctrl::RCtrl;
use KRdmaKit::mem::RMemVirt;
use KRdmaKit::mem::{RMemPhy, Memory};
use rust_kernel_linux_util::timer::KTimer;
use core::sync::atomic::compiler_fence;
use core::sync::atomic::Ordering;
use KRdmaKit::cm::SidrCM;
use KRdmaKit::qp::send_reg_mr;
use alloc::boxed::Box;

use alloc::sync::Arc;
use linux_kernel_module::random::getrandom;


static REMOTE_SERVICE_ID: usize = 50;
static QD_HINT: u64 = 73;
static mut RCTRL_READY: bool = false;
static CTX_IDX: usize = 0;
static OP_SIZE: u64 = 32;

// statistics
use crate::{TOTAL_OP, TOTAL_PASSED_US};

const DEFAULT_MR_ACCESS_FLAGS: u32 = ib_access_flags::IB_ACCESS_LOCAL_WRITE |
    ib_access_flags::IB_ACCESS_REMOTE_READ |
    ib_access_flags::IB_ACCESS_REMOTE_WRITE |
    ib_access_flags::IB_ACCESS_REMOTE_ATOMIC;

pub unsafe extern "C" fn server_thread(
    _data: *mut linux_kernel_module::c_types::c_void,
) -> linux_kernel_module::c_types::c_int {
    println!("server thread come in");

    let mut ctrl =
        RCtrl::create(REMOTE_SERVICE_ID,
                      &mut crate::ALLRCONTEXTS.get_mut()[CTX_IDX]).unwrap();
    let ctx: &mut RContext = &mut crate::ALLRCONTEXTS.get_mut()[CTX_IDX];
    let server_ud = UD::new(ctx).unwrap();
    ctrl.reg_ud(QD_HINT as usize, server_ud);
    println!("create UD success");

    RCTRL_READY = true;


    while crate::RUNNING {
        // do nothing
    }
    0
}

pub unsafe extern "C" fn client_thread(
    _data: *mut linux_kernel_module::c_types::c_void,
) -> linux_kernel_module::c_types::c_int {
    while !RCTRL_READY {};

    let ctx: &mut RContext = &mut crate::ALLRCONTEXTS.get_mut()[CTX_IDX];
    let sa_client = &mut crate::SA_CLIENT.get_mut();

    // preparation
    let mut explorer = IBExplorer::new();

    let gid_addr = ctx.get_gid_as_string();
    let _ = explorer.resolve(1, &mut crate::ALLRCONTEXTS.get_mut()[CTX_IDX], &gid_addr, sa_client.get_inner_sa_client());

    let path_res = explorer.get_path_result().unwrap();
    // param
    let mut ud = UD::new(ctx).unwrap();
    let mut sidr_cm = SidrCM::new(ctx, core::ptr::null_mut()).unwrap();
    // sidr handshake. Only provide the `path` and the `service_id`
    let remote_info = sidr_cm.sidr_connect(
        path_res, REMOTE_SERVICE_ID as u64, QD_HINT as u64);

    if remote_info.is_err() {
        return 0;
    }
    let mqp = unsafe { Arc::get_mut_unchecked(&mut ud) };
    // let mut point = remote_info.unwrap();
    let (mut point, dct_meta) = remote_info.unwrap();

    let alloc_sz = 1024 * 1024 * 4;
    let sz = OP_SIZE as u64;
    // ========== Memory Region ===================================
    let mut test_mr = RMemPhy::new(1024 * 1024 * 2);
    let lkey = unsafe { ctx.get_lkey() } as u32;// lkey
    let local_pa = test_mr.get_pa(0) as u64;    // local address

    // try vmalloc todo: fix this problem!
    // let mut l_ptr: Box<[core::mem::MaybeUninit<i8>], VmallocAllocator> =
    //     Box::new_zeroed_slice_in(alloc_sz as usize, VmallocAllocator);
    // let l_pblock = RMemVirt::create(ctx, l_ptr[0].as_mut_ptr(), alloc_sz);
    // let local_pa = l_pblock.get_dma_buf();    // local address
    // let lmr = l_pblock.get_inner_mr();
    // let lkey = unsafe { (*lmr).lkey } as u32;
    // send_reg_mr(mqp.get_qp(), lmr, DEFAULT_MR_ACCESS_FLAGS);
    // point.bind_pd(ctx.get_pd());
    // ================= start to send message =====================
    let mut send_op = UDOp::<32>::new(mqp); // recv buffer could be small at send side
    let op_count = 500000;
    let mut random: [u8; 1] = [0x0; 1];
    let timer: KTimer = KTimer::new();

    for _ in 0..op_count {
        compiler_fence(Ordering::SeqCst);
        // let _ = getrandom(&mut random);
        let _ = send_op.send(local_pa as u64,
                                lkey, &point, sz as usize);
    }
    let passed_usec = timer.get_passed_usec() as u64;

    let lat_per_op: f64 = passed_usec as f64 / (op_count as f64);    // latency
    let thpt: f64 = op_count as f64 / (passed_usec) as f64 * (1000 * 1000) as f64;
    println!(
        "[single thread] benchmark result:\n thpt:[ {:.5} ] op/s\tlat:[ {:.5} ] us",
        thpt, lat_per_op
    );
    TOTAL_OP += op_count;
    TOTAL_PASSED_US += passed_usec;

    0
}