use rust_kernel_rdma_base::*;
use crate::qp::{DoorbellHelper, RC};
use core::option::Option;
use linux_kernel_module::bindings::*;
use linux_kernel_module::{Error};
use crate::qp::recv_helper::RecvHelper;
use alloc::sync::Arc;

#[allow(unused_imports)]
pub struct RCOp<'a> {
    qp: &'a Arc<RC>,
}

impl<'a> RCOp<'a> {
    pub fn new(qp: &'a Arc<RC>) -> Self {
        Self {
            qp,
        }
    }

    #[inline]
    pub fn get_qp_status(&self) -> Option<u32> {
        self.qp.get_status()
    }
}

impl<'a> RCOp<'a> {
    #[inline]
    pub fn push_with_imm(
        &mut self,
        op: u32, // RDMA operation type (denote the type `ib_wr_opcode`)
        local_ptr: u64, // local mr address
        lkey: u32,      // local key
        sz: usize,      // request size (in Byte)
        remote_addr: u64,   // remote mr address
        rkey: u32,          // remote key
        imm_data: u32,
        send_flag: i32,
    ) -> linux_kernel_module::KernelResult<()> {
        let mut wr: ib_rdma_wr = Default::default();
        let mut sge: ib_sge = Default::default();

        // first set the sge
        sge.addr = local_ptr;
        sge.length = sz as u32;
        sge.lkey = lkey;

        // then set the wr
        wr.wr.opcode = op;
        wr.wr.send_flags = send_flag;
        wr.wr.ex.imm_data = imm_data;
        wr.remote_addr = remote_addr;
        wr.rkey = rkey;

        // should be able to send
        self.post_send(sge, wr)
    }

    #[inline]
    pub fn push(
        &mut self,
        op: u32, // RDMA operation type (denote the type `ib_wr_opcode`)
        local_ptr: u64, // local mr address
        lkey: u32,      // local key
        sz: usize,      // request size (in Byte)
        remote_addr: u64,   // remote mr address
        rkey: u32,          // remote key
        send_flag: i32,
    ) -> linux_kernel_module::KernelResult<()> {
        self.push_with_imm(op, local_ptr, lkey, sz, remote_addr, rkey, 0, send_flag)
    }

    /// pop the completion of the request from this QP
    #[inline]
    pub fn pop(&mut self) -> Option<*mut ib_wc> {
        let mut wc: ib_wc = Default::default();
        let ret = unsafe { bd_ib_poll_cq(self.qp.get_cq(), 1, &mut wc as *mut ib_wc) };
        if ret < 0 {
            return None;
        } else if ret == 1 {
            if wc.status != 0 && wc.status != 5 {
                linux_kernel_module::println!("rc poll cq error, wc_status:{}", wc.status);
            }
            return Some(&mut wc as *mut ib_wc);
        }
        None
    }

    #[inline]
    fn post_send(
        &mut self,
        mut sge: ib_sge,
        mut wr: ib_rdma_wr,
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
    pub fn wait_til_comp(&mut self) -> Option<*mut ib_wc> {
        use linux_kernel_module::println;
        let mut idx = 0;
        loop {
            let ret = self.pop();
            if ret.is_some() {
                return ret;
            }
            idx += 1;
            // if idx > 100000 {
            //     println!("time out when pop rc cq");
            //     break;
            // }
        }
        None
    }
}

/// Assemble function for Simple use
impl<'a> RCOp<'a> {
    #[inline]
    pub fn read(&mut self,
                local_ptr: u64,
                lkey: u32,
                sz: usize,
                remote_addr: u64,
                rkey: u32,
    ) -> linux_kernel_module::KernelResult<Option<*mut ib_wc>> {
        self.push(ib_wr_opcode::IB_WR_RDMA_READ,
                  local_ptr, lkey, sz, remote_addr, rkey, ib_send_flags::IB_SEND_SIGNALED)
            .and_then(|()| Ok(self.wait_til_comp()))
    }

    #[inline]
    pub fn write(&mut self,
                 local_ptr: u64,
                 lkey: u32,
                 sz: usize,
                 remote_addr: u64,
                 rkey: u32,
    ) -> linux_kernel_module::KernelResult<Option<*mut ib_wc>> {
        self.push(ib_wr_opcode::IB_WR_RDMA_WRITE,
                  local_ptr, lkey, sz, remote_addr, rkey, ib_send_flags::IB_SEND_SIGNALED)
            .and_then(|()| Ok(self.wait_til_comp()))
    }


    #[inline]
    pub fn send(&mut self,
                local_ptr: u64,
                lkey: u32,
                sz: usize,
                remote_addr: u64,
                rkey: u32,
    ) -> linux_kernel_module::KernelResult<Option<*mut ib_wc>> {
        self.push(ib_wr_opcode::IB_WR_SEND,
                  local_ptr, lkey, sz, remote_addr, rkey, ib_send_flags::IB_SEND_SIGNALED)
            .and_then(|()| Ok(self.wait_til_comp()))
    }
}


/// For two sided RC recv
impl RCOp<'_> {

    #[inline]
    pub fn pop_recv_with_shared(recv_cnt: usize,
                                offset: usize,
                                recv_cq: *mut ib_cq,
                                recv_buffer: &mut Arc<RecvHelper<2048>>) -> (Option<*mut ib_wc>, usize) {
        use linux_kernel_module::println;
        if recv_cq.is_null() {
            return (None, 0);
        }
        let wc_header =
            unsafe { Arc::get_mut_unchecked(recv_buffer) }.get_wc_header();
        let wc_ptr = unsafe { Arc::get_mut_unchecked(recv_buffer) }.get_wc(offset);


        let ret = unsafe {
            bd_ib_poll_cq(recv_cq,
                          recv_cnt as i32,
                          wc_ptr)
        };
        if ret < 0 {
            return (None, 0);
        } else if ret > 0 {
            return (Some(wc_header), ret as usize);
        }
        return (None, 0);
    }

    #[inline]
    pub fn post_doorbell(
        &mut self,
        doorbell: &mut DoorbellHelper,
    ) -> linux_kernel_module::KernelResult<()> {
        doorbell.freeze();
        let ret = self.post_send(doorbell.sges[0], doorbell.wrs[0]);
        doorbell.freeze_done();
        ret
    }
}
