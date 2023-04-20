use crate::queue_pairs::{DatapathError, MemoryRegion, QueuePair};
use core::iter::TrustedRandomAccessNoCoerce;
use core::ops::Range;
use rdma_shim::bindings::*;

/// Typically, NIC doesn't expect a very large batch size.
///
/// A struct to help send doorbell.
/// It contains wrs and sges to save requests
///
pub struct DoorbellHelper {
    pub wr_list: Box<[ibv_send_wr]>,
    pub sg_list: Box<[ibv_sge]>,
    pub capacity: usize,
    cur_idx: isize,
}

impl DoorbellHelper {
    pub fn new_uninit(capacity: usize) -> Self {
        Self {
            capacity,
            cur_idx: -1,
            wr_list: unsafe { Box::new_zeroed_slice(capacity).assume_init() },
            sg_list: unsafe { Box::new_zeroed_slice(capacity).assume_init() },
        }
    }

    /// Create a DoorbellHelp, and initialize all its wr_list and sg_list
    /// # Arguments
    /// - `capacity` is the max batch size of the doorbell
    /// - `op` is the ib operation shared by all entries in this doorbell
    pub fn create_and_init(capacity: usize) -> Self {
        let mut ret = Self {
            capacity,
            cur_idx: -1,
            wr_list: unsafe { Box::new_zeroed_slice(capacity).assume_init() },
            sg_list: unsafe { Box::new_zeroed_slice(capacity).assume_init() },
        };
        ret.init();
        ret
    }

    #[inline]
    pub fn init(&mut self) {
        for i in 0..self.capacity {
            self.wr_list[i].num_sge = 1;
            self.wr_list[i].next = &mut self.wr_list[(i + 1) % self.capacity] as *mut ibv_send_wr;
            self.wr_list[i].sg_list = &mut self.sg_list[i] as *mut ibv_sge;
        }
    }

    #[inline]
    pub fn sanity_check(&self) -> bool {
        let mut ret = true;
        for i in 0..self.capacity {
            let sge_ptr = &(self.sg_list[i]) as *const ibv_sge;
            let wr_sg_list = self.wr_list[i].sg_list;
            ret &= (sge_ptr as u64) == (wr_sg_list as u64);
        }
        ret
    }

    /// Return current batching size
    #[inline]
    pub fn size(&self) -> isize {
        self.cur_idx + 1
    }
    #[inline]
    pub fn is_empty(&self) -> bool {
        self.size() <= 0
    }
    #[inline]
    pub fn empty(&mut self) {
        self.cur_idx = -1;
    }
    #[inline]
    pub fn is_full(&self) -> bool {
        self.size() >= self.capacity as isize
    }

    /// Get the next doorbell entry
    /// # Return value
    /// - `true` means the doorbell batching size is less than `capacity`, it is ok to add a new doorbell
    /// - `false` means doorbell is full, cannot add new entry
    ///
    /// User shall check its return value
    #[inline]
    pub fn next(&mut self) -> bool {
        if self.is_full() {
            return false;
        }
        self.cur_idx += 1;
        true
    }
}

impl DoorbellHelper {
    // Before flushing the doorbell, we must freeze it to prevent adding
    #[inline]
    pub fn freeze(&mut self) {
        assert!(!self.is_empty()); // should not be empty
        self.cur_wr().next = core::ptr::null_mut();
    }

    // After flushing the doorbell, unfreeze it
    #[inline]
    pub fn freeze_done(&mut self) {
        assert!(!self.is_empty());
        if self.cur_idx == (self.capacity - 1) as isize {
            self.wr_list[self.cur_idx as usize].next = &mut self.wr_list[0] as *mut ib_send_wr;
        } else {
            self.wr_list[self.cur_idx as usize].next =
                &mut self.wr_list[(self.cur_idx + 1) as usize] as *mut ib_send_wr;
        }
    }

    #[inline]
    pub fn clear(&mut self) {
        self.freeze_done();
        self.cur_idx = -1;
    }
    // Return the ptr to current doorbell entry's wr
    #[inline]
    pub fn cur_wr(&mut self) -> &mut ibv_send_wr {
        return if self.is_empty() {
            &mut self.wr_list[0]
        } else {
            &mut self.wr_list[self.cur_idx as usize]
        };
    }
    // Return the ptr to current doorbell entry's sge
    #[inline]
    pub fn cur_sge(&mut self) -> &mut ibv_sge {
        return if self.is_empty() {
            &mut self.sg_list[0]
        } else {
            &mut self.sg_list[self.cur_idx as usize]
        };
    }

