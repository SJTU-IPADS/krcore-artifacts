#![no_std]
#![feature(get_mut_unchecked, allocator_api)]
#[warn(non_snake_case)]
#[warn(dead_code)]
extern crate alloc;

pub mod console_msgs;

use alloc::boxed::Box;
use alloc::sync::Arc;
use alloc::vec::Vec;
use KRdmaKit::cm::SidrCM;
use KRdmaKit::ctrl::RCtrl;
use KRdmaKit::device::{RContext, RNIC};
use KRdmaKit::ib_path_explorer::IBExplorer;
use KRdmaKit::mem::RMemVirt;
use KRdmaKit::qp::{Config, get_qp_status, RC, RCOp, UD, UDOp, send_reg_mr};
use KRdmaKit::rust_kernel_rdma_base;
use KRdmaKit::SAClient;
use KRdmaKit::thread_local::ThreadLocal;
use lazy_static::lazy_static;
// linux_kernel_module
use linux_kernel_module::{c_types, println};
// rust_kernel_rdma_base
use rust_kernel_rdma_base::*;
use rust_kernel_rdma_base::linux_kernel_module;
use KRdmaKit::mem::{RMemPhy, Memory};


type UnsafeGlobal<T> = ThreadLocal<T>;

struct RegMRTestModule {}

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


// path resolve
fn test_case_rc_reg() {
    println!(">>>>>>>>> start testcase rc reg <<<<<<<<");
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
    let ctrl = RCtrl::create(remote_service_id, &mut ALLRCONTEXTS.get_mut()[0]).unwrap();

    // create RC at one side
    let rc = RC::new(&mut ALLRCONTEXTS.get_mut()[0], core::ptr::null_mut(), core::ptr::null_mut());

    let mut rc = rc.unwrap();
    let mrc = unsafe { Arc::get_mut_unchecked(&mut rc) };
    // connect to the server (Ctrl)
    let _ = mrc.connect(qd_hint + 1, path_res, remote_service_id as u64);

    // 1. generate MR
    let alloc_sz = 1024 * 1024 * 90;
    let mut ptr: Box<[core::mem::MaybeUninit<i8>], VmallocAllocator> =
        Box::new_zeroed_slice_in(alloc_sz as usize, VmallocAllocator);
    let malloc_ptr = ptr[0].as_mut_ptr();
    let sz = 1024 * 1024 * 10 as u64;

    let l_pblock = RMemVirt::create(&boxed_ctx, malloc_ptr, alloc_sz);

    // 2. get all of the params for later operation
    let local_pa = l_pblock.get_dma_buf();
    let remote_pa = local_pa + sz;

    let lbuf = malloc_ptr as u64;
    let rbuf = lbuf + sz;

    let lmr = l_pblock.get_inner_mr();

    let lkey = unsafe { (*lmr).lkey } as u32;
    let rkey = unsafe { (*lmr).rkey } as u32;

    let local_va = (lbuf as u64) as *mut i8;
    let remote_va = (rbuf as u64) as *mut i8;

    send_reg_mr(mrc.get_qp(), lmr, ib_access_flags::IB_ACCESS_LOCAL_WRITE |
        ib_access_flags::IB_ACCESS_REMOTE_READ |
        ib_access_flags::IB_ACCESS_REMOTE_WRITE, false);

    let ptr = remote_va as *mut i8;
    unsafe { (*ptr) = 99 as i8 };

    // 3. send request of type `READ`
    let mut op = RCOp::new(&rc);
    println!("[before] value = {}", unsafe { *(local_va) });

    let _ = op.read(local_pa, lkey, sz as usize, remote_pa, rkey);

    println!("[after] value = {}", unsafe { *(local_va) });
    // print_test_msgs(0, unsafe { *(local_va) } == write_value);
}

