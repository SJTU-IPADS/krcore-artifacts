use lazy_static::lazy_static;
use KRdmaKit::thread_local::ThreadLocal;
use KRdmaKit::{Profile};
use KRdmaKit::device::{RNIC, RContext};
use hashbrown::HashMap;

use alloc::vec::Vec;
use alloc::boxed::Box;
use alloc::string::String;
use core::convert::TryInto;
use core::ffi::c_void;
use KRdmaKit::qp::{DCTargetMeta, RC, rc_connector};
use KRdmaKit::rust_kernel_rdma_base;
use rust_kernel_rdma_base::*;
use rust_kernel_rdma_base::linux_kernel_module;
use linux_kernel_module::{println, c_types};
use KRdmaKit::ctrl::RCtrl;
use core::pin::Pin;
use core::ptr::null_mut;
use nostd_async::{Runtime, Task};
use KRdmaKit::cm::{EndPoint, SidrCM};
use KRdmaKit::consts::DEFAULT_RPC_HINT;
use KRdmaKit::ib_path_explorer::IBExplorer;
use KRdmaKit::mem::{RMemPhy};
use KRdmaKit::net_util::gid_to_str;
use KRdmaKit::rpc::RPCClient;
use KRdmaKit::rust_kernel_rdma_base::rust_kernel_linux_util::{debug, info};
use crate::consts::{RPC_BUFFER_N};
use crate::rpc::caller::{call_connect_rc, call_dereg_dc_meta, call_disconnect_rc, call_query_dc_meta, call_reg_dc_meta};
use crate::rpc::handler::{dereg_dc_meta_handler, fill_handler_table, query_dc_meta_handler, reg_dc_meta_handler};
use crate::rpc::RPCReqType::{DeregisterDcMeta, QueryDCMeta, RegisterDCMeta};

type UnsafeGlobal<T> = ThreadLocal<T>;

// global memory of 4K. used for simple test
lazy_static! {
    pub static ref GLOBAL_MEM: UnsafeGlobal<Vec<RMemPhy>> = UnsafeGlobal::new(Vec::new());
}

lazy_static! {
    pub static ref ALLNICS: UnsafeGlobal<Vec<RNIC>> = UnsafeGlobal::new(Vec::new());
    pub static ref ALLRCONTEXTS: UnsafeGlobal<Vec<RContext<'static>>> = UnsafeGlobal::new(Vec::new());
    pub static ref RCTRL : UnsafeGlobal<Vec<Pin<Box<RCtrl<'static>>>>> = UnsafeGlobal::new(Vec::with_capacity(50));

}

lazy_static! {
    pub static ref META_INFO: UnsafeGlobal<HashMap<String, EndPoint>>
            = UnsafeGlobal::new(Default::default());
    // rpc
    pub static ref RPC_CLIENTS: UnsafeGlobal<Vec<RPCClient<'static,RPC_BUFFER_N>>> = UnsafeGlobal::new(Vec::with_capacity(4));

}
static mut CLIENT: Option<ib_client> = None;
pub static mut META_KV_PA: u64 = 0;
pub const MAX_SERVICE_NUM: usize = 25;

unsafe extern "C" fn _add_one(dev: *mut ib_device) {
    let nic = RNIC::create(dev, 1);
    if nic.is_ok() {
        ALLNICS.get_mut().push(nic.ok().unwrap());
    }
}

unsafe extern "C" fn _remove_one(dev: *mut ib_device, _client_data: *mut c_types::c_void) {
    debug!("remove one dev {:?}", dev);
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
pub fn get_global_test_mem_pa(idx: usize) -> u64 {
    let len = GLOBAL_MEM.get_ref().len();
    { &mut GLOBAL_MEM.get_mut()[idx % len] }.get_dma_buf()
}

#[inline]
pub fn get_global_test_mem_len() -> usize {
    GLOBAL_MEM.get_ref().len()
}

#[inline]
pub fn get_global_meta_kv_mem() -> &'static mut RMemPhy {
    &mut GLOBAL_MEM.get_mut()[1]
}

// we default use the nic_0
#[inline]
pub fn get_bind_ctrl(port: usize) -> &'static mut Pin<Box<RCtrl<'static>>> {
    get_global_rctrl(port * 2)
}


#[inline]
pub fn get_rpc_client(idx: usize) -> &'static mut RPCClient<'static, RPC_BUFFER_N> {
    let len = RPC_CLIENTS.len();
    &mut RPC_CLIENTS.get_mut()[idx % len]
}

#[inline]
pub fn get_remote_dc_meta_unsafe(remote_gid: &String) -> &EndPoint {
    META_INFO.get_mut().get(remote_gid).unwrap()
}

#[inline]
pub fn get_remote_dc_meta(remote_gid: &String) -> Option<&EndPoint> {
    META_INFO.get_mut().get(remote_gid)
}

#[inline]
pub fn put_remote_dc_meta(remote_gid: &String, point: &EndPoint) {
    META_INFO.get_mut().insert(remote_gid.clone(), point.self_clone());
}

