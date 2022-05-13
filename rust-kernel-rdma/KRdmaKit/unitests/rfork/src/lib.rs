#![no_std]
#![feature(get_mut_unchecked, allocator_api)]
#[warn(non_snake_case)]
#[warn(dead_code)]
#[warn(unused_imports)]
extern crate alloc;

pub mod console_msgs;
mod consts;

mod frwr;
mod dct;
mod two_nic;

use frwr::*;
use alloc::vec;
use alloc::vec::Vec;
use KRdmaKit::device::{RContext, RNIC};
use KRdmaKit::{random, rust_kernel_rdma_base};
use KRdmaKit::thread_local::ThreadLocal;
use lazy_static::lazy_static;
// linux_kernel_module
use linux_kernel_module::{c_types, println};
// rust_kernel_rdma_base
use rust_kernel_rdma_base::*;
use rust_kernel_rdma_base::linux_kernel_module;
use crate::dct::test_dct_bench;
use crate::two_nic::demo_use_two_nics;


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

#[inline]
pub fn random_split(total: u64, shard_num: u64) -> Vec<u64> {
    let mut rand = random::FastRandom::new(0xdeadbeaf);
    let mut result: Vec<u64> = Vec::with_capacity(shard_num as usize);
    let mut sum = 0 as u64;
    for _ in 0..shard_num - 1 {
        let tmp = total - sum;
        let mut val = rand.get_next() % tmp;
        result.push(val);
        sum += val;
    }
    result.push(total - sum);
    result
}

impl linux_kernel_module::KernelModule for RegMRTestModule {
    fn init() -> linux_kernel_module::KernelResult<Self> {
        ctx_init();
        // bench_frwr();           // FRWR
        // bench_pin();
        // bench_vma();
        // test_dct_bench();
        demo_use_two_nics();
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
