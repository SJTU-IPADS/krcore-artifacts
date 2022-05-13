use crate::qp::DC;
use rust_kernel_rdma_base::*;
use crate::rust_kernel_rdma_base::linux_kernel_module::Error;
use crate::cm::EndPoint;


#[allow(unused_imports)]
pub struct DCOp<'a> {
    pub qp: &'a DC,
}

impl<'a> DCOp<'a> {
    pub fn new(qp: &'a DC) -> Self {
        Self {
            qp
        }
    }
}

impl DCOp<'_> {
    #[inline]
    pub fn push(&mut self,
                op: u32, // RDMA operation type (denote the type `ib_wr_opcode`)
                local_ptr: u64, // local mr address
                lkey: u32,      // local key
                sz: usize,      // request size (in Byte)
                remote_addr: u64,   // remote mr address
                rkey: u32,          // remote key
                end_point: &EndPoint,
                send_flag: i32,
    ) -> linux_kernel_module::KernelResult<()> {
        let mut wr: ib_dc_wr = Default::default();
        let mut sge: ib_sge = Default::default();

        let ah = end_point.ah;
        let dct_num = end_point.dct_num;
        // 1. set the sge
        sge.addr = local_ptr;
        sge.length = sz as _;
        sge.lkey = lkey;

        // 2. set the wr
        wr.wr.opcode = op;
        wr.wr.send_flags = send_flag;

        wr.remote_addr = remote_addr;
        wr.rkey = rkey;

        wr.ah = ah;
        wr.dct_access_key = 73;
        wr.dct_number = dct_num;
        self.post_send(sge, wr)
    }

    #[inline]
    pub fn push_with_imm(
        &mut self,
        op: u32, // RDMA operation type (denote the type `ib_wr_opcode`)
        local_ptr: u64, // local mr address
        lkey: u32,      // local key
        sz: usize,      // request size (in Byte)
        remote_addr: u64,   // remote mr address
        rkey: u32,          // remote key
        end_point: &EndPoint,
        send_flag: i32,
        imm_data: u32,
    ) -> linux_kernel_module::KernelResult<()> {
        let mut wr: ib_dc_wr = Default::default();
        let mut sge: ib_sge = Default::default();

        let ah = end_point.ah;
        let dct_num = end_point.dct_num;
        // 1. set the sge
        sge.addr = local_ptr;
        sge.length = sz as _;
        sge.lkey = lkey;

        // 2. set the wr
        wr.wr.opcode = op;
        wr.wr.send_flags = send_flag;
        wr.wr.ex.imm_data = imm_data;
        wr.remote_addr = remote_addr;
        wr.rkey = rkey;

        wr.ah = ah;
        wr.dct_access_key = 73;
        wr.dct_number = dct_num;
        self.post_send(sge, wr)
    }

    #[inline]
    fn post_send(&mut self,
                 mut sge: ib_sge,
                 mut wr: ib_dc_wr)
                 -> linux_kernel_module::KernelResult<()> {
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
        let mut cnt = 0;
        loop {
            let ret = self.pop();
            if ret.is_some() {
                return ret;
            }
            cnt += 1;
            if cnt > 10000 {
                linux_kernel_module::println!("dc op wait til comp timeout!");
                break;
            }
        }
        None
    }

    #[inline]
    pub fn pop(&mut self) -> Option<*mut ib_wc> {
        let mut wc: ib_wc = Default::default();
        let ret = unsafe { bd_ib_poll_cq(self.qp.get_cq(), 1, &mut wc as *mut ib_wc) };
        if ret < 0 {
            return None;
        } else if ret == 1 {
            return Some(&mut wc as *mut _);
        }
        None
    }
}