#[inline]
pub fn evict_remote_dc_meta(remote_gid: &String) {
    META_INFO.get_mut().remove(remote_gid);
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

        if 0 == ALLNICS.len() {
            unsafe { ib_unregister_client(get_global_client() as *mut ib_client) };
            return None;
        }
        // create all of the context according to NIC
        for i in 0..ALLNICS.len() {
            ALLRCONTEXTS.get_mut()
                .push(RContext::create(&mut ALLNICS.get_mut()[i]).unwrap());
        }

        for i in 0..ALLRCONTEXTS.len() {
            info!("ctx {} info {:?}", i, ALLRCONTEXTS.get_ref()[i]);
        }

        // create necessary rctrl for qp server one-sided connection
        let core_num = MAX_SERVICE_NUM;
        for i in 0..core_num {
            for j in 0..ALLNICS.len() {
                RCTRL.get_mut().push(
                    RCtrl::create(
                        i.try_into().unwrap(), get_global_rcontext(j),
                    ).unwrap()
                );
            }
        }

        for i in 0..2 {
            let ctx = get_global_rcontext(0);
            let ctrl = get_global_rctrl(i * 2);
            let mr = ctrl.get_self_test_mr();

            let dct_num = ctrl.get_dc_num();
            let ud = ctrl.get_ud(DEFAULT_RPC_HINT).unwrap();
            RPC_CLIENTS.get_mut().push(
                RPCClient::create(ctx,
                                  ud,
                                  dct_num,
                                  &mr)
            );
        }

        for i in 0..RPC_CLIENTS.len() {
            fill_handler_table(get_rpc_client(i));
        }

        for _ in 0..12 {
            GLOBAL_MEM.get_mut().push(
                RMemPhy::new(1024 * 4)
            );
        }
        #[cfg(feature = "rpc_server")]
        {
            use rust_kernel_linux_util::bindings::{bd_ssleep, bd_kthread_run};
            pub unsafe extern "C" fn polling_thread(
                data: *mut linux_kernel_module::c_types::c_void,
            ) -> linux_kernel_module::c_types::c_int {
                let runtime = Runtime::new();   // runtime of this kthread
                println!("start polling thread");
                let rpc_client = &mut *(data as *mut RPCClient<16>);
                let mut polling_task = Task::new(async {
                    rpc_client.poll_all().await
                });
                let s_handler = polling_task.spawn(&runtime);
                // wait until polling is done
                s_handler.join();
                0
            }
            let _ = unsafe {
                bd_kthread_run(
                    Some(polling_thread),
                    (&mut RPC_CLIENTS.get_mut()[0] as *mut RPCClient<16>).cast::<c_void>(),
                    b"RLib CM\0".as_ptr() as *const i8,
                )
            };
        }
        #[cfg(feature = "meta_kv")]
        if conn_meta().is_none() {
            return None;
        }
        Some(Self {})
    }
}

#[cfg(feature = "meta_kv")]
fn conn_meta() -> Option<()> {
    // fallback path
    {
        let meta_pa = get_global_meta_kv_mem().get_dma_buf();
        unsafe { META_KV_PA = meta_pa; }
    }
    let remote_gid = crate::get_meta_server_gid();
    let remote_service_id = 0;
    let ctx = &ALLRCONTEXTS.get_mut()[remote_service_id];
    // 1. get path
    let mut retry_cnt = 0;

    let path_res = loop {
        match ctx.explore_path(remote_gid.clone(), remote_service_id as u64) {
            Some(path) => { break path; }
            None => {
                retry_cnt += 1;
                if retry_cnt > 15 {
                    println!("remote meta kv not exist");
                    return None;
                }
            }
        }
    };

    // 2. sidr connect
    let mut sidr_cm = SidrCM::new(ctx, null_mut()).unwrap();
    let remote_info = sidr_cm.sidr_connect(
        path_res, remote_service_id as u64, DEFAULT_RPC_HINT as u64,
    );
    if remote_info.is_err() {
        println!("connect bad");
        return None;
    }
    let point = remote_info.unwrap();

    META_INFO.get_mut().insert(remote_gid.clone(), point);

    {
        let meta_point = get_remote_dc_meta_unsafe(&remote_gid);
        let reply = call_reg_dc_meta(meta_point);
        if reply.is_some() {
            let meta_pa = reply.unwrap().meta_pa as u64;
            unsafe { META_KV_PA = meta_pa; }
        }
    }
    return Some(());
}

impl Drop for Client {
    fn drop(&mut self) {
        debug!("core client exit, free {} devs", ALLNICS.get_mut().len());
        #[cfg(feature = "meta_kv")]
        {
            let remote_gid = crate::get_meta_server_gid();
            let remote_point = get_remote_dc_meta_unsafe(&remote_gid);
            call_dereg_dc_meta(remote_point);
        }
        META_INFO.get_mut().clear();
        // first clear all the rctrl
        RPC_CLIENTS.get_mut().clear();
        RCTRL.get_mut().clear();
        for ctx in ALLRCONTEXTS.get_mut() {
            ctx.reset();
        }
        unsafe { ib_unregister_client(get_global_client() as *mut ib_client) };
    }
}

