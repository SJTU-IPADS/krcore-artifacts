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

unsafe extern "C" fn _add_one(dev: *mut ib_device) {
    ALL_DEVS.lock_f(|d| d.push(DevWrapper(dev)));
}

gen_add_dev_func!(_add_one, _new_add_one);

unsafe extern "C" fn _remove_one(_dev: *mut ib_device, _client_data: *mut c_types::c_void) {}

/* 
static mut CLIENT: ib_client = ib_client {
    name: b"raw-kernel-rdma-unit-test\0".as_ptr() as *mut c_types::c_char,
    add: Some(_new_add_one),
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


/// modify DCT states
unsafe fn bring_dc_to_init(qp: *mut ib_qp) -> bool {
    let mut attr: ib_qp_attr = Default::default();
    let mut mask: linux_kernel_module::c_types::c_int = ib_qp_attr_mask::IB_QP_STATE;

    attr.qp_state = ib_qp_state::IB_QPS_INIT;

    attr.pkey_index = 0;
    mask = mask | ib_qp_attr_mask::IB_QP_PKEY_INDEX;

    attr.port_num = 1;
    mask = mask | ib_qp_attr_mask::IB_QP_PORT;

    mask = mask | ib_qp_attr_mask::IB_QP_DC_KEY;

    ib_modify_qp(qp, &mut attr as _, mask) == 0
}

unsafe fn bring_dc_to_rtr(qp: *mut ib_qp) -> linux_kernel_module::c_types::c_int {
    let mut qp_attr: ib_qp_attr = Default::default();
    let mut mask: linux_kernel_module::c_types::c_int = ib_qp_attr_mask::IB_QP_STATE;

    qp_attr.qp_state = ib_qp_state::IB_QPS_RTR;

    qp_attr.path_mtu = ib_mtu::IB_MTU_4096;
    mask = mask | ib_qp_attr_mask::IB_QP_PATH_MTU;

    qp_attr.ah_attr.port_num = 1;

    qp_attr.ah_attr.sl = 0;
    mask = mask | ib_qp_attr_mask::IB_QP_AV;

    ib_modify_qp(qp, &mut qp_attr as _, mask)
}

unsafe fn bring_dc_to_rts(qp: *mut ib_qp) -> linux_kernel_module::c_types::c_int {
    let mut qp_attr: ib_qp_attr = Default::default();
    let mut mask: linux_kernel_module::c_types::c_int = ib_qp_attr_mask::IB_QP_STATE;    
    
    qp_attr.qp_state = ib_qp_state::IB_QPS_RTS;

    qp_attr.timeout = 50;
    mask = mask | ib_qp_attr_mask::IB_QP_TIMEOUT;

    qp_attr.retry_cnt = 5;
    mask = mask | ib_qp_attr_mask::IB_QP_RETRY_CNT;

    qp_attr.rnr_retry = 5;
    mask = mask | ib_qp_attr_mask::IB_QP_RNR_RETRY;

    qp_attr.max_rd_atomic = 16;
    mask = mask | ib_qp_attr_mask::IB_QP_MAX_QP_RD_ATOMIC;

    ib_modify_qp(qp, &mut qp_attr as _, mask)
}

/// for each device in the "ALL_DEVS", create a QP on it, if successful, then delete it
fn test_create_qp() {
    println!("===== start creating dct client qp =====");
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

                let cq1 = unsafe {
                    ib_create_cq(
                        v[i].0,
                        None,
                        None,
                        core::ptr::null_mut(),
                        &mut cq_attr as *mut _,
                    )
                };

                let cq2 = unsafe {
                    ib_create_cq(
                        v[i].0,
                        None,
                        None,
                        core::ptr::null_mut(),
                        &mut cq_attr as *mut _,
                    )
                };

                if cq.is_null() || cq1.is_null() {
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

                    // now test create the DC qp
                    let mut qp_attr: ib_exp_qp_init_attr = Default::default();
                    qp_attr.cap.max_send_wr = 128;
                    qp_attr.cap.max_send_sge = 1;
                    qp_attr.cap.max_recv_wr = 0;
                    qp_attr.cap.max_recv_sge = 0;
                    qp_attr.cap.max_inline_data = 64;
                    qp_attr.sq_sig_type = ib_sig_type::IB_SIGNAL_REQ_WR;
                    qp_attr.qp_type = ib_qp_type::IB_EXP_QPT_DC_INI;
                    qp_attr.send_cq = cq1;
                    qp_attr.recv_cq = cq1;

                    let qp = unsafe { ib_create_qp_dct(pd, &mut qp_attr as _) };
                    if qp.is_null() {
                        println!("null qp dct x");
                    } else {
                        println!("[DCT] bring dc ok, start to change to init");
                        
                        if unsafe { bring_dc_to_init(qp) } {
                            println!("[DCT] bring dc to init ok, start next step");

                            let res = unsafe { bring_dc_to_rtr(qp) };
                            if  res == 0 {
                                println!("[DCT] bring dc to rtr ok, start next step");
                                let res = unsafe { bring_dc_to_rts(qp)}; 
                                if res != 0 { 
                                    println!("[DCT] bring dc to rts err, {}", res);
                                }
                                println!("[DCT] bring dc to rts ok!");
                            } else {
                                println!("[DCT] err bring dc to rtr! {}", res);
                            }
                        } else {
                            println!("[DCT] err bring dc to init!");
                        } 

                        let ret = unsafe { ib_destroy_qp(qp) };
                        if ret != 0 {
                            println!("err destroying qp err: {}", ret);
                        }
                    }

                    // free
                    unsafe { ib_free_cq(cq) };
                    unsafe { ib_free_cq(cq1) };
                    unsafe { ib_free_cq(cq2) };
                }
            }
        } else {
            // report error in dmesg to notify the test framework
            println!("err, empty device");
        }
    });
}

fn test_create_dct_server() { 
    println!("===== start creating dct server =====");
    ALL_DEVS.lock_f(|v| {
        for i in 0..v.len() {
            println!("creating DCT on nic {}", i);
            let pd = unsafe { ib_alloc_pd(v[i].0, 0) };
                if pd.is_null() {
                    println!("err creating pd");
                    return;
            }            
            
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
                println!("err dct server cq null");
                return;
            }
            
            // create srq
            let mut cq_attr : ib_srq_init_attr = Default::default();
            cq_attr.attr.max_wr = 128;
            cq_attr.attr.max_sge = 1;            
            

            let srq = unsafe { 
                ib_create_srq(pd, &mut cq_attr as _)
            };

            if srq.is_null() { 
                println!("null srq");
            } else {
                // start tes
                println!("create srq success {:?}, start to create DCT target", srq);
                let dc_key = 73;

                let  mut dctattr : ib_dct_init_attr = Default::default();
                dctattr.pd = pd;
                dctattr.cq = cq;
                dctattr.srq = srq;
                dctattr.dc_key = dc_key;
                dctattr.port = 1;
                dctattr.access_flags = ib_access_flags::IB_ACCESS_REMOTE_WRITE | ib_access_flags::IB_ACCESS_REMOTE_READ;
                dctattr.min_rnr_timer = 2;
                dctattr.tclass = 0;
                dctattr.flow_label = 0;
                dctattr.mtu = ib_mtu::IB_MTU_4096;
                dctattr.pkey_index = 0;
                dctattr.hop_limit = 1;
                dctattr.inline_size = 60;

                let dct = unsafe { safe_ib_create_dct(pd, &mut dctattr as _) }; 
                if dct.is_null() { 
                    println!("create null dct");                    
                } else {
                    println!("create dct ok {:?}", dct);
                    let res = unsafe { safe_ib_destroy_dct(dct) };
                    println!("destroy dct code: {}", res);
                }
                
                unsafe { ib_destroy_srq(srq)};
            } 

            unsafe { ib_free_cq(cq) };
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
//        test_create_dct_server();

        println!("[DCT] all test done");

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
