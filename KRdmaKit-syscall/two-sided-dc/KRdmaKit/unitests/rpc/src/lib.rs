#![no_std]
#![feature(get_mut_unchecked)]
#[warn(non_snake_case)]
#[warn(dead_code)]
#[warn(unused_must_use)]
extern crate alloc;

pub mod console_msgs;

use KRdmaKit::rust_kernel_rdma_base;
// rust_kernel_rdma_base
use rust_kernel_rdma_base::*;
use rust_kernel_rdma_base::linux_kernel_module;
// linux_kernel_module
use linux_kernel_module::{println, c_types};
use KRdmaKit::device::{RNIC, RContext};
use KRdmaKit::qp::{RC, RCOp, UD, UDOp, Config, get_qp_status, DCTargetMeta};
use KRdmaKit::SAClient;
use KRdmaKit::thread_local::ThreadLocal;
use KRdmaKit::ib_path_explorer::IBExplorer;
use KRdmaKit::ctrl::RCtrl;
use KRdmaKit::cm::SidrCM;
use KRdmaKit::mem::{RMemPhy, Memory};
use KRdmaKit::rpc;
use alloc::vec::Vec;
use alloc::boxed::Box;

use lazy_static::lazy_static;
use alloc::sync::Arc;

type UnsafeGlobal<T> = ThreadLocal<T>;

struct RPCTestModule {}

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

use rust_kernel_linux_util::bindings;
use linux_kernel_module::c_types::c_void;
use KRdmaKit::cm::UDEndPoint;

// UD sidr handshake
fn test_case_rpc() {
    let ctx_idx = 1;
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

    let mut ctrl = RCtrl::create(remote_service_id, &mut ALLRCONTEXTS.get_mut()[ctx_idx]).unwrap();
    // end of preparation
    let mut client_ud = ctrl.get_ud(qd_hint).unwrap();
    let qpn = client_ud.get_qp_num();
    let qkey = client_ud.get_qkey();
    let qp = unsafe { Arc::get_mut_unchecked(&mut client_ud) };

    // let sqp = unsafe { Arc::get_mut_unchecked(&mut server_ud) };
    println!("create UD success");

    let mut sidr_cm = SidrCM::new(ctx, core::ptr::null_mut()).unwrap();
    // sidr handshake. Only provide the `path` and the `service_id`
    let remote_info = sidr_cm.sidr_connect(
        path_res, remote_service_id as u64, qd_hint as u64);

    if remote_info.is_err() {
        return;
    }
    // ================= Finish handshake, start to send message =====================
    // let point = remote_info.unwrap();
    let (mut point, dct_meta) = remote_info.unwrap();
    // 1. generate MR
    let mut test_mr = RMemPhy::new(1024 * 1024);
    // 2. get all of the params for later operation
    let lkey = unsafe { ctx.get_lkey() } as u32;
    let client_pa = test_mr.get_pa(0) as u64;   // use pa for RCOp
    let server_pa = test_mr.get_pa(1024) as u64;
    let sz = 8 as usize;

    // va of the mr. These addrs are used to read/write value in test function
    let client_va = (test_mr.get_ptr() as u64) as *mut i8;
    let server_va = (test_mr.get_ptr() as u64 + 1024 as u64) as *mut i8;

    // now write a value
    let write_value = 122 as i8;
    unsafe { *(client_va) = write_value };  // client side , set value 4
    unsafe { *(server_va) = 4 };
    println!("[before] value = {}", unsafe { *(server_va) });

    // rpc process
    const kUD_HEADER_SZ: usize = 40;
    let recv_entry_size: u64 = 64;
    let mut payload = rpc::Payload::<DCTargetMeta> {
        data: DCTargetMeta {
            dct_num: 11,
            lid: ctx.get_lid(),
        },
        route: rpc::Routing {
            qpn: qpn,
            qkey: qkey,
            lid: ctx.get_lid(),
            gid: ctx.get_gid(),
        },
    };

    // test normal
    let mut op = UDOp::<2048>::new(qp);
    let sz = core::mem::size_of_val(&payload) as u64;
    let recv_entry_size = core::mem::size_of_val(&payload) as u64 + kUD_HEADER_SZ as u64;
    // 1. ready to receive
    let _ = op.post_recv_buffer(server_pa, lkey, recv_entry_size as usize, // entry size
                                1); // entry count
    // 2. call
    unsafe {
        bindings::memcpy((client_va as *mut rpc::Payload::<DCTargetMeta>).cast::<c_void>(),
                         (&mut payload as *mut rpc::Payload::<DCTargetMeta>).cast::<c_void>(),
                         core::mem::size_of_val(&payload) as u64);
    }


    op.send(client_pa as u64,
            lkey, &point, sz as usize);
    // 3. reply
    {
        let _ = op.wait_one_recv_cq(); // receive one
        let msg_ptr = (server_va as u64 + kUD_HEADER_SZ as u64) as *mut i8;

        let recv_meta = unsafe { rpc::Payload::<DCTargetMeta>::from_raw(msg_ptr as *mut rpc::Payload::<DCTargetMeta>) }.unwrap();
        println!("[1] meta  qpn: {} lid:{}", recv_meta.route.qpn, recv_meta.route.qkey);
        op.post_recv_buffer(server_pa, lkey, recv_entry_size as usize, // entry size
                            1); // entry count

        let mut point = UDEndPoint {
            qpn: recv_meta.route.qpn,
            qkey: recv_meta.route.qkey,
            lid: recv_meta.route.lid,
            gid: recv_meta.route.gid,
            ah: core::ptr::null_mut(),
        };
        point.bind_pd(ctx.get_pd());
        // assemble reply
        let mut payload = rpc::Payload::<DCTargetMeta> {
            data: DCTargetMeta {
                dct_num: 11,
                lid: ctx.get_lid(),
            },
            route: rpc::Routing {
                qpn: qpn,
                qkey: qkey,
                lid: ctx.get_lid(),
                gid: ctx.get_gid(),
            },
        };
        let reply_va = server_va as u64 + 2048;
        let reply_pa = server_pa as u64 + 2048;
        unsafe {
            bindings::memcpy((reply_va as *mut rpc::Payload::<DCTargetMeta>).cast::<c_void>(),
                             (&mut payload as *mut rpc::Payload::<DCTargetMeta>).cast::<c_void>(),
                             core::mem::size_of_val(&payload) as u64);
        }

        op.send(reply_pa as u64, lkey, &point, sz as usize);
    }

    // 4. receive reply
    let _ = op.wait_one_recv_cq(); // receive one

    let msg_ptr = (server_va as u64 + (kUD_HEADER_SZ as u64 + recv_entry_size) as u64) as *mut i8;

    let recv_meta = unsafe { rpc::Payload::<DCTargetMeta>::from_raw(msg_ptr as *mut rpc::Payload::<DCTargetMeta>) }.unwrap();
    println!("[2] meta  qpn: {} qkey:{}", recv_meta.route.qpn, recv_meta.route.qkey);
}

