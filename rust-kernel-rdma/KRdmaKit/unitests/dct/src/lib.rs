#![no_std]
#![feature(get_mut_unchecked, allocator_api)]
#[warn(non_snake_case)]
#[warn(dead_code)]
#[warn(unused_must_use)]
extern crate alloc;

pub mod console_msgs;

use rust_kernel_rdma_base::*;
use KRdmaKit::rust_kernel_rdma_base;
// rust_kernel_rdma_base
use rust_kernel_rdma_base::linux_kernel_module;
// linux_kernel_module
use linux_kernel_module::{c_types, println};
use KRdmaKit::ctrl::RCtrl;
use KRdmaKit::device::{RContext, RNIC};
use KRdmaKit::ib_path_explorer::IBExplorer;
use KRdmaKit::mem::{Memory, RMemPhy};
use KRdmaKit::qp::*;
use KRdmaKit::thread_local::ThreadLocal;


use alloc::vec::Vec;

use alloc::sync::Arc;
use lazy_static::lazy_static;

type UnsafeGlobal<T> = ThreadLocal<T>;

struct DCTTestModule {}

lazy_static! {
    static ref ALLNICS: UnsafeGlobal<Vec<RNIC>> = UnsafeGlobal::new(Vec::new());
    static ref ALLRCONTEXTS: UnsafeGlobal<Vec<RContext<'static>>> = UnsafeGlobal::new(Vec::new());
}

unsafe extern "C" fn _add_one(dev: *mut ib_device) {
    let nic = RNIC::create(dev, 1);
    ALLNICS.get_mut().push(nic.ok().unwrap());
}

gen_add_dev_func!(_add_one, _new_add_one);

unsafe extern "C" fn _remove_one(dev: *mut ib_device, _client_data: *mut c_types::c_void) {
    println!("remove one dev {:?}", dev);
}

/*
static mut CLIENT: ib_client = ib_client {
    name: b"raw-kernel-rdma-unit-test\0".as_ptr() as *mut c_types::c_char,
    add: Some(_add_one),
    remove: Some(_remove_one),
    get_net_dev_by_params: None,
    list: rust_kernel_rdma_base::bindings::list_head::new_as_nil(),
}; */

static mut CLIENT: Option<ib_client> = None;

unsafe fn get_global_client() -> &'static mut ib_client {
    match CLIENT {
        Some(ref mut x) => &mut *x,
        None => panic!(),
    }
}

fn print_test_msgs(test_case_idx: usize, assert: bool) {
    if assert {
        println!("{:?}", crate::console_msgs::SUCC[test_case_idx]);
    } else {
        println!("{:?}", crate::console_msgs::ERR[test_case_idx]);
    }
}

fn ctx_init() {
    // register
    unsafe {
        CLIENT = Some(core::mem::MaybeUninit::zeroed().assume_init());
        get_global_client().name = b"kRdmaKit-unit-test\0".as_ptr() as *mut c_types::c_char;
        get_global_client().add = Some(_new_add_one);
        get_global_client().remove = Some(_remove_one);
        get_global_client().get_net_dev_by_params = None;
    }

    let _ = unsafe { ib_register_client(get_global_client() as *mut ib_client) };

    print_test_msgs(0, ALLNICS.len() > 0);

    // create all of the context according to NIC
    for i in 0..ALLNICS.len() {
        ALLRCONTEXTS
            .get_mut()
            .push(RContext::create(&mut ALLNICS.get_mut()[i]).unwrap());
        println!("create [{}] success", i);
    }
}


use KRdmaKit::Profile;
use KRdmaKit::cm::{EndPoint, SidrCM};
use KRdmaKit::consts::DEFAULT_RPC_HINT;

