use rust_kernel_rdma_base::*;
use linux_kernel_module::{println};
use linux_kernel_module::bindings::mutex;
use alloc::sync::Arc;
use crate::device::RContext;
use crate::qp::{create_dc_qp, bring_dc_to_ready, Config};
use crate::cm::EndPoint;
use crate::mem::TempMR;
use core::ptr::null_mut;

#[repr(C, align(8))]
#[derive(Copy, Clone, Debug, Default)]
pub struct DCTargetMeta {
    pub dct_num: u32,
    pub lid: u16,
    pub gid: ib_gid,
    pub mr: TempMR,
}

impl DCTargetMeta {
    pub unsafe fn from_raw(meta_ptr: *mut DCTargetMeta) -> Option<Self> {
        if meta_ptr.is_null() {
            return None;
        }
        let meta = *meta_ptr;
        Some(meta.clone())
    }
}

pub struct DCTServer {
    dct: *mut ib_dct,
    cq: *mut ib_cq,
    srq: *mut ib_srq,
    recv_cq: *mut ib_cq,
}

impl DCTServer {
    pub fn new(ctx: &RContext) -> Option<Arc<Self>> {
        let config: crate::qp::Config = Config::new().set_max_recv_wr_sz(4096);
        Self::new_from_config(config, ctx)
    }

    pub fn new_from_config(config: crate::qp::Config, ctx: &RContext) -> Option<Arc<Self>> {
        let pd = ctx.get_pd();
        let device = ctx.get_raw_dev();

        // start creation
        let res = Self {
            dct: core::ptr::null_mut(),
            cq: core::ptr::null_mut(),
            srq: core::ptr::null_mut(),
            recv_cq: core::ptr::null_mut(),
        };
        let mut boxed = Arc::new(res);
        let dc_server = unsafe { Arc::get_mut_unchecked(&mut boxed) };


        let mut cq_attr: ib_cq_init_attr = Default::default();
        cq_attr.cqe = config.max_cqe as u32;

        let cq = unsafe {
            ib_create_cq(
                device,
                None,
                None,
                core::ptr::null_mut(),
                &mut cq_attr as *mut _,
            )
        };

        if cq.is_null() {
            println!("[dct] err dct server cq null");
        }

        let recv_cq = unsafe {
            ib_create_cq(
                device,
                None,
                None,
                core::ptr::null_mut(),
                &mut cq_attr as *mut _,
            )
        };

        if recv_cq.is_null() {
            println!("[dct] err dct server recv cq null");
        }

        // create srq
        let mut cq_attr: ib_srq_init_attr = Default::default();
        cq_attr.attr.max_wr = config.max_send_wr_sz as u32;
        cq_attr.attr.max_sge = config.max_send_sge as u32;

        let srq = unsafe { ib_create_srq(pd, &mut cq_attr as _) };

        if srq.is_null() {
            println!("[dct] null srq");
        }

        let mut dctattr: ib_dct_init_attr = Default::default();
        dctattr.pd = pd;
        dctattr.cq = cq;
        dctattr.srq = srq;
        dctattr.dc_key = 73;
        dctattr.port = 1;
        dctattr.access_flags =
            ib_access_flags::IB_ACCESS_LOCAL_WRITE |
                ib_access_flags::IB_ACCESS_REMOTE_READ |
                ib_access_flags::IB_ACCESS_REMOTE_WRITE |
                ib_access_flags::IB_ACCESS_REMOTE_ATOMIC;
        dctattr.min_rnr_timer = 2;
        dctattr.tclass = 0;
        dctattr.flow_label = 0;
        dctattr.mtu = ib_mtu::IB_MTU_4096;
        dctattr.pkey_index = 0;
        dctattr.hop_limit = 1;
        dctattr.inline_size = 60;

        let dct = unsafe { safe_ib_create_dct(pd, &mut dctattr as _) };

        dc_server.srq = srq;
        dc_server.cq = cq;
        dc_server.recv_cq = recv_cq;
        dc_server.dct = dct;


        Some(boxed)
    }
}