use rust_kernel_linux_util::bindings::{bd_ssleep, bd_kthread_run};

#[inline]
fn test_case_two_way() {
    let t_server = unsafe {
        bd_kthread_run(
            Some(server_thread),
            core::ptr::null_mut(),
            b"RLib CM\0".as_ptr() as *const i8,
        )
    };
    let t_client = unsafe {
        bd_kthread_run(
            Some(client_thread),
            core::ptr::null_mut(),
            b"RLib CM\0".as_ptr() as *const i8,
        )
    };
    for i in 0..2 {
        unsafe {
            bd_ssleep(1);
        }
        println!("sleep for {} sec", i);
    }
}

pub unsafe extern "C" fn server_thread(
    _data: *mut linux_kernel_module::c_types::c_void,
) -> linux_kernel_module::c_types::c_int {
    const kUD_HEADER_SZ: usize = 40;

    println!("server thread come in");
    {
        let ctx_idx = 1;
        let ctx: &mut RContext = &mut ALLRCONTEXTS.get_mut()[ctx_idx];
        let sa_client = &mut SA_CLIENT.get_mut();

        // param
        let remote_service_id = 50;
        let qd_hint = 73;

        let mut ctrl = RCtrl::create(remote_service_id, &mut ALLRCONTEXTS.get_mut()[ctx_idx]).unwrap();
        // end of preparation
        let mut client_ud = ctrl.get_ud(qd_hint).unwrap();
        let qpn = client_ud.get_qp_num();
        let qkey = client_ud.get_qkey();
        let qp = unsafe { Arc::get_mut_unchecked(&mut client_ud) };

        let mut test_mr = RMemPhy::new(1024 * 1024);
        let lkey = unsafe { ctx.get_lkey() } as u32;
        let pa = test_mr.get_pa(0) as u64;
        let va = (test_mr.get_ptr() as u64) as *mut i8;

        let mut op = UDOp::<2048>::new(qp);
        let en_sz = 512;

        ctrl.post_rpc_listen(pa, lkey, en_sz);

        let mut failed_cnt = 0;
        while ctrl.poll(va as u64, va as u64 + 1024, pa + 1024, lkey).is_err()
            && failed_cnt <= 10 {
            failed_cnt += 1;
        }
    }
    0
}

