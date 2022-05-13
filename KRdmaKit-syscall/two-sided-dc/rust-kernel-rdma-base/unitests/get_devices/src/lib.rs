#![no_std]

extern crate alloc;

pub mod console_msgs;

use rust_kernel_rdma_base::linux_kernel_module;

use linux_kernel_module::mutex::LinuxMutex;
use linux_kernel_module::sync::Mutex;
use linux_kernel_module::{c_types, println};

use rust_kernel_rdma_base::*;

use alloc::vec::Vec;
use lazy_static::lazy_static;

struct DevWrapper(*mut ib_device);

unsafe impl Send for DevWrapper {}

// Global per-thread dev and connection controls
lazy_static! {
    static ref ALL_DEVS: LinuxMutex<Vec<DevWrapper>> = LinuxMutex::new(Vec::new());
}

unsafe extern "C" fn _add_one(dev: *mut ib_device) {
    ALL_DEVS.lock_f(|d| d.push(DevWrapper(dev)));
}

unsafe extern "C" fn _remove_one(_dev: *mut ib_device, _client_data: *mut c_types::c_void) {}

static mut CLIENT: ib_client = ib_client {
    name: b"raw-kernel-rdma-unit-test\0".as_ptr() as *mut c_types::c_char,
    add: Some(_add_one),
    remove: Some(_remove_one),
    get_net_dev_by_params: None,
    list: rust_kernel_rdma_base::bindings::list_head::new_as_nil(),
};

struct GetDevTestModule {}

fn print_test_msgs(test_case_idx: usize, succ: bool) {
    if succ {
        println!("{:?}", crate::console_msgs::SUCC[test_case_idx]);
    } else {
        println!("{:?}", crate::console_msgs::ERR[test_case_idx]);
    }
}

// test whether we can acquire devices
fn test_case_0() {
    // note, the dev is destroyed in the Drop of GetDevTestModule
    let err = unsafe { ib_register_client(&mut CLIENT as *mut ib_client) };
    print_test_msgs(0, err == 0);
}

// test whether we can create pd using the dev
fn test_case_1() {
    print_test_msgs(1, ALL_DEVS.lock_f(|v| v.len()) != 0);
}

fn test_case_gid() {
    ALL_DEVS.lock_f(|v| {
        if v.len() > 0 {
            for i in 0..v.len() {
                let dev: &mut ib_device = &mut unsafe { *(v[i].0) };
                let mut gid: ib_gid = Default::default();

                let err = unsafe {
                    dev.query_gid.unwrap()(v[i].0, 1, 0, &mut gid as *mut ib_gid)
                };
                if err != 0 {
                    println!("err query gid");
                }
                println!("check gid: {:?}", gid);

            }
        } else {
            println!("e\rr, empty device");
        }
    });
}

impl linux_kernel_module::KernelModule for GetDevTestModule {
    fn init() -> linux_kernel_module::KernelResult<Self> {
        test_case_0();
        test_case_1();
        test_case_gid();
        Ok(Self {})
    }
}

impl Drop for GetDevTestModule {
    fn drop(&mut self) {
        ALL_DEVS.lock_f(|d| d.clear());
        unsafe { ib_unregister_client(&mut CLIENT as *mut ib_client) };
    }
}

linux_kernel_module::kernel_module!(
    GetDevTestModule,
    author: b"xmm",
    description: b"A sample module for testing whether we can get ib_device in the kernel",
    license: b"GPL"
);