// DCT uses an RC to register the memory
#[inline]
fn test_dct_case() -> () {
    println!(">>>>>> start [test_case_dct] <<<<<<<");
    let mut explorer = IBExplorer::new();

    let ctx_idx = 0;
    let remote_service_id = 8;
    let qd_hint = DEFAULT_RPC_HINT;
    let ctx: &mut RContext = &mut ALLRCONTEXTS.get_mut()[ctx_idx];
    let gid_addr = ctx.get_gid_as_string();
    if explorer.resolve_v2(remote_service_id, ctx, &gid_addr).is_err() {
        println!("resolve path error");
        return;
    };
    let path_res = explorer.get_path_result().unwrap();

    // contexts
    let ctx: &mut RContext = &mut ALLRCONTEXTS.get_mut()[ctx_idx];

    let _ctrl =
        RCtrl::create(remote_service_id as u64, &mut ALLRCONTEXTS.get_mut()[ctx_idx]).unwrap();

    let mut sidr_cm = SidrCM::new(ctx, core::ptr::null_mut()).unwrap();
    // sidr handshake. Only provide the `path` and the `service_id`
    let remote_info = sidr_cm.sidr_connect(
        path_res, remote_service_id as u64, qd_hint as u64);

    if remote_info.is_err() {
        return;
    }

    let point = remote_info.unwrap();
    // create DC
    let dc = DC::new(ctx);
    if dc.is_none() {
        println!("[dct] err create dc qp.");
        return;
    }

    println!("create dc success");

    let mut dc = dc.unwrap();
    let mdc = unsafe { Arc::get_mut_unchecked(&mut dc) };

    // 1. generate MR
    let mut test_mr = RMemPhy::new(1024 * 1024);

    // 2. get all of the params for later operation
    let lkey = unsafe { ctx.get_lkey() } as u32;
    let rkey = lkey as u32;
    let local_pa = test_mr.get_pa(0) as u64; // use pa for RCOp
    let remote_pa = test_mr.get_pa(1024) as u64;
    let _sz = 64 as usize;

    // va of the mr. These addrs are used to read/write value in test function
    let va_buf = (test_mr.get_ptr() as u64 + 1024) as *mut i8;
    let target_buf = (test_mr.get_ptr()) as *mut i8;

    // now write a value
    let write_value = 101 as i8;
    unsafe { *(va_buf) = write_value };
    unsafe { *(target_buf) = 73 };
    //    println!("[before] value = {}", unsafe { *(target_buf) });

    let mut dc_op = DCOp::new(mdc);

    let mut profile = Profile::new();
    profile.reset_timer();
    let running_cnt = 20000;

    for _i in 0..running_cnt {
        let _ = dc_op.push(ib_wr_opcode::IB_WR_RDMA_READ,
                           local_pa, lkey, 1024, remote_pa, rkey, &point,
                           ib_send_flags::IB_SEND_SIGNALED);

        let mut cnt = 0;
        loop {
            let ret = dc_op.pop();
            if ret.is_some() {
                break;
            }
            cnt += 1;
            if cnt > 1000 {
                break;
            }
        }
    }
    profile.tick_record(0);
    profile.increase_op(running_cnt);

    profile.report(1);
    return;
}

fn test_two_sided_dct() -> () {
    let mut explorer = IBExplorer::new();
    let ctx_idx = 0;
    let qd_hint = DEFAULT_RPC_HINT;
    let remote_service_id = 52;
    let local_service_id = 50;
    let ctx = &mut ALLRCONTEXTS.get_mut()[ctx_idx];
    let server_ctrl =
        RCtrl::create(remote_service_id as u64, ctx).unwrap();
    let mut client_ctr =
        RCtrl::create(local_service_id as u64, ctx).unwrap();

    {
        let gid_addr = ctx.get_gid_as_string();
        let _ = explorer.resolve_v2(remote_service_id, ctx, &gid_addr);
    }

    let path_res = explorer.get_path_result().unwrap();

    let srq = server_ctrl.get_srq();
    let mut sidr_cm = SidrCM::new(ctx, core::ptr::null_mut()).unwrap();
    // sidr handshake. Only provide the `path` and the `service_id`
    let remote_info = sidr_cm.sidr_connect(
        path_res, remote_service_id as u64, qd_hint as u64);

    let point = remote_info.unwrap();
    // create DC
    let client_dc = client_ctr.get_dc_mut().unwrap();
    let mut clientdc_op = DCOp::new(client_dc.as_ref());

    let mut test_mr = RMemPhy::new(1024 * 1024);
    let lkey = unsafe { ctx.get_lkey() } as u32;
    let rkey = lkey as u32;
    let local_pa = test_mr.get_pa(0) as u64; // use pa for RCOp
    let remote_pa = test_mr.get_pa(1024) as u64;
    let _sz = 64 as usize;

    // va of the mr. These addrs are used to read/write value in test function
    let remote_va = (test_mr.get_ptr() as u64 + 1024) as *mut i8;
    let local_va = (test_mr.get_ptr()) as *mut i8;

    unsafe { (*remote_va) = 32 };
    unsafe { (*local_va) = 22 };

    // server post recv
    {
        let mut wr: ib_recv_wr = Default::default();
        let mut sge: ib_sge = Default::default();
        let mut bad_wr: *mut ib_recv_wr = core::ptr::null_mut();
        sge.addr = remote_pa as u64;
        sge.length = 512 as u32;
        sge.lkey = lkey as u32;
        // 1. reset the wr
        wr.sg_list = &mut sge as *mut _;
        wr.num_sge = 1;

        let err = unsafe {
            bd_ib_post_srq_recv(
                srq,
                &mut wr as *mut _,
                &mut bad_wr as *mut _,
            )
        };
        if err != 0 {
            println!("error while post srq recv");
        }
    }
    println!("before value:{}", unsafe { *remote_va });
    // client push request
    {
        let _ = clientdc_op.push_with_imm(
            ib_wr_opcode::IB_WR_RDMA_WRITE_WITH_IMM,
            // ib_wr_opcode::IB_WR_RDMA_READ,
            local_pa, lkey, 12, remote_pa, rkey,
            &point, ib_send_flags::IB_SEND_SIGNALED, 1024);

        let mut cnt = 0;
        loop {
            let ret = clientdc_op.pop();
            if ret.is_some() {
                break;
            }
            cnt += 1;
            if cnt > 5000 {
                println!("time out");
                break;
            }
        }
    }

    // server pop msg
    {
        println!("after value:{}", unsafe { *remote_va });
        let recv_cq: *mut ib_cq = server_ctrl.get_dct_cq();
        if recv_cq.is_null() {
            println!("null recv cq!");
            return;
        }
        let mut wc: ib_wc = Default::default();
        // let ret = unsafe { bd_ib_poll_cq(recv_cq, 1, &mut wc as *mut ib_wc) };
        // if ret < 0 {
        //     println!("error!")
        // } else if ret == 1 {
        //     println!("receve success!!!");
        // } else {
        //     println!("None");
        // }
    }
}

