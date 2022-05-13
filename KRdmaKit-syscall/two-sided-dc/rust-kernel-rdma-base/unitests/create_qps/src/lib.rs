#![no_std]

extern crate alloc;

pub mod console_msgs;

use linux_kernel_module::mutex::LinuxMutex;
use linux_kernel_module::sync::Mutex;
use linux_kernel_module::{c_types, println};
use rust_kernel_rdma_base::linux_kernel_module;

use rust_kernel_rdma_base::*;

use alloc::vec::Vec;
use lazy_static::lazy_static;

struct DevWrapper(*mut ib_device);

unsafe impl Send for DevWrapper {}

// Global per-thread dev and connection controls
lazy_static! {
    static ref ALL_DEVS: LinuxMutex<Vec<DevWrapper>> = LinuxMutex::new(Vec::new());
}

unsafe extern "C" fn _add_one(dev: *mut ib_device) -> i32 {
    ALL_DEVS.lock_f(|d| d.push(DevWrapper(dev)));
    0
}

gen_add_dev_func!(_add_one,_new_add_one);

unsafe extern "C" fn _remove_one(_dev: *mut ib_device, _client_data: *mut c_types::c_void) {}

/*
static mut CLIENT: ib_client = ib_client {
    name: b"raw-kernel-rdma-unit-test\0".as_ptr() as *mut c_types::c_char,
    add: Some(_add_one),
    remove: Some(_remove_one),
    get_net_dev_by_params: None,
    //    list:  rust_kernel_rdma_base::bindings::list_head::new_as_nil(),
}; */

static mut CLIENT: Option<ib_client> = None;

unsafe fn get_global_client() -> &'static mut ib_client {
    match CLIENT {
        Some(ref mut x) => &mut *x,
        None => panic!(),
    }
}

struct CreateQPTestModule {}

fn print_test_msgs(test_case_idx: usize, succ: bool) {
    if succ {
        println!("{:?}", crate::console_msgs::SUCC[test_case_idx]);
    } else {
        println!("{:?}", crate::console_msgs::ERR[test_case_idx]);
    }
}

// test functions

/// for each device in the "ALL_DEVS", create a CQ on it, if successful, then delete it
fn test_create_cq() {
    ALL_DEVS.lock_f(|v| {
        if v.len() > 0 {
            for i in 0..v.len() {
                let mut cq_attr: ib_cq_init_attr = Default::default();
                cq_attr.cqe = 128;

                let cq = unsafe {
                    ib_create_cq(
                        v[i].0,
                        None,
                        None,
                        core::ptr::null_mut(),
                        &mut cq_attr as *mut _,
                    )
                };
                if cq.is_null() {
                    println!("null cq");
                } else {
                    println!("create cq ok {:?}, now free it", cq);
                    // free
                    unsafe { ib_free_cq(cq) };
                }
            }
        } else {
            // report error in dmesg to notify the test framework
            println!("err, empty device");
        }
    });
}

/// for each device in the "ALL_DEVS", create a QP on it, if successful, then delete it
fn test_create_qp() {
    ALL_DEVS.lock_f(|v| {
        if v.len() > 0 {
            for i in 0..v.len() {
                let mut cq_attr: ib_cq_init_attr = Default::default();
                cq_attr.cqe = 128;

                let cq = unsafe {
                    ib_create_cq(
                        v[i].0,
                        None,
                        None,
                        core::ptr::null_mut(),
                        &mut cq_attr as *mut _,
                    )
                };
                if cq.is_null() {
                    println!("null cq");
                } else {
                    println!("create cq ok {:?}, now free it", cq);

                    let pd = unsafe { ib_alloc_pd(v[i].0, 0) };
                    if pd.is_null() {
                        println!("err creating pd");
                        return;
                    }

                    // now the QP
                    let mut qp_attr: ib_qp_init_attr = Default::default();
                    qp_attr.cap.max_send_wr = 64;
                    qp_attr.cap.max_recv_wr = 12;
                    qp_attr.cap.max_recv_sge = 1;
                    qp_attr.cap.max_send_sge = 16;
                    qp_attr.sq_sig_type = ib_sig_type::IB_SIGNAL_REQ_WR;
                    qp_attr.qp_type = ib_qp_type::IB_QPT_RC;
                    qp_attr.send_cq = cq;
                    qp_attr.recv_cq = cq;

                    let qp = unsafe { ib_create_qp(pd, &mut qp_attr as *mut ib_qp_init_attr) };
                    if qp.is_null() {
                        println!("null qp");
                    } else {
                        println!("create qp ok {:?}, now free it", qp);
                        let ret = unsafe { ib_destroy_qp(qp) };
                        if ret != 0 {
                            println!("err destroying qp err: {}", ret);
                        }
                    }

                    // free
                    unsafe { ib_free_cq(cq) };
                }
            }
        } else {
            // report error in dmesg to notify the test framework
            println!("err, empty device");
        }
    });
}

impl CreateQPTestModule {
    pub fn new() -> Self {
        unsafe {
            CLIENT = Some(core::mem::MaybeUninit::zeroed().assume_init());
            get_global_client().name = b"raw-kernel-rdma-unit-test\0".as_ptr() as *mut c_types::c_char;
            get_global_client().add = Some(_new_add_one);
            get_global_client().remove = Some(_remove_one);
            get_global_client().get_net_dev_by_params = None;
        }

        let err = unsafe { ib_register_client(get_global_client() as *mut ib_client) };
        print_test_msgs(0, err == 0);

        test_create_cq();
        test_create_qp();

        Self {}
    }
}

impl linux_kernel_module::KernelModule for CreateQPTestModule {
    fn init() -> linux_kernel_module::KernelResult<Self> {
        let res = CreateQPTestModule::new();
        Ok(res)
    }
}

impl Drop for CreateQPTestModule {
    fn drop(&mut self) {
        ALL_DEVS.lock_f(|d| d.clear());
        unsafe { ib_unregister_client(get_global_client() as *mut ib_client) };
    }
}

linux_kernel_module::kernel_module!(
    CreateQPTestModule,
    author: b"xmm",
    description: b"A module for testing creating/deleting qps",
    license: b"GPL"
);
