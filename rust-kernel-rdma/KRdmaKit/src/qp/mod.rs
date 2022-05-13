//! The **qp** module supports the RC/UD struct definition, creation process.

// modules qp

// rc primitive
mod rc;
mod rc_op;
mod ud;
mod ud_op;
mod recv_helper;
mod doorbell;
mod dc;
mod dc_op;

// export
pub use rc::{RC, rc_connector};
use rust_kernel_rdma_base::{linux_kernel_module::c_types, rdma_ah_attr_type, ib_send_flags};
pub use ud::UD;
pub use rc_op::RCOp;
pub use ud_op::UDOp;
pub use dc::{DCTServer, DC, DCTargetMeta};
pub use dc_op::DCOp;
pub use doorbell::DoorbellHelper;

pub mod conn;
pub mod statistics;

pub use recv_helper::RecvHelper;
use crate::rust_kernel_rdma_base::*;
use crate::rust_kernel_rdma_base::linux_kernel_module;
use linux_kernel_module::{println};

/// control the parameters of the QP
#[derive(Debug)]
pub struct Config {
    pub max_rd_atomic: usize,
    // max number of outstanding request len while `post_send`
    pub max_send_wr_sz: usize,
    // max number of outstanding request len while `post_recv`
    pub max_recv_wr_sz: usize,
    // max length of cq entry
    pub max_cqe: usize,
    // max length of recv_cq entry
    pub max_recv_cqe: usize,
    // max length of send sge list
    pub max_send_sge: usize,
    // max length of recv sge list
    pub max_recv_sge: usize,
    // qkey for UD communication
    pub qkey: u32,

    pub dc_key : usize,
}

impl Config {
    pub fn new() -> Self {
        let mut res : Self = Default::default();
        res.dc_key = 73; // I like this number :)
        res
    }

    #[inline]
    pub fn set_max_rd_atomic(mut self, new_v: usize) -> Self {
        self.max_rd_atomic = new_v;
        self
    }

    #[inline]
    pub fn set_max_send_wr_sz(mut self, new_v: usize) -> Self {
        self.max_send_wr_sz = new_v;
        self
    }

    #[inline]
    pub fn set_max_recv_wr_sz(mut self, new_v: usize) -> Self {
        self.max_recv_wr_sz = new_v;
        self
    }

    #[inline]
    pub fn set_max_send_sge(mut self, new_v: usize) -> Self {
        self.max_send_sge = new_v;
        self
    }

    #[inline]
    pub fn set_max_recv_sge(mut self, new_v: usize) -> Self {
        self.max_recv_sge = new_v;
        self
    }

    #[inline]
    pub fn set_max_cqe(mut self, new_v: usize) -> Self {
        self.max_cqe = new_v;
        self
    }

    #[inline]
    pub fn set_max_recv_cqe(mut self, new_v: usize) -> Self {
        self.max_recv_cqe = new_v;
        self
    }
}

use linux_kernel_module::random::getrandom;

impl Default for Config {
    fn default() -> Self {
        let mut qkey: [u8; 1] = [0x0; 1];
        let _ = getrandom(&mut qkey);
        Self {
            max_rd_atomic: crate::consts::MAX_RD_ATOMIC,
            max_send_wr_sz: 2048,
            max_recv_wr_sz: 4096,
            max_cqe: 2048,
            max_recv_cqe: 4096,
            max_send_sge: 16,
            max_recv_sge: 1,
            qkey: qkey[0] as u32,
            dc_key : 0,
        }
    }
}


pub unsafe extern "C" fn comp_handler(_cq: *mut ib_cq, _ctx_ptr: *mut linux_kernel_module::c_types::c_void) {
    // do nothing
    println!("in send comp");
}


/// Create ib_cq by `ib_device`. The cq could be both cq in RC and recv_cq in UD
///
/// Usage: ib_device could be fetched by RContext.get_raw_dev()
#[inline]
pub fn create_ib_cq(dev: *mut ib_device, cq_attr: *mut ib_cq_init_attr) -> *mut ib_cq {

    // create cq
    unsafe {
        ib_create_cq(
            dev,
            Some(comp_handler),
            None,
            core::ptr::null_mut(),
            cq_attr,
        )
    }
}

