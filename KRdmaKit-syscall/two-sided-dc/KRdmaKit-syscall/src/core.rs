#[warn(unused_imports)]
use crate::client::{get_global_rctrl};
use crate::bindings::*;
use KRdmaKit::rust_kernel_rdma_base::*;
use KRdmaKit::rust_kernel_rdma_base::linux_kernel_module;
use linux_kernel_module::{KernelResult};
use KRdmaKit::qp::RC;
use alloc::sync::Arc;
use core::ptr::null_mut;
use linux_kernel_module::c_types::c_void;
use linux_kernel_module::bindings::{_copy_from_user, _copy_to_user};
use KRdmaKit::consts::DEFAULT_RPC_HINT;

/// Main func for qp connection in kernel space.
/// The runtime latency of this function should be profiled in a more detailed way.
pub fn qp_connect(vid: usize, port: usize, path_rec: &sa_path_rec) -> KernelResult<Option<Arc<RC>>> {
    // let remote_service_id =
    //     if vid >= get_global_rctrl_len() {
    //         vid    // vid as the port number
    //     } else {
    //         unsafe { bd_get_cpu() as usize }    // pick from cpu id
    //     } as u64;
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
                user_wc.wc_wr_id = wc.get_wr_id() as u64;
                user_wc.wc_status = wc.status;
                user_wc.imm_data = unsafe { wc.ex.imm_data } as u32;
                unsafe {
                    _copy_to_user(
                        (req.reply_buf as u64 +
                            core::mem::size_of::<reply_t>() as u64 +
                            i as u64 * core::mem::size_of::<user_wc_t>() as u64
                        ) as *mut c_void,
                        (&user_wc as *const user_wc_t).cast::<c_void>(),
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