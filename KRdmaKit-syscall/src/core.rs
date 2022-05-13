use alloc::string::String;
#[warn(unused_imports)]
use crate::client::{get_global_rctrl};
use crate::bindings::*;
use KRdmaKit::rust_kernel_rdma_base::*;
use KRdmaKit::rust_kernel_rdma_base::linux_kernel_module;
use linux_kernel_module::{KernelResult};
use KRdmaKit::qp::{DCTargetMeta, RC};
use alloc::sync::Arc;
use core::ptr::null_mut;
use hashbrown::HashMap;
use KRdmaKit::cm::EndPoint;
use KRdmaKit::consts::MAX_KMALLOC_SZ;
use KRdmaKit::mem::{RMemPhy, TempMR};
use linux_kernel_module::c_types::c_void;
use linux_kernel_module::bindings::{_copy_to_user};

/// Main func for qp connection in kernel space.
/// The runtime latency of this function should be profiled in a more detailed way.
pub fn qp_connect(vid: usize, port: usize, path_rec: &sa_path_rec) -> KernelResult<Option<Arc<RC>>> {
    let remote_service_id = port as u64;
    let ctrl = get_global_rctrl(remote_service_id as usize);
    let ctx = ctrl.get_context();
    // create local qp
    let rc = RC::new_with_srq(
        Default::default(),
        ctx, null_mut(),
        null_mut(),
        ctrl.get_recv_cq(),
    );
    if rc.is_some() {
        let mut qp = rc.unwrap();
        let mrc = unsafe { Arc::get_mut_unchecked(&mut qp) };
        // connect handshake(param `vid` is useless)
        mrc.connect(vid as u64, *path_rec, remote_service_id)?;
        return Ok(Some(qp));
    }
    Ok(None)
}


#[inline]
pub fn post_recv(core_id: usize, post_cnt: usize, vid: usize) -> u32 {
    let ctrl = get_global_rctrl(core_id);
    let qp = ctrl.get_trc(vid).unwrap().get_qp();
    let recv_buffer = ctrl.get_recv_buffer();
    let recv = unsafe { Arc::get_mut_unchecked(recv_buffer) };
    return match recv.post_recvs(qp, core::ptr::null_mut(), post_cnt) {
        Ok(_) => {
            reply_status::ok
        }
        Err(_) => reply_status::err
    };
}

#[inline]
pub fn pop_recv(core_id: usize, pop_cnt: usize, offset: usize) -> (Option<*mut ib_wc>, usize) {
    let ctrl = get_global_rctrl(core_id);

    let recv_cq = ctrl.get_recv_cq();
    let recv = unsafe { Arc::get_mut_unchecked(ctrl.get_recv_buffer()) };
    recv.pop_recvs(recv_cq, pop_cnt, offset).unwrap()
}


#[inline]
pub fn handle_pop_ret(pop_ret: Option<*mut ib_wc>,
                      req: &mut req_t,
                      wc_len: usize,
                      payload_sz: u32) -> u32 {
    match pop_ret {
        Some(wc) => {

            // assemble each wc element
            let wc_sz: u64 = core::mem::size_of::<ib_wc>() as u64;
            for i in 0..(wc_len as u64) {
                // assemble the wc
                let mut user_wc: user_wc_t = Default::default();
                let wc = unsafe { *((wc as u64 + i * wc_sz) as *const ib_wc) };
                user_wc.wc_op = wc.opcode;
                let va = wc.get_wr_id() as u64;
                user_wc.wc_wr_id = wc.get_wr_id() as u64;
                user_wc.wc_status = wc.status;
                user_wc.imm_data = unsafe { wc.ex.imm_data } as u32;
                unsafe {
                    rust_kernel_linux_util::bindings::memcpy(
                        (va + payload_sz as u64) as *mut c_void,
                        (va as u64) as *mut c_void,
                        payload_sz as u64,
                    );
                    // copy to user
                    _copy_to_user(
                        (req.reply_buf as u64 +
                            core::mem::size_of::<reply_t>() as u64 +
                            i as u64 * core::mem::size_of::<user_wc_t>() as u64
                        ) as *mut c_void,
                        (&user_wc as *const user_wc_t).cast::<c_void>(),
                        core::mem::size_of_val(&user_wc) as u64,
                    )
                };
            }
            let pop_len = wc_len as u32;
            unsafe {
                _copy_to_user(
                    (req.reply_buf as u64 +
                        core::mem::size_of::<reply_t>() as u64 +
                        wc_consts::pop_wc_len as u64 * core::mem::size_of::<user_wc_t>() as u64
                    ) as *mut c_void,
                    (&pop_len as *const u32).cast::<c_void>(),
                    core::mem::size_of_val(&pop_len) as u64,
                )
            };
            reply_status::ok
        }
        None => {
            reply_status::nil
        }
    }
}

use KRdmaKit::rust_kernel_rdma_base::linux_kernel_module::c_types::*;
use KRdmaKit::thread_local::ThreadLocal;
use crate::client::{get_global_rcontext, get_global_test_mem_pa};
use crate::virtual_queue::VQ;
use lazy_static::lazy_static;
use crate::println;

#[repr(C, align(8))]
pub struct RCConnectParam<'a> {
    pub vid: usize,
    pub port: usize,
    pub path: sa_path_rec,
    pub vq: *mut VQ<'a>,
}

#[cfg(feature = "migrate_qp")]
pub unsafe extern "C" fn bg_rc_migrate_thread(
    data: *mut c_void,
) -> c_int {
    let connect_param = &mut *(data as *mut RCConnectParam);
    let (port, vid, path) = (connect_param.port, connect_param.vid, connect_param.path);
    if (*connect_param.vq).virtual_queue.is_none() {
        match qp_connect(vid, port, &path) {
            Ok(qp) => {
                #[cfg(feature = "virtual_queue")]
                    {
                        (*connect_param.vq).virtual_queue = qp;
                        // println!("RCQP migration success")
                    }
                reply_status::ok
            }
            Err(_) => reply_status::err
        };
    }

    0
}

unsafe impl<'a> Send for RCConnectParam<'a> {}

unsafe impl<'a> Sync for RCConnectParam<'a> {}

/// Local cache for VQ
pub struct LocalCache {
    // local path_rec cache
    pub(crate) path_rec_cache: HashMap<String, sa_path_rec>,

    // local dct meta cache
    pub(crate) dct_meta_cache: HashMap<String, DCTargetMeta>,

    pub(crate) local_mr: Option<TempMR>,

    // cache up <vid, endpoint>
    // cached_client_endpoint: Vec<Option<EndPoint>>, used for two-sided server side
    pub(crate) cached_client_endpoint: HashMap<usize, EndPoint>,

    // for twosided client side
    // used for two-sided client side / dc client side
    pub(crate) remote_endpoint: Option<EndPoint>,
}


impl Default for LocalCache {
    fn default() -> Self {
        Self {
            path_rec_cache: Default::default(),
            dct_meta_cache: Default::default(),
            cached_client_endpoint: Default::default(),
            local_mr: Some(TempMR::new(get_global_test_mem_pa(0 as usize),
                                       MAX_KMALLOC_SZ as u32,
                                       unsafe { get_global_rcontext(0).get_lkey() }, )),
            remote_endpoint: None,
        }
    }
}
