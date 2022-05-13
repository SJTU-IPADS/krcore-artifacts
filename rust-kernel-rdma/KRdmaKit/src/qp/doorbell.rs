use rust_kernel_rdma_base::*;
pub const DEFAULT_BATCH_SZ: usize = 64;

pub struct DoorbellHelper {
    pub wrs: [ib_rdma_wr; DEFAULT_BATCH_SZ],
    pub sges: [ib_sge; DEFAULT_BATCH_SZ],

    cur_idx: isize,
    capacity: usize,
}

impl DoorbellHelper {
    pub fn create(capacity: usize, op: u32) -> Self {
        let mut ret = Self {
            capacity,
            cur_idx: -1,
            wrs: [Default::default(); DEFAULT_BATCH_SZ],
            sges: [Default::default(); DEFAULT_BATCH_SZ],
        };
        for i in 0..capacity {
            ret.wrs[i].wr.opcode = op;
            ret.wrs[i].wr.num_sge = 1;
            ret.wrs[i].wr.next = &mut ret.wrs[(i + 1) % capacity].wr as *mut ib_send_wr;
            ret.wrs[i].wr.sg_list = &mut ret.sges[i] as *mut ib_sge;
        }
        ret
    }

    pub fn size(&self) -> isize {
        self.cur_idx + 1
    }

    pub fn is_empty(&self) -> bool {
        self.size() <= 0
    }

    pub fn empty(&mut self) {
        self.cur_idx = -1;
    }

    pub fn is_full(&self) -> bool {
        self.size() >= self.capacity as isize
    }

    pub fn next(&mut self) -> bool {
        if self.is_full() {
            return false;
        }
        self.cur_idx += 1;
        true
    }
}

impl DoorbellHelper {
    #[allow(non_upper_case_globals)]
    #[inline]
    pub fn freeze(&mut self) {
        assert!(!self.is_empty());  // should not be empty
        self.cur_wr().wr.next = core::ptr::null_mut();
    }

    #[allow(non_upper_case_globals)]
    #[inline]
    pub fn freeze_done(&mut self) {
        assert!(!self.is_empty());
        if self.cur_idx == (self.capacity - 1) as isize {
            self.wrs[self.cur_idx as usize].wr.next = &mut self.wrs[0].wr as *mut ib_send_wr;
        } else {
            self.wrs[self.cur_idx as usize].wr.next = &mut self.wrs[(self.cur_idx + 1) as usize].wr as *mut ib_send_wr;
        }
    }

    #[allow(non_upper_case_globals)]
    #[inline]
    pub fn clear(&mut self) {
        self.freeze_done();
        self.cur_idx = -1;
    }

    #[allow(non_upper_case_globals)]
    #[inline]
    pub fn cur_wr(&mut self) -> &mut ib_rdma_wr {
        return if self.is_empty() {
            &mut self.wrs[0]
        } else {
            &mut self.wrs[self.cur_idx as usize]
        };
    }

    #[allow(non_upper_case_globals)]
    #[inline]
    pub fn cur_sge(&mut self) -> &mut ib_sge {
        return if self.is_empty() {
            &mut self.sges[0]
        } else {
            &mut self.sges[self.cur_idx as usize]
        };
    }

    #[allow(non_upper_case_globals)]
    #[inline]
    pub fn first_wr_ptr(&mut self) -> *mut ib_send_wr {
        &mut self.wrs[0].wr as *mut ib_send_wr
    }

    /// No check for idx. The caller has to take care of it by himself
    #[allow(non_upper_case_globals)]
    #[inline]
    pub fn get_wr_ptr(&mut self, idx: usize) -> *mut ib_rdma_wr {
        &mut self.wrs[idx] as *mut ib_rdma_wr
    }

    pub fn get_sge_ptr(&mut self, idx: usize) -> *mut ib_sge {
        &mut self.sges[idx] as *mut ib_sge
    }
}