use crate::rust_kernel_linux_util::bindings::bd_kthread_run;
use KRdmaKit::rust_kernel_rdma_base::linux_kernel_module::c_types::*;

#[repr(C)]
#[derive(Copy, Clone, Debug, Default)]
struct RCConnectParam {
    pub vid: c_int,
    pub port: c_int,
}

unsafe extern "C" fn bg_thread(
    data: *mut linux_kernel_module::c_types::c_void,
) -> linux_kernel_module::c_types::c_int {
    let val = &mut (*(data as *mut RCConnectParam));
    linux_kernel_module::println!("{:?}", val);
    0
}

fn test_dct_bg_thread() {
    println!(">>>>>> start [test_dct_bg_thread] <<<<<<<");
    // prepare for the pointer

    let mut data = RCConnectParam {
        vid: 3222,
        port: 12412,
    };


    let t_bg = unsafe {
        bd_kthread_run(
            Some(bg_thread),
            (&mut data as *mut RCConnectParam).cast::<c_void>(),
            b"bg thread\0".as_ptr() as *const i8,
        )
    };
    assert!(!t_bg.is_null());

    return;
}

fn test_dct_bench() {
    let ctx_idx = 0;
    let remote_service_id = 8;
    let mut _ctrl =
        RCtrl::create(remote_service_id as u64, &mut ALLRCONTEXTS.get_mut()[ctx_idx]).unwrap();
    let ctx: &mut RContext = &mut ALLRCONTEXTS.get_mut()[ctx_idx];
    let pd = ctx.get_pd();
    let device = ctx.get_raw_dev();
    let config: Config = Default::default();
    // create CQ
    let mut cq_attr: ib_cq_init_attr = Default::default();
    cq_attr.cqe = config.max_cqe as u32;

    let cq = unsafe {
        ib_create_cq(
            device,
            None,
            None,
            core::ptr::null_mut(),
            &mut cq_attr as *mut _,
        )
    };

    // Recv CQ
    let recv_cq = unsafe {
        ib_create_cq(
            device,
            None,
            None,
            core::ptr::null_mut(),
            &mut cq_attr as *mut _,
        )
    };

    // srq
    let mut cq_attr: ib_srq_init_attr = Default::default();
    cq_attr.attr.max_wr = config.max_send_wr_sz as u32;
    cq_attr.attr.max_sge = config.max_send_sge as u32;

    let srq = unsafe { ib_create_srq(pd, &mut cq_attr as _) };
    let mut profile = Profile::new();
    let run_cnt = 100000;

    for i in 0..run_cnt {
        profile.reset_timer();

        let mut dctattr: ib_dct_init_attr = Default::default();
        dctattr.pd = pd;
        dctattr.cq = cq;
        dctattr.srq = srq;
        dctattr.dc_key = 73;
        dctattr.port = 1;
        dctattr.access_flags =
            ib_access_flags::IB_ACCESS_LOCAL_WRITE |
                ib_access_flags::IB_ACCESS_REMOTE_READ |
                ib_access_flags::IB_ACCESS_REMOTE_WRITE |
                ib_access_flags::IB_ACCESS_REMOTE_ATOMIC;
        dctattr.min_rnr_timer = 2;
        dctattr.tclass = 0;
        dctattr.flow_label = 0;
        dctattr.mtu = ib_mtu::IB_MTU_4096;
        dctattr.pkey_index = 0;
        dctattr.hop_limit = 1;
        dctattr.inline_size = 60;
        dctattr.create_flags = 0;

        let dct = unsafe { safe_ib_create_dct(pd, &mut dctattr as _) };

        profile.tick_record(0);
        // unsafe { ib_exp_destroy_dct(dct, core::ptr::null_mut()); }
        profile.tick_record(1);
        println!("created {}, dct num:{}", i, unsafe { (*dct).dct_num });
    };
    profile.increase_op(run_cnt);
    profile.report(2);

    // // Request
    // {
    //     let mut point: EndPoint = EndPoint::new(pd, 0, 0,
    //                                             ctx.get_lid(), ctx.get_gid(),
    //                                             unsafe { (*dct).dct_num },
    //                                             &_ctrl.get_self_test_mr());
    //     let dc = DC::new(ctx);
    //     if dc.is_none() {
    //         println!("[dct] err create dc qp.");
    //         return;
    //     }
    //
    //     println!("create dc success");
    //
    //     let mut dc = dc.unwrap();
    //     let mdc = unsafe { Arc::get_mut_unchecked(&mut dc) };
    //
    //     // 1. generate MR
    //     let mut test_mr = RMemPhy::new(1024 * 1024);
    //
    //     // 2. get all of the params for later operation
    //     let lkey = unsafe { ctx.get_lkey() } as u32;
    //     let rkey = lkey as u32;
    //     let local_pa = test_mr.get_pa(0) as u64; // use pa for RCOp
    //     let remote_pa = test_mr.get_pa(1024) as u64;
    //     let _sz = 64 as usize;
    //
    //     // va of the mr. These addrs are used to read/write value in test function
    //     let va_buf = (test_mr.get_ptr() as u64 + 1024) as *mut i8;
    //     let target_buf = (test_mr.get_ptr()) as *mut i8;
    //
    //     // now write a value
    //     let write_value = 101 as i8;
    //     unsafe { *(va_buf) = write_value };
    //     unsafe { *(target_buf) = 73 };
    //     //    println!("[before] value = {}", unsafe { *(target_buf) });
    //
    //     let mut dc_op = DCOp::new(mdc);
    //
    //     let mut profile = Profile::new();
    //     profile.reset_timer();
    //     let running_cnt = 20000;
    //
    //     for _i in 0..running_cnt {
    //         let _ = dc_op.push(ib_wr_opcode::IB_WR_RDMA_READ,
    //                            local_pa, lkey, 1024, remote_pa, rkey, &point,
    //                            ib_send_flags::IB_SEND_SIGNALED);
    //
    //         let mut cnt = 0;
    //         loop {
    //             let ret = dc_op.pop();
    //             if ret.is_some() {
    //                 break;
    //             }
    //             cnt += 1;
    //             if cnt > 1000 {
    //                 break;
    //             }
    //         }
    //     }
    //     profile.tick_record(0);
    //     profile.increase_op(running_cnt);
    //
    //     profile.report(1);
    // }


    {
        let mut profile = Profile::new();
        unsafe {
            profile.reset_timer();
            // ib_exp_destroy_dct(dct, core::ptr::null_mut());
            profile.tick_record(0);
            profile.increase_op(1);
            ib_destroy_srq(srq);
            ib_free_cq(cq);
            ib_free_cq(recv_cq);
        };
        profile.report(1);
    }
}

impl linux_kernel_module::KernelModule for DCTTestModule {
    fn init() -> linux_kernel_module::KernelResult<Self> {
        ctx_init();

        // for DCT tests
        test_dct_case();
        // test_two_sided_dct();
        // test_dct_bench();
        Ok(Self {})
    }
}

impl Drop for DCTTestModule {
    fn drop(&mut self) {
        ALLRCONTEXTS.get_mut().clear();
        ALLNICS.get_mut().clear();
        unsafe { ib_unregister_client(get_global_client() as *mut ib_client) };
    }
}

linux_kernel_module::kernel_module!(
    DCTTestModule,
    author: b"lfm",
    description: b"A unit test for qp basic creation",
    license: b"GPL"
);