    #[inline]
    pub fn first_wr_ptr(&mut self) -> *mut ibv_send_wr {
        &mut self.wr_list[0] as *mut ibv_send_wr
    }

    /// Return the ptr to specified doorbell entry's wr
    ///
    /// ## WARN
    /// No check for idx. The caller has to take care of it by himself
    #[inline]
    pub fn get_wr_ptr(&mut self, idx: usize) -> *mut ibv_send_wr {
        &mut self.wr_list[idx] as *mut ibv_send_wr
    }

    /// Return the ptr to specified doorbell entry's sge
    #[inline]
    pub fn get_sge_ptr(&mut self, idx: usize) -> *mut ibv_sge {
        &mut self.sg_list[idx] as *mut ibv_sge
    }
}

pub struct RcDoorbellHelper {
    send_doorbell: DoorbellHelper,
}

impl RcDoorbellHelper {
    pub fn create(capacity: usize) -> Self {
        Self {
            send_doorbell: DoorbellHelper::create_and_init(capacity),
        }
    }

    ///Init RcDoorbellHelper's internal doorbell with specific IBV_WR_OPCODE
    /// Since one-sided test may read or write,
    /// we leave the init(op) to be called by user to delay initialization.
    #[inline]
    pub fn init(&mut self) {
        self.send_doorbell.init();
    }

    /// Post WR to `send_doorbell`'s next entry
    ///
    /// Return the current size of the doorbell batch list
    pub fn post_send_write(
        &mut self,
        mr: &MemoryRegion,
        range: Range<u64>,
        signaled: bool,
        raddr: u64,
        rkey: u32,
        wr_id: u64,
    ) -> Result<usize, DatapathError> {
        if !self.send_doorbell.next() {
            return Ok((self.send_doorbell.cur_idx + 1) as usize);
        }
        /* set sge for current wr */
        self.send_doorbell.cur_sge().addr = unsafe { mr.get_rdma_addr() + range.start };
        self.send_doorbell.cur_sge().length = range.size() as u32;
        self.send_doorbell.cur_sge().lkey = mr.lkey().0;

        /* set wr fields */
        let send_flag: i32 = if signaled {
            ibv_send_flags::IBV_SEND_SIGNALED as i32
        } else {
            0
        };
        self.send_doorbell.cur_wr().wr_id = wr_id;
        self.send_doorbell.cur_wr().opcode = ibv_wr_opcode::IBV_WR_RDMA_WRITE;

        #[cfg(feature = "OFED_5_4")]
        {
            self.send_doorbell.cur_wr().send_flags = send_flag as u32;
        }

        #[cfg(not(feature = "OFED_5_4"))]
        {
            self.send_doorbell.cur_wr().send_flags = send_flag;
        }
        unsafe {
            self.send_doorbell.cur_wr().wr.rdma.as_mut().remote_addr = raddr;
            self.send_doorbell.cur_wr().wr.rdma.as_mut().rkey = rkey;
        }
        // no need to set imm_data for read/write
        Ok((self.send_doorbell.cur_idx + 1) as usize)
    }

    pub fn post_send_read(
        &mut self,
        mr: &MemoryRegion,
        range: Range<u64>,
        signaled: bool,
        raddr: u64,
        rkey: u32,
        wr_id: u64,
    ) -> Result<usize, DatapathError> {
        if !self.send_doorbell.next() {
            return Ok((self.send_doorbell.cur_idx + 1) as usize);
        }
        /* set sge for current wr */
        self.send_doorbell.cur_sge().addr = unsafe { mr.get_rdma_addr() + range.start };
        self.send_doorbell.cur_sge().length = range.size() as u32;
        self.send_doorbell.cur_sge().lkey = mr.lkey().0;

        /* set wr fields */
        let send_flag: i32 = if signaled {
            ibv_send_flags::IBV_SEND_SIGNALED as i32
        } else {
            0
        };
        self.send_doorbell.cur_wr().wr_id = wr_id;
        self.send_doorbell.cur_wr().opcode = ibv_wr_opcode::IBV_WR_RDMA_READ;

        #[cfg(feature = "OFED_5_4")]
        {
            self.send_doorbell.cur_wr().send_flags = send_flag as u32;
        }

        #[cfg(not(feature = "OFED_5_4"))]
        {
            self.send_doorbell.cur_wr().send_flags = send_flag;
        }
        unsafe {
            self.send_doorbell.cur_wr().wr.rdma.as_mut().remote_addr = raddr;
            self.send_doorbell.cur_wr().wr.rdma.as_mut().rkey = rkey;
        }
        // no need to set imm_data for read/write
        Ok((self.send_doorbell.cur_idx + 1) as usize)
    }

