use lazy_static::lazy_static;
use KRdmaKit::thread_local::ThreadLocal;
use KRdmaKit::SAClient;
use KRdmaKit::device::{RNIC, RContext};

use alloc::vec::Vec;
use alloc::boxed::Box;
use KRdmaKit::rust_kernel_rdma_base;
use rust_kernel_rdma_base::*;
use rust_kernel_rdma_base::linux_kernel_module;
use linux_kernel_module::{println, c_types};
use KRdmaKit::ctrl::RCtrl;
use core::pin::Pin;
use KRdmaKit::mem::{RMemPhy, Memory};
use KRdmaKit::qp::{DC, RC, RecvHelper};
use alloc::sync::Arc;
use KRdmaKit::cm::ClientCM;
use core::ptr::null_mut;

type UnsafeGlobal<T> = ThreadLocal<T>;

// global memory of 4K. used for simple test
lazy_static! {
    pub static ref GLOBAL_MEM: UnsafeGlobal<Vec<RMemPhy>> = UnsafeGlobal::new(Vec::new());
    // pub static ref GLOBAL_MEM: UnsafeGlobal<RMemPhy> = UnsafeGlobal::new(RMemPhy::new(1024 * 1024 * 4));
}

lazy_static! {
    pub static ref ALLNICS: UnsafeGlobal<Vec<RNIC>> = UnsafeGlobal::new(Vec::new());
    pub static ref ALLRCONTEXTS: UnsafeGlobal<Vec<RContext<'static>>> = UnsafeGlobal::new(Vec::new());
    pub static ref SA_CLIENT: UnsafeGlobal<SAClient> = UnsafeGlobal::new(SAClient::create());
    pub static ref RCTRL : UnsafeGlobal<Vec<Pin<Box<RCtrl<'static>>>>> = UnsafeGlobal::new(Vec::with_capacity(50));
    pub static ref TRCTRL: UnsafeGlobal<Vec<Pin<Box<RCtrl<'static>>>>> = UnsafeGlobal::new(Vec::with_capacity(2));
}
static mut CLIENT: Option<ib_client> = None;
pub const MAX_SERVICE_NUM: usize = 25;

unsafe extern "C" fn _add_one(dev: *mut ib_device) {
    let nic = RNIC::create(dev, 1);
    ALLNICS.get_mut().push(nic.ok().unwrap());
}

unsafe extern "C" fn _remove_one(dev: *mut ib_device, _client_data: *mut c_types::c_void) {
    println!("remove one dev {:?}", dev);
}

gen_add_dev_func!(_add_one, _new_add_one);
#[inline]
unsafe fn get_global_client() -> &'static mut ib_client {
    match CLIENT {
        Some(ref mut x) => &mut *x,
        None => panic!(),
    }
}

#[inline]
pub fn get_global_rcontext(idx: usize) -> &'static mut RContext<'static> {
    let len = ALLRCONTEXTS.len();
    let ctx = &mut ALLRCONTEXTS.get_mut()[idx % len];
    return ctx;
}

#[inline]
pub fn get_global_rctrl(idx: usize) -> &'static mut Pin<Box<RCtrl<'static>>> {
    let len = RCTRL.len();
    &mut RCTRL.get_mut()[idx % len]
}

#[inline]
pub fn get_global_rctrl_len() -> usize {
    RCTRL.len()
}

#[inline]
pub fn get_global_sa_client() -> &'static mut SAClient {
    SA_CLIENT.get_mut()
}

#[inline]
pub fn get_global_test_mem_pa(idx: usize) -> u64 {
    let len = GLOBAL_MEM.get_ref().len();
    { &mut GLOBAL_MEM.get_mut()[0] }.get_dma_buf()
}

// we default use the nic_0
#[inline]
pub fn get_bind_ctrl(port: usize) -> &'static mut Pin<Box<RCtrl<'static>>> {
    get_global_rctrl(port * 2)
}

pub struct Client {}

impl Client {
    pub fn create() -> Option<Self> {
        unsafe {
            CLIENT = Some(core::mem::MaybeUninit::zeroed().assume_init());
            get_global_client().name = b"kRdmaKit-client\0".as_ptr() as *mut c_types::c_char;
            get_global_client().add = Some(_new_add_one);
            get_global_client().remove = Some(_remove_one);
            get_global_client().get_net_dev_by_params = None;
        }

        let _err = unsafe { ib_register_client(get_global_client() as *mut ib_client) };

        // create all of the context according to NIC
        for i in 0..ALLNICS.len() {
            ALLRCONTEXTS.get_mut()
                .push(RContext::create(&mut ALLNICS.get_mut()[i]).unwrap());
        }

        for i in 0..ALLRCONTEXTS.len() {
            println!("ctx {} info {:?}", i, ALLRCONTEXTS.get_ref()[i]);
        }

        // create necessary rctrl for qp server one-sided connection
        let core_num = MAX_SERVICE_NUM;
        for i in 0..core_num {
            for j in 0..ALLNICS.len() {
                RCTRL.get_mut().push(
                    RCtrl::create(
                        i, get_global_rcontext(j),
                    ).unwrap()
                );
            }
        }

        for _ in 0..2 {
            GLOBAL_MEM.get_mut().push(
                RMemPhy::new(1024 * 1024 * 4)
            );
        }

        Some(Self {})
    }
}

impl Drop for Client {
    fn drop(&mut self) {
        println!("core client exit, free {} devs", ALLNICS.get_mut().len());
        // first clear all the rctrl
        RCTRL.get_mut().clear();
        TRCTRL.get_mut().clear();
        // first, free all client's context
        for ctx in ALLRCONTEXTS.get_mut() {
            ctx.reset();
        }
        unsafe { ib_unregister_client(get_global_client() as *mut ib_client) };
        SA_CLIENT.get_mut().reset();
    }
}

