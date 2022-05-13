#![no_std]
#![feature(get_mut_unchecked, allocator_api)]
#[warn(non_snake_case)]
#[warn(dead_code)]
extern crate alloc;

pub mod console_msgs;
mod ud;


use KRdmaKit::rust_kernel_rdma_base;
// rust_kernel_rdma_base
use rust_kernel_rdma_base::*;
use rust_kernel_rdma_base::linux_kernel_module;
// linux_kernel_module
use linux_kernel_module::{println, c_types};
use KRdmaKit::device::{RNIC, RContext};
use KRdmaKit::SAClient;
use KRdmaKit::thread_local::ThreadLocal;

use alloc::vec::Vec;

use lazy_static::lazy_static;
use rust_kernel_linux_util::bindings::{bd_ssleep, bd_kthread_run};

// for sync between server and client
static mut RUNNING: bool = true;

// statistics
static mut TOTAL_OP: u64 = 0;
static mut TOTAL_PASSED_US: u64 = 0;

type UnsafeGlobal<T> = ThreadLocal<T>;

use ud::client_thread;
use ud::server_thread;


struct UDTestModule {}

lazy_static! {
    static ref ALLNICS: UnsafeGlobal<Vec<RNIC>> = UnsafeGlobal::new(Vec::new());
    static ref ALLRCONTEXTS: UnsafeGlobal<Vec<RContext<'static>>> = UnsafeGlobal::new(Vec::new());
    static ref SA_CLIENT: UnsafeGlobal<SAClient> = UnsafeGlobal::new(SAClient::create());
}

unsafe extern "C" fn _add_one(dev: *mut ib_device) {
    let nic = RNIC::create(dev, 1);
    ALLNICS.get_mut().push(nic.ok().unwrap());
}

unsafe extern "C" fn _remove_one(dev: *mut ib_device, _client_data: *mut c_types::c_void) {
    println!("remove one dev {:?}", dev);
}

static mut CLIENT: ib_client = ib_client {
    name: b"raw-kernel-rdma-unit-test\0".as_ptr() as *mut c_types::c_char,
    add: Some(_add_one),
    remove: Some(_remove_one),
    get_net_dev_by_params: None,
    list: rust_kernel_rdma_base::bindings::list_head::new_as_nil(),
};

fn print_test_msgs(test_case_idx: usize, assert: bool) {
    if assert {
        println!("{:?}", crate::console_msgs::SUCC[test_case_idx]);
    } else {
        println!("{:?}", crate::console_msgs::ERR[test_case_idx]);
    }
}

fn ctx_init() {
    // register
    let err = unsafe { ib_register_client(&mut CLIENT as *mut ib_client) };
    print_test_msgs(0, err == 0);
    print_test_msgs(0, ALLNICS.len() > 0);

    // create all of the context according to NIC
    for i in 0..ALLNICS.len() {
        ALLRCONTEXTS.get_mut()
            .push(RContext::create(&mut ALLNICS.get_mut()[i]).unwrap());
        println!("create [{}] success", i);
    }
}


impl linux_kernel_module::KernelModule for UDTestModule {
    fn init() -> linux_kernel_module::KernelResult<Self> {
        ctx_init();
        let t_server = unsafe {
            bd_kthread_run(
                Some(server_thread),
                core::ptr::null_mut(),
                b"RLib CM\0".as_ptr() as *const i8,
            )
        };
        assert!(!t_server.is_null());
        let client_cnt = 1;
        for _i in 0..client_cnt {
            let t_client = unsafe {
                bd_kthread_run(
                    Some(client_thread),
                    core::ptr::null_mut(),
                    b"RLib CM\0".as_ptr() as *const i8,
                )
            };
            assert!(!t_client.is_null());
        }


        for i in 0..2 {
            unsafe {
                bd_ssleep(1);
            }
            println!("sleep for {} sec", i);
        }
        unsafe { RUNNING = false; }

        // total
        unsafe {
            let lat_per_op: f64 = TOTAL_PASSED_US as f64 / (TOTAL_OP as f64);    // latency
            let thpt: f64 = client_cnt as f64 *
                TOTAL_OP as f64 / (TOTAL_PASSED_US) as f64 * (1000 * 1000) as f64;
            println!(
                "[total] benchmark result:\n thpt:[ {:.5} ] op/s\tlat:[ {:.5} ] us",
                thpt, lat_per_op
            );
        }

        Ok(Self {})
    }
}

impl Drop for UDTestModule {
    fn drop(&mut self) {
        unsafe {
            ib_unregister_client(&mut CLIENT as *mut ib_client)
        };
        SA_CLIENT.get_mut().reset();
    }
}


linux_kernel_module::kernel_module!(
    UDTestModule,
    author: b"lfm",
    description: b"A unit test for qp basic creation",
    license: b"GPL"
);
