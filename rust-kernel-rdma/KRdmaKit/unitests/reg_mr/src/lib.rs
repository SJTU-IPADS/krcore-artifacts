#![no_std]
#![feature(get_mut_unchecked, allocator_api)]
#[warn(non_snake_case)]
#[warn(dead_code)]
extern crate alloc;

pub mod console_msgs;

use alloc::boxed::Box;
use alloc::sync::Arc;
use alloc::vec;
use alloc::vec::Vec;
use core::ptr::null_mut;
use KRdmaKit::cm::SidrCM;
use KRdmaKit::ctrl::RCtrl;
use KRdmaKit::device::{RContext, RNIC};
use KRdmaKit::ib_path_explorer::IBExplorer;
use KRdmaKit::mem::{Memory, RMemPhy, RMemVirt};
use KRdmaKit::qp::*;
use KRdmaKit::{Profile, rust_kernel_rdma_base};
use KRdmaKit::thread_local::ThreadLocal;
use lazy_static::lazy_static;
use KRdmaKit::consts::DEFAULT_RPC_HINT;
// linux_kernel_module
use linux_kernel_module::{c_types, println};
// rust_kernel_rdma_base
use rust_kernel_rdma_base::*;
use rust_kernel_rdma_base::linux_kernel_module;


type UnsafeGlobal<T> = ThreadLocal<T>;

struct RegMRTestModule {}

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
    use KRdmaKit::Profile;
    println!(">>>>>>>>> start testcase rc reg <<<<<<<<");
    let ctx: &mut RContext = &mut ALLRCONTEXTS.get_mut()[0];
    let ctx_mr = ctx.get_kmr();
    let ctx_key = unsafe { ctx.get_lkey() };

    let mut explorer = IBExplorer::new();
    let remote_service_id = 50;
    let qd_hint = 73;
    let gid_addr = ctx.get_gid_as_string();
    let access_flag = ib_access_flags::IB_ACCESS_LOCAL_WRITE |
        ib_access_flags::IB_ACCESS_REMOTE_READ |
        ib_access_flags::IB_ACCESS_REMOTE_WRITE;
    let _ = explorer.resolve_v2(1, ctx, &gid_addr);
    // get path
    let boxed_ctx = Box::pin(ctx);
    // thus the path resolver could be split out of other module
    let path_res = explorer.get_path_result().unwrap();
    println!("finish path get");
    // create Ctrl. serve for cm handshakes
    let ctrl = RCtrl::create(remote_service_id, &mut ALLRCONTEXTS.get_mut()[0]).unwrap();

    // create RC at one side
    let mut rc = RC::new(&mut ALLRCONTEXTS.get_mut()[0], core::ptr::null_mut(), core::ptr::null_mut()).unwrap();

    let mrc = unsafe { Arc::get_mut_unchecked(&mut rc) };

    // connect to the server (Ctrl)
    let _ = mrc.connect(qd_hint, path_res, remote_service_id as u64);
    let server_rc = ctrl.get_rc(qd_hint as usize).unwrap();
    let mut op = RCOp::new(&rc);

    // 1. generate MR
    let alloc_sz = 1024 * 1024 * 128;
    let mut ptr: Box<[core::mem::MaybeUninit<i8>], VmallocAllocator> =
        Box::new_zeroed_slice_in(alloc_sz as usize, VmallocAllocator);
    let mut ptr_x: Box<[core::mem::MaybeUninit<i8>], VmallocAllocator> =
        Box::new_zeroed_slice_in(alloc_sz as usize, VmallocAllocator);
    let malloc_ptr = ptr[0].as_mut_ptr();
    let malloc_ptr_server = ptr_x[0].as_mut_ptr();
    let sz = 64 as u64;

    // Reg mr
    let l_pblock = RMemVirt::create(&boxed_ctx, malloc_ptr, alloc_sz);
    let l_pblock_server = RMemVirt::create(&boxed_ctx, malloc_ptr_server, alloc_sz);
    let lmr = l_pblock.get_inner_mr();
    let lmr_server = l_pblock_server.get_inner_mr();
    let client_key = unsafe { (*lmr).lkey } as u32;
    let server_key = unsafe { (*lmr_server).lkey } as u32;
    println!("server rc qpn:{} , client qpn:{}", server_rc.get_qp_num(), rc.get_qp_num());
    send_reg_mr(server_rc.get_qp(), lmr_server, access_flag, true);
    wait_one_cq(server_rc.get_cq());
    send_reg_mr(rc.get_qp(), lmr, access_flag, true);
    wait_one_cq(rc.get_cq());
    // 2. get all of the params for later operation
    let off: u64 = 1024 * 1024 * 100;
    let local_pa = l_pblock.get_dma_buf();
    let remote_pa = l_pblock_server.get_dma_buf() + off;

    let lbuf = malloc_ptr as u64;
    let rbuf = malloc_ptr_server as u64 + off;

    let local_va = (lbuf as u64) as *mut i8;
    let remote_va = (rbuf as u64) as *mut i8;

    let ptr = remote_va as *mut i8;
    unsafe { (*ptr) = 99 as i8 };

    // 3. send request of type `READ`
    println!("[before] value = {}", unsafe { *(local_va) });

    let _ = op.read(local_pa, client_key, sz as usize,
                    remote_pa, server_key);

    println!("[after] value = {}", unsafe { *(local_va) });
}