/// Create ib_qp. Usually done after creating the cq and recv_cq.
/// Note that the type of the created qp is determinded by param `qp_type`.
///
///    e.g. we could create one QP of type RC by
///         let rc =  create_ib_qp(pd, ib_qp_type::IB_QPT_RC, cq, recv_cq);
///         create one QP of type UD by
///         let rc =  create_ib_qp(pd, ib_qp_type::IB_QPT_UD, cq, recv_cq);
#[inline]
pub fn create_ib_qp(config: &Config, pd: *mut ib_pd, qp_type: u32, cq: *mut ib_cq, recv_cq: *mut ib_cq, srq: *mut ib_srq) -> *mut ib_qp {
    let mut qp_attr: ib_qp_init_attr = Default::default();
    qp_attr.cap.max_send_wr = config.max_send_wr_sz as u32;
    qp_attr.cap.max_recv_wr = config.max_recv_wr_sz as u32;
    qp_attr.cap.max_recv_sge = config.max_recv_sge as u32;
    qp_attr.cap.max_send_sge = config.max_send_sge as u32;
    qp_attr.cap.max_inline_data = 64 as u32;
    qp_attr.sq_sig_type = ib_sig_type::IB_SIGNAL_REQ_WR;
    qp_attr.qp_type = qp_type;
    qp_attr.send_cq = cq;
    qp_attr.recv_cq = recv_cq;
    qp_attr.srq = srq;

    unsafe { ib_create_qp(pd, &mut qp_attr as *mut ib_qp_init_attr) }
}


/// Fetch the status of one target qp
#[inline]
pub fn get_qp_status(ib_qp: *mut ib_qp) -> Option<u32> {
    let mut attr: ib_qp_attr = Default::default();
    let mut init_attr: ib_qp_init_attr = Default::default();

    // query
    let ret = unsafe {
        ib_query_qp(
            ib_qp,
            &mut attr as *mut ib_qp_attr,
            ib_qp_attr_mask::IB_QP_STATE,
            &mut init_attr as *mut ib_qp_init_attr,
        )
    };

    if ret != 0 {
        return None;
    }

    Some(attr.qp_state)
}

#[inline]
pub fn create_ib_ah(pd: *mut ib_pd, lid: u16, gid: ib_gid) -> *mut ib_ah {
    let mut ah_attr: rdma_ah_attr = Default::default();
    ah_attr.type_ = rdma_ah_attr_type::RDMA_AH_ATTR_TYPE_IB;

    unsafe { bd_rdma_ah_set_dlid(&mut ah_attr, lid as u32); }
    ah_attr.sl = 0;
    ah_attr.port_num = 1;

    unsafe {
        ah_attr.grh.dgid.global.subnet_prefix = gid.global.subnet_prefix;
        ah_attr.grh.dgid.global.interface_id = gid.global.interface_id;
    }

    let res = unsafe { rdma_create_ah_wrapper(pd, &mut ah_attr as _) };

    // maybe, we should wrapper ptr_is_err because this function is 100% safe
    if unsafe { ptr_is_err(res as _) } > 0 {
        return core::ptr::null_mut();
    }
    res
}

#[inline]
pub fn create_ib_ah_ptr(pd: *mut ib_pd, port : u8, lid: u16, gid: ib_gid) -> *mut ib_ah {
    let mut ah_attr: rdma_ah_attr = Default::default();
    ah_attr.type_ = rdma_ah_attr_type::RDMA_AH_ATTR_TYPE_IB;

    unsafe { bd_rdma_ah_set_dlid(&mut ah_attr, lid as u32); }
    ah_attr.sl = 0;
    ah_attr.port_num = port;

    unsafe {
        ah_attr.grh.dgid.global.subnet_prefix = gid.global.subnet_prefix;
        ah_attr.grh.dgid.global.interface_id = gid.global.interface_id;
    }

    let res = unsafe { rdma_create_ah_wrapper(pd, &mut ah_attr as _) };

    // maybe, we should wrapper ptr_is_err because this function is 100% safe
    if unsafe { ptr_is_err(res as _) } > 0 {
        return core::ptr::null_mut();
    }
    res
}

#[inline]
pub fn send_reg_mr(qp: *mut ib_qp, mr: *mut ib_mr, access_flags: u32, whether_signaled: bool) -> c_types::c_int {
    let rkey = unsafe { (*mr).rkey };
    let mut wr: ib_reg_wr = Default::default();

    wr.wr.opcode = ib_wr_opcode::IB_WR_REG_MR;
    wr.wr.num_sge = 0;
    wr.wr.send_flags = if whether_signaled { ib_send_flags::IB_SEND_SIGNALED } else { 0 };
    wr.mr = mr;
    wr.key = rkey;
    wr.access = access_flags as i32;
    let mut bad_wr: *mut ib_send_wr = core::ptr::null_mut();

    unsafe {
        bd_ib_post_send(qp, &mut wr.wr as *mut _,
                        &mut bad_wr as *mut _, )
    }
}


const DC_KEY: u64 = 73;


