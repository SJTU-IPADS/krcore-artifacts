use rust_kernel_rdma_base::*;
use crate::qp::UD;
use linux_kernel_module::bindings::*;
use linux_kernel_module::{Error, println};
use crate::cm::EndPoint;
use crate::mem::pa_to_va;

/// The UDOp supports the UD operations.
///
/// Note the const `N` means the length of receive buffer.
pub struct UDOp<'a> {
    qp: &'a UD,
}


impl<'a> UDOp<'a> {
    pub fn new(qp: &'a UD) -> Self {
        Self {
            qp,
        }
    }
}

impl<'a> UDOp<'a> {
    #[inline]
    pub fn push(
        &mut self,
        op: u32, // RDMA operation type (denote the type `ib_wr_opcode`)
        local_ptr: u64,
        lkey: u32,
        end_point: &EndPoint,
        sz: usize,
        imm_data: u32,
        send_flag: i32,
    ) -> linux_kernel_module::KernelResult<()> {
        let mut wr: ib_ud_wr = Default::default();
        let mut sge: ib_sge = Default::default();

        // first set the sge
        sge.addr = local_ptr;
        sge.length = sz as u32;
        sge.lkey = lkey;

        // then set the wr
        wr.remote_qpn = end_point.qpn as u32;
        wr.remote_qkey = end_point.qkey as u32;

        wr.ah = end_point.ah;
        wr.wr.opcode = op;
        wr.wr.send_flags = send_flag;
        wr.wr.ex.imm_data = imm_data;
        self.post_send(sge, wr)
    }


    #[inline]
    pub fn send(
        &mut self,
        local_ptr: u64,
        lkey: u32,
        end_point: &EndPoint,
        sz: usize,
    ) -> linux_kernel_module::KernelResult<ib_wc> {
        let mut wr: ib_ud_wr = Default::default();
        let mut sge: ib_sge = Default::default();

        // first set the sge
        sge.addr = local_ptr;
        sge.length = sz as u32;
        sge.lkey = lkey;

        // then set the wr
        wr.remote_qpn = end_point.qpn as u32;
        wr.remote_qkey = end_point.qkey as u32;

        wr.ah = end_point.ah;
        wr.wr.opcode = ib_wr_opcode::IB_WR_SEND;
        wr.wr.send_flags = ib_send_flags::IB_SEND_SIGNALED;
        // self.post_send(sge, wr)
        self.post_send(sge, wr)
            .and_then(|()| Ok(self.wait_one_cq()))
    }


    pub fn post_send(
        &mut self,
        mut sge: ib_sge,
        mut wr: ib_ud_wr,
    ) -> linux_kernel_module::KernelResult<()> {
        // 1. reset the wr
        wr.wr.sg_list = &mut sge as *mut _;
        wr.wr.num_sge = 1;

        let mut bad_wr: *mut ib_send_wr = core::ptr::null_mut();
        let err = unsafe {
            bd_ib_post_send(
                self.qp.get_qp(),
                &mut wr.wr as *mut _,
                &mut bad_wr as *mut _,
            )
        };

        if err != 0 {
            return Err(Error::from_kernel_errno(err));
        }

        Ok(())
    }

    #[inline]
    pub unsafe fn post_send_raw(&mut self,
                                wr: *mut ib_ud_wr)
                                -> linux_kernel_module::KernelResult<()> {
        let mut bad_wr: *mut ib_send_wr = core::ptr::null_mut();
        let err = bd_ib_post_send(
            self.qp.get_qp(),
            &mut (*wr).wr as *mut _,
            &mut bad_wr as *mut _
        );

        if err != 0 {
            return Err(Error::from_kernel_errno(err));
        }

        Ok(())
    }

    #[inline]
    pub fn wait_one_cq(&mut self) -> ib_wc {
        let mut wc: ib_wc = Default::default();
        let mut idx = 0;
        loop {
            let ret = unsafe { bd_ib_poll_cq(self.qp.get_cq(), 1, &mut wc as *mut ib_wc) };
            if ret < 0 {
                println!("not ok {}!!!", ret);
                return wc;
            }
            if ret == 1 {
                if 0 != wc.status {
                    println!("send poll comp error, wc_status:{}", wc.status);
                }
                return wc;
            }
            idx += 1;
            if idx > 1000 {
                println!("wait one cq timeout, ret:{}", ret);
                break;
            }
        }
        wc
    }

    #[inline]
    pub fn wait_til_comp(&mut self) -> Option<*mut ib_wc> {
        loop {
            let ret = self.pop();
            if ret.is_some() {
                return ret;
            }
        }
    }

    #[inline]
    pub fn pop(&mut self) -> Option<*mut ib_wc> {
        let mut wc: ib_wc = Default::default();
        let ret = unsafe { bd_ib_poll_cq(self.qp.get_cq(), 1, &mut wc as *mut ib_wc) };
        if ret < 0 {
            return None;
        } else if ret == 1 {
            if wc.status != 0 && wc.status != 5 {
                linux_kernel_module::println!("ud poll cq error, wc_status:{}", wc.status);
            }
            return Some(&mut wc as *mut ib_wc);
        }
        None
    }

    #[inline]
    pub fn pop_recv_cq(&mut self, pop_cnt: u32, wc_list: *mut ib_wc) -> Option<usize> {
        let ret = unsafe { bd_ib_poll_cq(self.qp.get_recv_cq(), pop_cnt as i32, wc_list) };
        if ret < 0 {
            return None;
        } else if ret > 0 {
            return Some(ret as usize);
        }
        None
    }

    #[inline]
    pub fn post_recv(
        &mut self,
        local_ptr: u64,
        lkey: u32,
        sz: usize,
    ) -> linux_kernel_module::KernelResult<()> {
        let mut wr: ib_recv_wr = Default::default();
        let mut sge: ib_sge = Default::default();

        sge.addr = local_ptr as u64;
        sge.length = sz as u32;
        sge.lkey = lkey as u32;

        // 1. reset the wr
        wr.sg_list = &mut sge as *mut _;
        wr.num_sge = 1;
        unsafe { bd_set_recv_wr_id(&mut wr, pa_to_va(local_ptr as *mut i8)) };
        let mut bad_wr: *mut ib_recv_wr = core::ptr::null_mut();
        let err = unsafe {
            bd_ib_post_recv(
                self.qp.get_qp(),
                &mut wr as *mut _,
                &mut bad_wr as *mut _,
            )
        };

        if err != 0 {
            println!("error when post recv {}", err);
            return Err(Error::from_kernel_errno(err));
        }

        Ok(())
    }
}
