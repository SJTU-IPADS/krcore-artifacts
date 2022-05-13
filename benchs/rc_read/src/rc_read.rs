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
use KRdmaKit::qp::{RC, RCOp, send_reg_mr};
use KRdmaKit::ib_path_explorer::IBExplorer;
use KRdmaKit::ctrl::RCtrl;
use KRdmaKit::mem::{RMemVirt, RMemPhy};
use KRdmaKit::random;
use rust_kernel_linux_util::timer::KTimer;
use core::sync::atomic::compiler_fence;
use core::sync::atomic::Ordering;

use alloc::boxed::Box;
use rust_kernel_linux_util::bindings::{bd_ssleep};

use alloc::sync::Arc;

use crate::{TOTAL_OP, TOTAL_PASSED_US};

static mut RCTRL_READY: bool = false;
static mut RUNNING: bool = true;


const DEFAULT_MR_ACCESS_FLAGS: u32 = ib_access_flags::IB_ACCESS_LOCAL_WRITE |
    ib_access_flags::IB_ACCESS_REMOTE_READ |
    ib_access_flags::IB_ACCESS_REMOTE_WRITE |
    ib_access_flags::IB_ACCESS_REMOTE_ATOMIC;

pub unsafe extern "C" fn server_thread(
    _data: *mut linux_kernel_module::c_types::c_void,
) -> linux_kernel_module::c_types::c_int {
    println!("server thread come in");

    let _ctrl =
        RCtrl::create(crate::REMOTE_SERVICE_ID,
                      &mut crate::ALLRCONTEXTS.get_mut()[crate::CTX_IDX]).unwrap();

    RCTRL_READY = true;
    RUNNING = true;
    for i in 0..crate::RUNNING_SECS {
        // do nothing
        unsafe { bd_ssleep(1) };
    }
    RUNNING = false;
    unsafe { bd_ssleep(1) };
    println!("server thread out");

    0
}

use core::cmp::min;

pub unsafe extern "C" fn client_thread(
    _data: *mut linux_kernel_module::c_types::c_void,
) -> linux_kernel_module::c_types::c_int {
    println!("client thread come in");
    while !RCTRL_READY {
        // wait till remote RCtrl is ready
    }
    let ctx: &mut RContext = &mut crate::ALLRCONTEXTS.get_mut()[crate::CTX_IDX];
    let sa_client = &mut crate::SA_CLIENT.get_mut();
    let gid_addr = ctx.get_gid_as_string();
    let mut explorer = IBExplorer::new();

    let _ = explorer.resolve(crate::REMOTE_SERVICE_ID as u64, ctx, &gid_addr, sa_client.get_inner_sa_client());

    // thus the path resolver could be split out of other module
    let path_res = explorer.get_path_result().unwrap();
    let rc = RC::new(ctx, core::ptr::null_mut(), null_nut());

    let mut rc = rc.unwrap();
    let mrc = unsafe { Arc::get_mut_unchecked(&mut rc) };
    // get path
    let boxed_ctx = Box::pin(ctx);

    // connect to the server (Ctrl)
    let _ = mrc.connect(crate::QD_HINT, path_res, crate::REMOTE_SERVICE_ID as u64);

    // 1. generate MR
    let alloc_sz = crate::MR_SIZE;
    let sz = crate::OP_SIZE as u64;

    let mut l_ptr: Box<[core::mem::MaybeUninit<i8>], VmallocAllocator> =
        Box::new_zeroed_slice_in(alloc_sz as usize, VmallocAllocator);
    let mut lkey = 0;
    let mut rkey = 0;
    let mut local_pa = 0;
    let mut remote_pa = 0;
    let v_mem = RMemVirt::create(&boxed_ctx, l_ptr[0].as_mut_ptr(), alloc_sz);
    let mut p_mem = RMemPhy::new(min(1024 * 1024 * 4, alloc_sz as usize));

    if crate::USE_VMALLOC {
        let lmr = v_mem.get_inner_mr();
        send_reg_mr(mrc.get_qp(), v_mem.get_inner_mr(), DEFAULT_MR_ACCESS_FLAGS);
        rkey = unsafe { (*v_mem.get_inner_mr()).rkey } as u32;
        lkey = unsafe { (*v_mem.get_inner_mr()).lkey } as u32;
        local_pa = v_mem.get_dma_buf();
        remote_pa = v_mem.get_dma_buf();
    } else {
        let mut ret = RMemPhy::new(min(1024 * 1024 * 4, alloc_sz as usize));
        lkey = unsafe { boxed_ctx.get_lkey() } as u32;
        rkey = unsafe { boxed_ctx.get_lkey() } as u32;
        local_pa = p_mem.get_dma_buf();
        remote_pa = p_mem.get_dma_buf();
    };


    // 3. send request of type `READ`
    let mut op = RCOp::new(mrc);

    let mut op_count: u64 = 0;
    let mut rand = random::FastRandom::new(0xdeadbeaf);

    // XD: maybe change to rsync timer for higher accuracy? (I think rlib uses rsync timer)
    let timer: KTimer = KTimer::new();

    // XD: maybe using the running flag, not the op count
    // try match the measurements of rlib
    let _mod = alloc_sz / sz;
    while RUNNING {
        compiler_fence(Ordering::SeqCst);
        let offset = sz * (rand.get_next() % _mod);
        let _ = op.read(local_pa,
                        lkey,
                        sz as usize,
                        remote_pa + offset,
                        rkey);
        op_count += 1;
    }
    compiler_fence(Ordering::SeqCst);
    let passed_usec = timer.get_passed_usec() as u64;

    let lat_per_op: f64 = passed_usec as f64 / (op_count as f64);    // latency
    let thpt: f64 = op_count as f64 / (passed_usec) as f64 * (1000 * 1000) as f64;
    println!(
        "benchmark result:\n thpt:[ {:.5} ] op/s\tlat:[ {:.5} ] us",
        thpt, lat_per_op
    );
    TOTAL_OP += op_count;
    TOTAL_PASSED_US += passed_usec;
    println!("client thread out");
    0
}