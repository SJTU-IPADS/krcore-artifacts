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
use rust_kernel_rdma_base::*;
// linux_kernel_module
use linux_kernel_module::{c_types, println};
use KRdmaKit::ctrl::RCtrl;
use KRdmaKit::device::{RContext, RNIC};
use KRdmaKit::ib_path_explorer::IBExplorer;
use KRdmaKit::mem::{Memory, RMemPhy};
use KRdmaKit::qp::{get_qp_status, send_reg_mr, create_dct_server, create_dc_qp, bring_dc_to_init, bring_dc_to_ready, bring_dc_to_reset, bring_dc_to_rtr, Config, RCOp, RC, DCTServer, DC, DCOp, DCTargetMeta, UD};
use KRdmaKit::thread_local::ThreadLocal;
use KRdmaKit::SAClient;

use KRdmaKit::mem::RMemVirt;

use alloc::boxed::Box;
use alloc::vec::Vec;

use alloc::sync::Arc;
use lazy_static::lazy_static;

type UnsafeGlobal<T> = ThreadLocal<T>;

struct DCTTestModule {}

lazy_static! {
    static ref ALLNICS: UnsafeGlobal<Vec<RNIC>> = UnsafeGlobal::new(Vec::new());
    static ref ALLRCONTEXTS: UnsafeGlobal<Vec<RContext<'static>>> = UnsafeGlobal::new(Vec::new());
    static ref SA_CLIENT: UnsafeGlobal<SAClient> = UnsafeGlobal::new(SAClient::create());
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

use KRdmaKit::rust_kernel_rdma_base::*;

const DC_KEY: u64 = 73;

use crate::rust_kernel_linux_util::timer::KTimer;
use KRdmaKit::Profile;
use KRdmaKit::cm::SidrCM;
use KRdmaKit::consts::DEFAULT_RPC_HINT;

// DCT uses an RC to register the memory
#[inline]
fn test_dct_case() -> () {
    println!(">>>>>> start [test_case_dct] <<<<<<<");
    let mut explorer = IBExplorer::new();

    let ctx_idx = 0;
    let remote_service_id = 12;
    let qd_hint = DEFAULT_RPC_HINT;
    let ctx: &mut RContext = &mut ALLRCONTEXTS.get_mut()[ctx_idx];
    let sa_client = &mut SA_CLIENT.get_mut();
    let gid_addr = ctx.get_gid_as_string();
    let _ = explorer.resolve(remote_service_id, ctx, &gid_addr, sa_client.get_inner_sa_client());

    let path_res = explorer.get_path_result().unwrap();
    // contexts
    let sa_client = &mut SA_CLIENT.get_mut();
    let ctx: &mut RContext = &mut ALLRCONTEXTS.get_mut()[0];

    let ctrl =
        RCtrl::create(remote_service_id as usize, &mut ALLRCONTEXTS.get_mut()[ctx_idx]).unwrap();

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
    let mut dc = dc.unwrap();
    let mdc = unsafe { Arc::get_mut_unchecked(&mut dc) };

    // 1. generate MR
    let mut test_mr = RMemPhy::new(1024 * 1024);
    // 2. get all of the params for later operation
    let lkey = unsafe { ctx.get_lkey() } as u32;
    let rkey = lkey as u32;
    let local_pa = test_mr.get_pa(0) as u64; // use pa for RCOp
    let remote_pa = test_mr.get_pa(1024) as u64;
    let sz = 64 as usize;

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
    for i in 0..running_cnt {
        dc_op.push(ib_wr_opcode::IB_WR_RDMA_READ,
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

#[inline]
fn test_dc_create_latency() {
    let ctx: &mut RContext = &mut ALLRCONTEXTS.get_mut()[1];
    let mut profile = Profile::new();

    use core::ptr::null_mut;
    let (_qp, cq, _) = create_dc_qp(ctx, null_mut(), null_mut());
    if _qp.is_null() {
        println!("[dct] err create dc qp.");
        return;
    }
    if cq.is_null() {
        println!("[dct] err create dc cq.");
        return;
    }
    profile.reset_timer();

    // bring status
    if !bring_dc_to_ready(_qp) {
        println!("[dct] err to bring to ready.");
        return;
    }
    profile.increase_op(1);
    profile.tick_record(0);
    profile.report(1);
}

fn test_two_sided_dct() -> () {
    let mut explorer = IBExplorer::new();
    let ctx_idx = 0;
    let qd_hint = DEFAULT_RPC_HINT;
    let remote_service_id = 52;
    let local_service_id = 50;
    let ctx = &mut ALLRCONTEXTS.get_mut()[ctx_idx];
    let mut server_ctrl =
        RCtrl::create(remote_service_id as usize, ctx).unwrap();
    let mut client_ctr =
        RCtrl::create(local_service_id as usize, ctx).unwrap();

    {
        let sa_client = &mut SA_CLIENT.get_mut();
        let gid_addr = ctx.get_gid_as_string();
        let _ = explorer.resolve(remote_service_id, ctx, &gid_addr, sa_client.get_inner_sa_client());
    }

    let path_res = explorer.get_path_result().unwrap();

    let srq = server_ctrl.get_srq();
    let mut sidr_cm = SidrCM::new(ctx, core::ptr::null_mut()).unwrap();
    // sidr handshake. Only provide the `path` and the `service_id`
    let remote_info = sidr_cm.sidr_connect(
        path_res, remote_service_id as u64, qd_hint as u64);

    let point = remote_info.unwrap();
    // create DC
    let mut client_dc = client_ctr.get_dc_mut().unwrap();
    let mut clientdc_op = DCOp::new(client_dc.as_ref());

    let mut test_mr = RMemPhy::new(1024 * 1024);
    let lkey = unsafe { ctx.get_lkey() } as u32;
    let rkey = lkey as u32;
    let local_pa = test_mr.get_pa(0) as u64; // use pa for RCOp
    let remote_pa = test_mr.get_pa(1024) as u64;
    let sz = 64 as usize;

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
        clientdc_op.push_with_imm(
            ib_wr_opcode::IB_WR_SEND_WITH_IMM,
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

        // let recv_cq = server_ctrl.get_dct_cq();
        // if recv_cq.is_null() {
        //     println!("null recv cq!");
        //     return;
        // }
        //
        // for i in 0..12 {
        //     let mut wc: ib_wc = Default::default();
        //     let ret = unsafe { bd_ib_poll_cq(recv_cq, 1, &mut wc as *mut ib_wc) };
        //     if ret < 0 {
        //         println!("error!")
        //     } else if ret == 1 {
        //         println!("receve success!!!");
        //     } else {
        //         println!("None");
        //     }
        // }

    }
}

use rust_kernel_linux_util::bindings;
use linux_kernel_module::c_types::c_void;

impl linux_kernel_module::KernelModule for DCTTestModule {
    fn init() -> linux_kernel_module::KernelResult<Self> {
        ctx_init();

        // for DCT tests
        // test_dct_case();
        // test_dc_create_latency();
        test_two_sided_dct();
        Ok(Self {})
    }
}

impl Drop for DCTTestModule {
    fn drop(&mut self) {
        ALLRCONTEXTS.get_mut().clear();
        ALLNICS.get_mut().clear();
        unsafe { ib_unregister_client(get_global_client() as *mut ib_client) };
        SA_CLIENT.get_mut().reset();
    }
}

linux_kernel_module::kernel_module!(
    DCTTestModule,
    author: b"lfm",
    description: b"A unit test for qp basic creation",
    license: b"GPL"
);