fn test_case_ud_reg() {
    println!(">>>>>>>>> start testcase ud reg <<<<<<<<");
    let ctx_idx = 0;
    let ctx: &mut RContext = &mut ALLRCONTEXTS.get_mut()[ctx_idx];
    let ctx_key = unsafe { ctx.get_lkey() };
    let reg_flags = ib_access_flags::IB_ACCESS_LOCAL_WRITE |
        ib_access_flags::IB_ACCESS_REMOTE_READ |
        ib_access_flags::IB_ACCESS_REMOTE_WRITE;
    // preparation
    let mut explorer = IBExplorer::new();

    let gid_addr = ctx.get_gid_as_string();
    let _ = explorer.resolve_v2(1, ctx, &gid_addr);

    let path_res = explorer.get_path_result().unwrap();
    // param
    let remote_service_id = 50;
    let qd_hint = 73;

    let ctrl = RCtrl::create(remote_service_id, &mut ALLRCONTEXTS.get_mut()[0]).unwrap();
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
        let ud = ctrl.get_ud(qd_hint as usize).unwrap();
        send_reg_mr(ud.get_qp(), lmr, reg_flags, false);
    }
    let server_key = unsafe { (*lmr).lkey };


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
    let _recv_entry_size: u64 = 1;

    let mut send_op = UDOp::new(mud); // recv buffer could be small at send side

    let ud = ctrl.get_ud(qd_hint as usize);
    if ud.is_some() {
        // send reg mr
        let mut recv_op = UDOp::new(ud.unwrap());

        recv_op.post_recv(raddr, ctx_key, 512); // fixme: not work using `server_key`
        // recv_op.post_recv(raddr, server_key, 512);

        // send 16 request at once
        for _ in 0..1 {
            let _ = send_op.send(laddr as u64,
                                 ctx_key, &point, sz as usize);
        }
        {
            let mut wcs: ib_wc = Default::default();
            let mut cnt = 0;
            loop {
                let res = recv_op.pop_recv_cq(1, &mut wcs as _);
                if res.is_some() {
                    println!("succ");
                    break;
                } else {
                    cnt += 1;
                    if cnt > 1000 {
                        println!("time out while pop recv cq");
                        break;
                    }
                }
            }
        }
    }
}

const K: usize = 1024;
const M: usize = K * 1024;

fn wait_one_cq(cq: *mut ib_cq) {
    let mut wc: ib_wc = Default::default();
    let mut run_cnt = 0;
    loop {
        let ret = unsafe { bd_ib_poll_cq(cq, 1, &mut wc as *mut ib_wc) };
        if ret < 0 {
            linux_kernel_module::println!("ret < 0");
            break;
        } else if ret == 1 {
            if wc.status != 0 && wc.status != 5 {
                linux_kernel_module::println!("rc poll cq error, wc_status:{}", wc.status);
            }
            break;
        } else {
            run_cnt += 1;
            if run_cnt > 10000 {
                linux_kernel_module::println!("timeout");
                break;
            }
        }
    }
}