    pub fn post_send_cas(
        &mut self,
        mr: &MemoryRegion,
        offset: u64,
        signaled: bool,
        raddr: u64,
        rkey: u32,
        wr_id: u64,
        old: u64,
        new: u64,
    ) -> Result<usize, DatapathError> {
        if !self.send_doorbell.next() {
            return Ok((self.send_doorbell.cur_idx + 1) as usize);
        }
        /* set sge for current wr */
        self.send_doorbell.cur_sge().addr = unsafe { mr.get_rdma_addr() + offset };
        self.send_doorbell.cur_sge().length = 8;
        self.send_doorbell.cur_sge().lkey = mr.lkey().0;

        /* set wr fields */
        let send_flag: i32 = if signaled {
            ibv_send_flags::IBV_SEND_SIGNALED as i32
        } else {
            0
        };

        self.send_doorbell.cur_wr().wr_id = wr_id;
        self.send_doorbell.cur_wr().opcode = ibv_wr_opcode::IBV_WR_ATOMIC_CMP_AND_SWP;
        self.send_doorbell.cur_wr().send_flags = send_flag as _;

        unsafe {
            self.send_doorbell.cur_wr().wr.atomic.as_mut().remote_addr = raddr;
            self.send_doorbell.cur_wr().wr.atomic.as_mut().compare_add = old;
            self.send_doorbell.cur_wr().wr.atomic.as_mut().swap = new;
            self.send_doorbell.cur_wr().wr.atomic.as_mut().rkey = rkey;
        }
        // no need to set imm_data for read/write
        Ok((self.send_doorbell.cur_idx + 1) as usize)
    }

    pub fn post_send_faa(
        &mut self,
        mr: &MemoryRegion,
        offset: u64,
        signaled: bool,
        raddr: u64,
        rkey: u32,
        wr_id: u64,
        val: u64,
    ) -> Result<usize, DatapathError> {
        if !self.send_doorbell.next() {
            return Ok((self.send_doorbell.cur_idx + 1) as usize);
        }
        /* set sge for current wr */
        self.send_doorbell.cur_sge().addr = unsafe { mr.get_rdma_addr() + offset };
        self.send_doorbell.cur_sge().length = 8;
        self.send_doorbell.cur_sge().lkey = mr.lkey().0;

        /* set wr fields */
        let send_flag = if signaled {
            ibv_send_flags::IBV_SEND_SIGNALED
        } else {
            0
        };

        self.send_doorbell.cur_wr().wr_id = wr_id;
        self.send_doorbell.cur_wr().opcode = ibv_wr_opcode::IBV_WR_ATOMIC_FETCH_AND_ADD;
        self.send_doorbell.cur_wr().send_flags = send_flag as _;

        unsafe {
            self.send_doorbell.cur_wr().wr.atomic.as_mut().remote_addr = raddr;
            self.send_doorbell.cur_wr().wr.atomic.as_mut().compare_add = val;
            self.send_doorbell.cur_wr().wr.atomic.as_mut().swap = 0;
            self.send_doorbell.cur_wr().wr.atomic.as_mut().rkey = rkey;
        }
        // no need to set imm_data for read/write
        Ok((self.send_doorbell.cur_idx + 1) as usize)
    }

    #[inline]
    pub fn flush_doorbell(&mut self, qp: &QueuePair) -> Result<(), DatapathError> {
        self.send_doorbell.freeze();
        let res = qp.post_send_wr(self.send_doorbell.first_wr_ptr());
        self.send_doorbell.clear();
        res
    }
}

unsafe impl Send for RcDoorbellHelper {}
unsafe impl Sync for RcDoorbellHelper {}