// this function has memory leakage, but its fine since it is in the unittest
#[inline]
pub fn create_dct_server(pd: *mut ib_pd, device: *mut ib_device) -> *mut ib_dct {
    let mut cq_attr: ib_cq_init_attr = Default::default();
    cq_attr.cqe = 128;

    let cq = create_ib_cq(device, &mut cq_attr as *mut _);


    if cq.is_null() {
        println!("[dct] err dct server cq null");
        return core::ptr::null_mut();
    }

    // create srq
    let mut cq_attr: ib_srq_init_attr = Default::default();
    cq_attr.attr.max_wr = 128;
    cq_attr.attr.max_sge = 1;

    let srq = unsafe { ib_create_srq(pd, &mut cq_attr as _) };

    if srq.is_null() {
        println!("[dct] null srq");
        return core::ptr::null_mut();
    }

    let mut dctattr: ib_dct_init_attr = Default::default();
    dctattr.pd = pd;
    dctattr.cq = cq;
    dctattr.srq = srq;
    dctattr.dc_key = DC_KEY;
    dctattr.port = 1;
    dctattr.access_flags =
        ib_access_flags::IB_ACCESS_REMOTE_WRITE | ib_access_flags::IB_ACCESS_REMOTE_READ;
    dctattr.min_rnr_timer = 2;
    dctattr.tclass = 0;
    dctattr.flow_label = 0;
    dctattr.mtu = ib_mtu::IB_MTU_4096;
    dctattr.pkey_index = 0;
    dctattr.hop_limit = 1;
    dctattr.inline_size = 60;

    unsafe { safe_ib_create_dct(pd, &mut dctattr as _) }
}

use crate::device::RContext;

#[inline]
pub fn create_dc_qp(ctx: &RContext, srq: *mut ib_srq, recv_cq: *mut ib_cq) -> (*mut ib_qp, *mut ib_cq, *mut ib_cq) {
    let pd = ctx.get_pd();
    if pd.is_null() {
        println!("[dct] err creating qp pd");
        return (
            core::ptr::null_mut(),
            core::ptr::null_mut(),
            core::ptr::null_mut(),
        );
    }

    let mut cq_attr: ib_cq_init_attr = Default::default();
    cq_attr.cqe = 128;

    let cq = create_ib_cq(ctx.get_raw_dev(), &mut cq_attr as *mut _);
    if cq.is_null() {
        println!("[dct] qp null cq");
        return (
            core::ptr::null_mut(),
            core::ptr::null_mut(),
            core::ptr::null_mut(),
        );
    }

    let recv_cq = if recv_cq.is_null() { cq } else { recv_cq };
    // now test create the DC qp
    let mut qp_attr: ib_exp_qp_init_attr = Default::default();
    qp_attr.cap.max_send_wr = 128;
    qp_attr.cap.max_send_sge = 1;
    qp_attr.cap.max_recv_wr = 0;
    qp_attr.cap.max_recv_sge = 1;
    qp_attr.cap.max_inline_data = 64;
    qp_attr.sq_sig_type = ib_sig_type::IB_SIGNAL_REQ_WR;
    qp_attr.qp_type = ib_qp_type::IB_EXP_QPT_DC_INI;
    qp_attr.send_cq = cq;
    qp_attr.recv_cq = recv_cq;
    qp_attr.srq = srq;

    (unsafe { ib_create_qp_dct(pd, &mut qp_attr as _) }, cq, recv_cq)
}

/// modify DCT states
#[inline]
pub unsafe fn bring_dc_to_reset(qp: *mut ib_qp) -> bool {
    let mut attr: ib_qp_attr = Default::default();
    let mask: linux_kernel_module::c_types::c_int = ib_qp_attr_mask::IB_QP_STATE;

    attr.qp_state = ib_qp_state::IB_QPS_RESET;
    ib_modify_qp(qp, &mut attr as _, mask) == 0
}

/// Bring dc state during creation
#[inline]
pub fn bring_dc_to_ready(qp: *mut ib_qp) -> bool {
    if !unsafe { bring_dc_to_init(qp) } {
        println!("[dct] err to re-bring to init.");
        return false;
    }
    if !unsafe { bring_dc_to_rtr(qp) } {
        println!("[dct] err to re-bring to rtr.");
        return false;
    }
    if !unsafe { bring_dc_to_rts(qp) } {
        println!("[dct] err to re-bring to rts.");
        return false;
    }
    return true;
}

#[inline]
pub unsafe fn bring_dc_to_init(qp: *mut ib_qp) -> bool {
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

#[inline]
pub unsafe fn bring_dc_to_rtr(qp: *mut ib_qp) -> bool {
    let mut qp_attr: ib_qp_attr = Default::default();
    let mut mask: linux_kernel_module::c_types::c_int = ib_qp_attr_mask::IB_QP_STATE;

    qp_attr.qp_state = ib_qp_state::IB_QPS_RTR;

    qp_attr.path_mtu = ib_mtu::IB_MTU_4096;
    mask = mask | ib_qp_attr_mask::IB_QP_PATH_MTU;

    qp_attr.ah_attr.port_num = 1;

    qp_attr.ah_attr.sl = 0;
    qp_attr.ah_attr.ah_flags = 0;
    mask = mask | ib_qp_attr_mask::IB_QP_AV;

    ib_modify_qp(qp, &mut qp_attr as _, mask) == 0
}

#[inline]
pub unsafe fn bring_dc_to_rts(qp: *mut ib_qp) -> bool {
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

    ib_modify_qp(qp, &mut qp_attr as _, mask) == 0
}