impl DCTServer {
    #[inline]
    pub fn get_dct_num(&self) -> u32 {
        unsafe { (*self.dct).dct_num }
    }

    #[inline]
    pub fn get_cq(&self) -> *mut ib_cq { self.cq }

    #[inline]
    pub fn get_recv_cq(&self) -> *mut ib_cq { self.recv_cq }

    #[inline]
    pub fn get_srq(&self) -> *mut ib_srq { self.srq }
}

impl Drop for DCTServer {
    fn drop(&mut self) {
        if !self.dct.is_null() {
            unsafe { ib_exp_destroy_dct(self.dct, core::ptr::null_mut()) };
            // self.dct = null_mut();
        }

        if !self.srq.is_null() {
            unsafe { ib_destroy_srq(self.srq) };
            // self.srq = null_mut();
        }

        if !self.cq.is_null() {
            unsafe { ib_free_cq(self.cq) };
            // self.cq = null_mut();
        }

        if !self.recv_cq.is_null() {
            unsafe { ib_free_cq(self.recv_cq) };
            // self.recv_cq = null_mut();
        }
    }
}

impl Default for DCTServer {
    fn default() -> Self {
        Self {
            dct: core::ptr::null_mut(),
            cq: core::ptr::null_mut(),
            srq: core::ptr::null_mut(),
            recv_cq: core::ptr::null_mut(),
        }
    }
}

unsafe impl Sync for DCTServer {}

unsafe impl Send for DCTServer {}

type LinuxMutexInner = mutex;

pub struct DC {
    qp: *mut ib_qp,
    cq: *mut ib_cq,
    recv_cq: *mut ib_cq,
}

impl DC {
    #[inline]
    pub fn new(ctx: &RContext) -> Option<Arc<Self>> {
        DC::new_with_srq(ctx, null_mut(), null_mut())
    }


    pub fn new_with_srq(ctx: &RContext, srq: *mut ib_srq, recv_cq: *mut ib_cq) -> Option<Arc<Self>> {
        let res = Self {
            qp: core::ptr::null_mut(),
            cq: core::ptr::null_mut(),
            recv_cq: core::ptr::null_mut(),
        };
        let mut boxed = Arc::new(res);
        let qp = unsafe { Arc::get_mut_unchecked(&mut boxed) };

        let (_qp, cq, recv_cq) = create_dc_qp(ctx, srq, recv_cq);
        if _qp.is_null() {
            println!("[dct] err create dc qp.");
            return None;
        }
        if cq.is_null() {
            println!("[dct] err create dc cq.");
            return None;
        }

        if recv_cq.is_null() {
            println!("[dct] err null recv cq.");
            return None;
        }

        // bring status
        if !bring_dc_to_ready(_qp) {
            println!("[dct] err to bring to ready.");
            return None;
        }
        qp.qp = _qp;
        qp.cq = cq;
        qp.recv_cq = recv_cq;

        Some(boxed)
    }
}

impl DC {
    #[inline]
    pub fn get_qp(&self) -> *mut ib_qp {
        self.qp
    }

    #[inline]
    pub fn get_cq(&self) -> *mut ib_cq {
        self.cq
    }

    #[inline]
    pub fn get_recv_cq(&self) -> *mut ib_cq {
        self.recv_cq
    }
}

impl Default for DC {
    fn default() -> Self {
        Self {
            qp: null_mut(),
            cq: null_mut(),
            recv_cq: null_mut(),
        }
    }
}

impl Drop for DC {
    fn drop(&mut self) {
        // free qp
        if !self.qp.is_null() {
            unsafe { ib_destroy_qp(self.qp) };
        }
        // free cq
        if !self.cq.is_null() {
            unsafe { ib_free_cq(self.cq) };
        }
    }
}

unsafe impl Sync for DC {}

unsafe impl Send for DC {}