pub unsafe extern "C" fn client_thread(
    _data: *mut linux_kernel_module::c_types::c_void,
) -> linux_kernel_module::c_types::c_int {
    unsafe {
        bd_ssleep(1);
    }
    const kUD_HEADER_SZ: usize = 40;

    println!("client thread come in");
    {
        let ctx_idx = 1;
        let ctx: &mut RContext = &mut ALLRCONTEXTS.get_mut()[ctx_idx];
        let sa_client = &mut SA_CLIENT.get_mut();

        // preparation
        let mut explorer = IBExplorer::new();

        let gid_addr = ctx.get_gid_as_string();
        let remote_service_id = 50;

        let _ = explorer.resolve(remote_service_id, ctx, &gid_addr, sa_client.get_inner_sa_client());

        let path_res = explorer.get_path_result().unwrap();
        // param
        let qd_hint = 73;

        let mut ctrl = RCtrl::create(12, &mut ALLRCONTEXTS.get_mut()[ctx_idx]).unwrap();
        // end of preparation
        let mut client_ud = ctrl.get_ud(qd_hint).unwrap();
        let qpn = client_ud.get_qp_num();
        let qkey = client_ud.get_qkey();
        let qp = unsafe { Arc::get_mut_unchecked(&mut client_ud) };

        // let sqp = unsafe { Arc::get_mut_unchecked(&mut server_ud) };
        println!("create UD success");

        let mut sidr_cm = SidrCM::new(ctx, core::ptr::null_mut()).unwrap();
        // sidr handshake. Only provide the `path` and the `service_id`
        let remote_info = sidr_cm.sidr_connect(
            path_res, remote_service_id as u64, qd_hint as u64);

        if remote_info.is_err() {
            println!("get remote error");
            return 0;
        }
        // ================= Finish handshake, start to send message =====================
        // let point = remote_info.unwrap();
        let (mut point, dct_meta) = remote_info.unwrap();

        // 1. generate MR
        let mut test_mr = RMemPhy::new(1024 * 1024);
        let lkey = unsafe { ctx.get_lkey() } as u32;
        let pa = test_mr.get_pa(0) as u64;
        let va = (test_mr.get_ptr() as u64) as *mut i8;

        // request
        ctrl.call_get_dct(pa, va as u64, 1024, lkey, &point);

        // async fetch response
        let mut failed_cnt = 0;
        while ctrl.get_dct_response(va as u64 + 1024).is_err() &&
            failed_cnt <= 10 {
            failed_cnt += 1;
        };
        if failed_cnt > 10 {
            println!("time out !!!")
        }

        let msg_ptr = (va as u64 + (1024 + kUD_HEADER_SZ) as u64) as *mut i8;
        let reply = unsafe { rpc::Payload::<DCTargetMeta>::from_raw(msg_ptr as *mut rpc::Payload::<DCTargetMeta>) }.unwrap();
        println!("get response: {}", reply.data.dct_num);
    }
    0
}


impl linux_kernel_module::KernelModule for RPCTestModule {
    fn init() -> linux_kernel_module::KernelResult<Self> {
        ctx_init();

        test_case_rpc();
        // test_case_two_way();
        Ok(Self {})
    }
}

impl Drop for RPCTestModule {
    fn drop(&mut self) {
        unsafe { ib_unregister_client(get_global_client() as *mut ib_client) };
        SA_CLIENT.get_mut().reset();
    }
}


linux_kernel_module::kernel_module!(
    RPCTestModule,
    author: b"lfm",
    description: b"A unit test for rpc communication",
    license: b"GPL"
);
