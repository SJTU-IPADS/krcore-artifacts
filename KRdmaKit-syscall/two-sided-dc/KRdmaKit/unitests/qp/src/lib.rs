#![no_std]
#![feature(get_mut_unchecked)]
#[warn(non_snake_case)]
#[warn(dead_code)]
#[warn(unused_must_use)]
extern crate alloc;

pub mod console_msgs;

use KRdmaKit::{rust_kernel_rdma_base, Profile};
// rust_kernel_rdma_base
use rust_kernel_rdma_base::*;
use rust_kernel_rdma_base::linux_kernel_module;
// linux_kernel_module
use linux_kernel_module::{println, c_types};
use KRdmaKit::device::{RNIC, RContext};
use KRdmaKit::qp::{RC, RCOp, UD, UDOp, Config, get_qp_status, RecvHelper};
use KRdmaKit::SAClient;
use KRdmaKit::thread_local::ThreadLocal;
use KRdmaKit::ib_path_explorer::IBExplorer;
use KRdmaKit::ctrl::RCtrl;
use KRdmaKit::cm::SidrCM;
use KRdmaKit::mem::{RMemPhy, Memory, pa_to_va};

use alloc::vec::Vec;
use alloc::boxed::Box;

use lazy_static::lazy_static;
use alloc::sync::Arc;
use core::convert::TryInto;
use core::ptr::null_mut;

type UnsafeGlobal<T> = ThreadLocal<T>;

struct QPTestModule {}

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

static mut CLIENT: Option<ib_client> = None;

unsafe fn get_global_client() -> &'static mut ib_client {
    match CLIENT {
        Some(ref mut x) => &mut *x,
        None => panic!(),
    }
}

/* 
static mut CLIENT: ib_client = ib_client {
    name: b"raw-kernel-rdma-unit-test\0".as_ptr() as *mut c_types::c_char,
    add: Some(_add_one),
    remove: Some(_remove_one),
    get_net_dev_by_params: None,
    list: rust_kernel_rdma_base::bindings::list_head::new_as_nil(),
}; */

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

    let err = unsafe { ib_register_client(get_global_client() as *mut ib_client) };
    print_test_msgs(0, err == 0);
    print_test_msgs(0, ALLNICS.len() > 0);

    // create all of the context according to NIC
    for i in 0..ALLNICS.len() {
        ALLRCONTEXTS.get_mut()
            .push(RContext::create(&mut ALLNICS.get_mut()[i]).unwrap());
        println!("create [{}] success", i);
    }
}


/// Test case for RC qp state change
fn test_case_rc_1() {
    let ctx: &mut RContext = &mut ALLRCONTEXTS.get_mut()[0];
    // sa client
    println!("sa client registered");


    let rc = RC::new(ctx, core::ptr::null_mut(), null_mut());
    if rc.is_some() {
        println!("create RC success");
        // check field getter
        let mut rc = rc.unwrap();
        let mrc = unsafe { Arc::get_mut_unchecked(&mut rc) };

        println!("[1] state:{}", get_qp_status(mrc.get_qp()).unwrap());
    }
}

// test for inner completion waiting
fn test_case_rc_2() {
    let ctx: &mut RContext = &mut ALLRCONTEXTS.get_mut()[0];
    // sa client
    let rc = RC::new(ctx, core::ptr::null_mut(), null_mut());
    if rc.is_some() {
        println!("test_case_2 create RC success");
        // check field getter
        let mut rc = rc.unwrap();
        let _mrc = unsafe { Arc::get_mut_unchecked(&mut rc) };
    }
}


