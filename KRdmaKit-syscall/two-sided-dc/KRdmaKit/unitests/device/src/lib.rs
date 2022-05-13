#![no_std]
extern crate alloc;

pub mod console_msgs;

use KRdmaKit::rust_kernel_rdma_base;
// rust_kernel_rdma_base
use rust_kernel_rdma_base::*;
use rust_kernel_rdma_base::linux_kernel_module;
// linux_kernel_module
use linux_kernel_module::{println, c_types};
use KRdmaKit::device::{RNIC, RContext};
use KRdmaKit::thread_local::ThreadLocal;
use KRdmaKit::SAClient;
use alloc::vec::Vec;
use lazy_static::lazy_static;

type UnsafeGlobal<T> = ThreadLocal<T>;

struct DeviceTestModule {}


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

}

fn test_case_0() {
    let err = unsafe { ib_register_client(get_global_client() as *mut ib_client) };
    print_test_msgs(0, err == 0);
}

fn test_case_1() {
    print_test_msgs(1, ALLNICS.len() > 0);
}

fn test_case_2() {
    for i in 0..ALLNICS.len() {
        println!("[{}]RNIC info {:?}", i, &mut ALLNICS.get_mut()[i]);
    }
}

fn test_case_3() {
    for i in 0..ALLNICS.len() {
        ALLRCONTEXTS.get_mut()
            .push(RContext::create(&mut ALLNICS.get_mut()[i]).unwrap());
        println!("create [{}] success", i);
    }
}


fn test_case_4() {
    for i in 0..ALLRCONTEXTS.len() {
        println!("[{}]Dev info {:?}", i, &mut ALLRCONTEXTS.get_mut()[i]);
        println!("[{}]Dev gid {}", i, &mut ALLRCONTEXTS.get_mut()[i].get_gid_as_string());
        unsafe {
            println!("[{}]Dev lkey:{}, rkey:{}", i,
                     &mut ALLRCONTEXTS.get_mut()[i].get_lkey(),
                     &mut ALLRCONTEXTS.get_mut()[i].get_rkey(),
            );
        }
    }
}

unsafe fn test_mr() {
    let ctx = &mut ALLRCONTEXTS.get_mut()[0];
    let pd = ctx.get_pd();
    let _device: *mut ib_device = (*pd).device;

    let _mr = ib_alloc_mr(pd, ib_mr_type::IB_MR_TYPE_MEM_REG, 64);
}

fn test_reset_device() {
    println!(">>>> test deallocation device <<<<");
    let _ = RContext::create(&mut ALLNICS.get_mut()[0]).unwrap();
    let _ = RContext::create(&mut ALLNICS.get_mut()[0]).unwrap();
    let _ = RContext::create(&mut ALLNICS.get_mut()[0]).unwrap();

    println!(">>>> sa client <<<<");
    let _ = SAClient::create();
    let _ = SAClient::create();
    let _ = SAClient::create();
}


impl linux_kernel_module::KernelModule for DeviceTestModule {
    fn init() -> linux_kernel_module::KernelResult<Self> {
        ctx_init();
        test_case_0();  // init
        test_case_1();  // size check
        test_case_2();  // RNIC print
        test_case_3();  // init DEVS
        test_case_4();  // DEV print
        unsafe { test_mr(); }
        test_reset_device();
        Ok(Self {})
    }
}

impl Drop for DeviceTestModule {
    fn drop(&mut self) {
        unsafe { ib_unregister_client(get_global_client() as *mut ib_client) };
    }
}


linux_kernel_module::kernel_module!(
    DeviceTestModule,
    author: b"lfm",
    description: b"A unit test for devices",
    license: b"GPL"
);