fn test_case_ud_reg() {
    println!(">>>>>>>>> start testcase ud reg <<<<<<<<");
    let ctx_idx = 0;
    let ctx: &mut RContext = &mut ALLRCONTEXTS.get_mut()[ctx_idx];
    let sa_client = &mut SA_CLIENT.get_mut();
    let reg_flags = ib_access_flags::IB_ACCESS_LOCAL_WRITE |
        ib_access_flags::IB_ACCESS_REMOTE_READ |
        ib_access_flags::IB_ACCESS_REMOTE_WRITE;
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
    let mud = unsafe { Arc::get_mut_unchecked(&mut ud) };
    let raw_ud = mud.get_qp();
    // ================= start to send message =====================
    let mut point = remote_info.unwrap();

    let alloc_sz = 1024;
    let mut ptr: Box<[core::mem::MaybeUninit<i8>], VmallocAllocator> =
        Box::new_zeroed_slice_in(alloc_sz as usize, VmallocAllocator);
    let malloc_ptr = ptr[0].as_mut_ptr();
    let sz = 64 as u64;

    let l_pblock = RMemVirt::create(&mut ALLRCONTEXTS.get_mut()[0], malloc_ptr, alloc_sz);

    // 2. get all of the params for later operation
    let laddr = l_pblock.get_dma_buf();
    let raddr = laddr + sz;

    let lbuf = malloc_ptr as u64;
    let rbuf = lbuf + sz;

    let lmr = l_pblock.get_inner_mr();
    let lkey = unsafe { (*lmr).lkey } as u32;
    let rkey = unsafe { (*lmr).rkey } as u32;

    // va of the mr. These addrs are used to read/write value in test function
    let va_buf = (lbuf as u64) as *mut i8;
    let target_buf = (rbuf as u64) as *mut i8;

    {
        send_reg_mr(raw_ud, lmr, reg_flags, false);
        point.bind_pd(l_pblock.get_mr_pd());

        // let rc = RC::new(&mut ALLRCONTEXTS.get_mut()[0], core::ptr::null_mut());
        //
        // let mut rc = rc.unwrap();
        // let mrc = unsafe { Arc::get_mut_unchecked(&mut rc) };
        // send_reg_mr(mrc.get_qp(), lmr, reg_flags, false);
    }


    // now write a value
    let write_value = 122 as i8;
    unsafe { *(va_buf) = write_value };  // client side , set value 4

    for i in 0..64 {
        let buf = (target_buf as u64 + i as u64) as *mut i8;
        unsafe { *(buf) = 0 };
    }
    unsafe { *(target_buf) = 4 };
    println!("[before] value = {}", unsafe { *(target_buf) });
    const RECV_ENTRY_COUNT: usize = 1;
    let recv_entry_size: u64 = 1;

    let mut send_op = UDOp::<32>::new(mud); // recv buffer could be small at send side

    let mut ud = ctrl.get_ud(qd_hint as usize);
    if ud.is_some() {
        let qp = unsafe { Arc::get_mut_unchecked(ud.as_mut().unwrap()) };
        // send reg mr
        let mut recv_op = UDOp::<2048>::new(qp);

        // let _ = recv_op.post_recv_buffer(raddr, lkey, recv_entry_size as usize, // entry size
        //                                  RECV_ENTRY_COUNT); // entry count

        // send 16 request at once
        for _ in 0..RECV_ENTRY_COUNT {
            let _ = send_op.send(laddr as u64,
                                 lkey, &point, sz as usize);
        }
        // let _ = recv_op.wait_recv_cq(RECV_ENTRY_COUNT);
    }
}


impl linux_kernel_module::KernelModule for RegMRTestModule {
    fn init() -> linux_kernel_module::KernelResult<Self> {
        ctx_init();
        test_case_rc_reg();
        // test_case_ud_reg();
        Ok(Self {})
    }
}

impl Drop for RegMRTestModule {
    fn drop(&mut self) {
        unsafe { ib_unregister_client(get_global_client() as *mut ib_client) };
        SA_CLIENT.get_mut().reset();
    }
}


linux_kernel_module::kernel_module!(
    RegMRTestModule,
    author: b"lfm",
    description: b"A unit test for qp basic creation",
    license: b"GPL"
);