fn bench_frwr() {
    use KRdmaKit::Profile;
    let ctx: &mut RContext = &mut ALLRCONTEXTS.get_mut()[0];
    let mut explorer = IBExplorer::new();
    let remote_service_id = 50;
    let qd_hint = 73;
    let gid_addr = ctx.get_gid_as_string();
    let _ = explorer.resolve_v2(1, ctx, &gid_addr);
    // get path
    let boxed_ctx = Box::pin(ctx);
    // thus the path resolver could be split out of other module
    let path_res = explorer.get_path_result().unwrap();
    // create Ctrl. serve for cm handshakes
    let ctrl = RCtrl::create(remote_service_id, &mut ALLRCONTEXTS.get_mut()[0]).unwrap();

    // create RC at one side
    let rc = RC::new(&mut ALLRCONTEXTS.get_mut()[0], core::ptr::null_mut(), core::ptr::null_mut());

    let mut rc = rc.unwrap();
    let mrc = unsafe { Arc::get_mut_unchecked(&mut rc) };
    // connect to the server (Ctrl)
    let _ = mrc.connect(qd_hint + 1, path_res, remote_service_id as u64);


    let sz_list = vec![128, 512, 1 * K, 4 * K, 16 * K, 256 * K, 512 * K,
                       1 * M, 4 * M, 16 * M, 64 * M, 128 * M, 511 * M, 512 * M];

    fn normal_req(rc: &Arc<RC>, ctx: &RContext, l_pblock: &RMemVirt, val: usize) {
        let sz: u64 = 64;
        let lmr = l_pblock.get_inner_mr();
        let local_pa = l_pblock.get_dma_buf();
        let local_va = l_pblock.get_va_buf();
        let remote_pa = local_pa + sz;
        let remote_va = local_va + sz;
        let lkey = unsafe { (*lmr).lkey } as u32;
        let rkey = unsafe { (*lmr).rkey } as u32;
        let k_key = unsafe { ctx.get_lkey() };

        let mut op = RCOp::new(rc);
        println!("[before] value = {}", unsafe { *(local_va as *mut i8) });
        let ptr = remote_va as *mut usize;
        unsafe { (*ptr) = val as usize };
        let _ = op.read(local_pa, k_key,
                        64 as usize, remote_pa, rkey);

        println!("[after] value = {}", unsafe { *(local_va as *mut usize) });
    }

    fn run_single_test(rc: &Arc<RC>, sz: usize) {
        let qp = rc.get_qp();
        let mut profile = Profile::new();
        let en_len = 1 + (sz / 4096);
        let ctx: &mut RContext = &mut ALLRCONTEXTS.get_mut()[0];
        let alloc_sz = sz;
        let running_cnt = 1;
        let flag = ib_access_flags::IB_ACCESS_LOCAL_WRITE |
            ib_access_flags::IB_ACCESS_REMOTE_READ |
            ib_access_flags::IB_ACCESS_REMOTE_WRITE;
        let mut ptr: Box<[core::mem::MaybeUninit<i8>], VmallocAllocator> =
            Box::new_zeroed_slice_in(alloc_sz as usize, VmallocAllocator);
        for _ in 0..running_cnt {
            let mr = unsafe {
                ib_alloc_mr(ctx.get_pd(),
                            ib_mr_type::IB_MR_TYPE_MEM_REG, en_len as u32)
            };
            profile.reset_timer();
            let mut l_pblock = RMemVirt::create_from_mr(ctx, mr, ptr[0].as_mut_ptr(), alloc_sz as u64);
            profile.tick_record(0);
            send_reg_mr(qp, l_pblock.get_inner_mr(), flag, true);
            wait_one_cq(rc.get_cq());
            profile.tick_record(1);
            normal_req(&rc, ctx, &l_pblock, sz);
        }
        profile.increase_op(running_cnt);
        println!("RegMR sz: [ {} ]", sz);
        profile.report(2);
    }
    for sz in sz_list{
        run_single_test(&rc, sz);
    }
}


impl linux_kernel_module::KernelModule for RegMRTestModule {
    fn init() -> linux_kernel_module::KernelResult<Self> {
        ctx_init();
        // test_case_rc_reg();
        // test_case_ud_reg();
        bench_frwr();
        Ok(Self {})
    }
}

impl Drop for RegMRTestModule {
    fn drop(&mut self) {
        unsafe { ib_unregister_client(get_global_client() as *mut ib_client) };
    }
}


linux_kernel_module::kernel_module!(
    RegMRTestModule,
    author: b"lfm",
    description: b"A unit test for qp basic creation",
    license: b"GPL"
);