// path resolve
fn test_case_rc_3() {
    println!(">>>>>> start [test_case_rc_3] <<<<<<<");
    let ctx: &mut RContext = &mut ALLRCONTEXTS.get_mut()[0];
    let sa_client = &mut SA_CLIENT.get_mut();
    let mut explorer = IBExplorer::new();
    let remote_service_id = 50;
    let qd_hint = 73;

    let gid_addr = ctx.get_gid_as_string();
    let _ = explorer.resolve(1, ctx, &gid_addr, sa_client.get_inner_sa_client());

    // get path
    let boxed_ctx = Box::pin(ctx);
    // thus the path resolver could be split out of other module
    let path_res = explorer.get_path_result().unwrap();
    println!("finish path get");
    // create Ctrl. serve for cm handshakes
    let ctrl =
        RCtrl::create(remote_service_id, &mut ALLRCONTEXTS.get_mut()[0]).unwrap();

    // create RC at one side

    for i in 10..20
    {
        let rc = RC::new(&mut ALLRCONTEXTS.get_mut()[0], core::ptr::null_mut(), null_mut());
        let mut rc = rc.unwrap();
        let mrc = unsafe { Arc::get_mut_unchecked(&mut rc) };
        // connect to the server (Ctrl)
        let _ = mrc.connect(qd_hint + i, path_res, remote_service_id as u64);
    }
    // after connection, we try to post wr

    let rc = RC::new(&mut ALLRCONTEXTS.get_mut()[0], core::ptr::null_mut(), null_mut());

    let mut rc = rc.unwrap();
    let mrc = unsafe { Arc::get_mut_unchecked(&mut rc) };
    // connect to the server (Ctrl)
    let _ = mrc.connect(qd_hint + 1, path_res, remote_service_id as u64);

    // 1. generate MR
    let mut test_mr = RMemPhy::new(1024 * 1024);
    // 2. get all of the params for later operation
    let lkey = unsafe { boxed_ctx.get_lkey() } as u32;
    let rkey = lkey as u32;
    let laddr = test_mr.get_pa(0) as u64;   // use pa for RCOp
    let raddr = test_mr.get_pa(1024) as u64;
    let sz = 8 as usize;

    // va of the mr. These addrs are used to read/write value in test function
    let va_buf = (test_mr.get_ptr() as u64 + 1024) as *mut i8;
    let target_buf = (test_mr.get_ptr()) as *mut i8;

    // now write a value
    let write_value = 99 as i8;
    unsafe { *(va_buf) = write_value };
    unsafe { *(target_buf) = 4 };
    println!("[before] value = {}", unsafe { *(target_buf) });
    // 3. send request of type `READ`
    let mut op = RCOp::new(&rc);
    let _ = op.read(laddr, lkey, sz, raddr, rkey);
    println!("[after] value = {}", unsafe { *(target_buf) });
    print_test_msgs(0, unsafe { *(target_buf) } == write_value);


    let mut profile = Profile::new();
    profile.reset_timer();
    let running_cnt = 40000;
    for _ in 0..running_cnt {
        op.push(ib_wr_opcode::IB_WR_RDMA_WRITE,
                (unsafe { pa_to_va(laddr as *mut i8) }) as u64, lkey, 32, raddr, rkey,
                ib_send_flags::IB_SEND_SIGNALED | ib_send_flags::IB_SEND_INLINE);

        let mut cnt = 0;
        loop {
            let ret = op.pop();
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
}

// UD creation
fn test_case_ud_1() {
    let ctx_idx = 0;
    let ctx: &mut RContext = &mut ALLRCONTEXTS.get_mut()[ctx_idx];

    let qp = UD::new(ctx);
    if qp.is_some() {
        println!("create UD success");
        // check field getter
        let qp = qp.unwrap();
        println!("qpn:{}", qp.get_qp_num());
    }
}

// UD sidr handshake
fn test_case_ud_2() {
    let ctx_idx = 0;
    let ctx: &mut RContext = &mut ALLRCONTEXTS.get_mut()[ctx_idx];
    let sa_client = &mut SA_CLIENT.get_mut();

    // preparation
    let mut explorer = IBExplorer::new();

    let gid_addr = ctx.get_gid_as_string();
    let _ = explorer.resolve(1, ctx, &gid_addr, sa_client.get_inner_sa_client());

    let path_res = explorer.get_path_result().unwrap();
    // param
    let remote_service_id = 50;
    let qd_hint = 73;

    let mut ctrl = RCtrl::create(remote_service_id, &mut ALLRCONTEXTS.get_mut()[0]).unwrap();
    // end of preparation
    // let cm = SidrCM::new(ctx, core::ptr::null_mut());

    let mut ud = UD::new(ctx).unwrap();
    let server_ud = UD::new(ctx).unwrap();
    ctrl.reg_ud(qd_hint, server_ud);
    println!("create UD success");

    let mut sidr_cm = SidrCM::new(ctx, core::ptr::null_mut()).unwrap();
    // sidr handshake. Only provide the `path` and the `service_id`
    let remote_info = sidr_cm.sidr_connect(
        path_res, remote_service_id as u64, qd_hint as u64);

    if remote_info.is_err() {
        return;
    }
    let mqp = unsafe { Arc::get_mut_unchecked(&mut ud) };
    // ================= start to send message =====================
    let point = remote_info.unwrap();

    // return;
    // let boxed_ctx = Box::pin(ctx);

    // 1. generate MR
    let mut test_mr = RMemPhy::new(1024 * 1024);
    // 2. get all of the params for later operation
    let lkey = unsafe { ctx.get_lkey() } as u32;
    let laddr = test_mr.get_pa(0) as u64;   // use pa for RCOp
    let raddr = test_mr.get_pa(16) as u64;
    let sz = 8 as usize;

    // va of the mr. These addrs are used to read/write value in test function
    let va_buf = (test_mr.get_ptr() as u64) as *mut i8;
    let target_buf = (test_mr.get_ptr() as u64 + 16 as u64) as *mut i8;

    // now write a value
    let write_value = 122 as i8;
    unsafe { *(va_buf) = write_value };  // client side , set value 4

    for i in 0..64 {
        let buf = (target_buf as u64 + i as u64) as *mut i8;
        unsafe { *(buf) = 0 };
    }
    unsafe { *(target_buf) = 4 };

    println!("[before] value = {}", unsafe { *(target_buf) });
    const RECV_ENTRY_COUNT: usize = 512;
    let recv_entry_size: u64 = 64;

    let mut send_op = UDOp::<32>::new(mqp); // recv buffer could be small at send side

    let mut ud = ctrl.get_ud(qd_hint as usize);
    if ud.is_some() {
        let qp = unsafe { Arc::get_mut_unchecked(ud.as_mut().unwrap()) };
        let mut recv_op = UDOp::<2048>::new(qp);
        let _ = recv_op.post_recv_buffer(raddr, lkey, recv_entry_size as usize, // entry size
                                         RECV_ENTRY_COUNT); // entry count

        // send 16 request at once
        for _ in 0..RECV_ENTRY_COUNT {
            let _ = send_op.send(laddr as u64,
                                 lkey, &point, sz as usize);
        }
        let _ = recv_op.wait_recv_cq(RECV_ENTRY_COUNT);
    }

    // result test; You will find the value at offset `40`
    for i in 0..recv_entry_size {
        let buf = target_buf as u64 + i;
        println!("[{}] val:{}", i, unsafe { *(buf as *mut i8) })
    }
}

impl linux_kernel_module::KernelModule for QPTestModule {
    fn init() -> linux_kernel_module::KernelResult<Self> {
        ctx_init();

        // for RC test
        // test_case_rc_1();
        // test_case_rc_2();
        test_case_rc_3();
        // for UD test
        // test_case_ud_1();
        // test_case_ud_2();
        Ok(Self {})
    }
}

impl Drop for QPTestModule {
    fn drop(&mut self) {
        unsafe { ib_unregister_client(get_global_client() as *mut ib_client) };
        SA_CLIENT.get_mut().reset();
    }
}


linux_kernel_module::kernel_module!(
    QPTestModule,
    author: b"lfm",
    description: b"A unit test for qp basic creation",
    license: b"GPL"
);
