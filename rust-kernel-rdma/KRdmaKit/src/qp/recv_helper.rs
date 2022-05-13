use crate::rust_kernel_rdma_base::*;
use crate::rust_kernel_rdma_base::linux_kernel_module;

use linux_kernel_module::{Error, println};
use alloc::sync::Arc;
use alloc::boxed::Box;
use crate::mem::pa_to_va;

pub struct RecvHelper<const N: usize> {
    pub wrs: Box<[core::mem::MaybeUninit<ib_recv_wr>], VmallocAllocator>,
    pub sges: Box<[core::mem::MaybeUninit<ib_sge>], VmallocAllocator>,
    pub wcs: Box<[core::mem::MaybeUninit<ib_wc>], VmallocAllocator>,

    cur_idx: usize,
    capacity: usize,
    en_sz: usize,
}

impl<const N: usize> RecvHelper<N> {
    pub fn create(entry_size: usize, lkey: u32, base_addr: u64) -> Arc<Self> {
        let ret = Self {
            capacity: N,
            cur_idx: 0,
            wrs: Box::new_zeroed_slice_in(N as usize, VmallocAllocator),
            sges: Box::new_zeroed_slice_in(N as usize, VmallocAllocator),
            wcs: Box::new_zeroed_slice_in(N as usize, VmallocAllocator),
            en_sz: entry_size,
        };
        let mut boxed = Arc::new(ret);
        let recvs = unsafe { Arc::get_mut_unchecked(&mut boxed) };

        let mut base: u64 = base_addr;
        for i in 0..recvs.capacity {
            let sge = recvs.sges[i].as_mut_ptr();
            let wr = recvs.wrs[i].as_mut_ptr();

            unsafe {
                let base_va = { pa_to_va(base as *mut i8) } as u64;
                (*sge).addr = base as u64;
                (*sge).length = entry_size as u32;
                (*sge).lkey = lkey as u32;

                (*wr).num_sge = 1;
                (*wr).next = recvs.get_recv_wr_ptr(i + 1);
                (*wr).sg_list = sge as *mut _;
                bd_set_recv_wr_id(wr, base_va); // setup to the va
            }
            base += entry_size as u64; // come to next
        }
        boxed
    }

    #[inline]
    pub fn is_created(&self) -> bool {
        self.capacity > 0
    }

    #[inline]
    pub fn freeze_at(&mut self, i: usize) {
        let wr = self.get_recv_wr_ptr(i);
        unsafe {
            (*wr).next = core::ptr::null_mut();
        }
    }

    #[inline]
    pub fn freeze_done_at(&mut self, i: usize) {
        let wr = self.get_recv_wr_ptr(i);
        unsafe {
            (*wr).next = self.get_recv_wr_ptr(i + 1);
        }
    }

    #[inline]
    pub fn get_recv_wr_ptr(&mut self, idx: usize) -> *mut ib_recv_wr {
        self.wrs[idx % self.capacity].as_mut_ptr()
    }

    #[inline]
    pub fn header_ptr(&mut self) -> *mut ib_recv_wr {
        self.get_recv_wr_ptr(self.cur_idx)
    }

    #[inline]
    pub fn get_wc_header(&mut self) -> *mut ib_wc {
        self.wcs[0].as_mut_ptr()
    }

    #[inline]
    pub fn get_wc(&mut self, idx: usize) -> *mut ib_wc {
        self.wcs[idx % N].as_mut_ptr()
    }

    #[inline]
    pub fn get_cur_message(&self, base: *mut i8) -> *mut i8 {
        (base as u64 + (self.cur_idx * self.en_sz) as u64) as *mut i8
    }
}

impl<const N: usize> RecvHelper<N> {
    /// Post recv use the receive buffer directly
    /// srq: Use shared receive queue if set this to not null
    pub fn post_recvs(
        &mut self,
        qp: *mut ib_qp,
        srq: *mut ib_srq,
        buffer_sz: usize,
    ) -> linux_kernel_module::KernelResult<()> {
        let tail = (self.cur_idx + buffer_sz - 1) % self.capacity;

        let mut bad_wr: *mut ib_recv_wr = core::ptr::null_mut();
        self.freeze_at(tail);
        let err = unsafe {
            if srq.is_null() {
                bd_ib_post_recv(
                    qp,
                    self.header_ptr(),
                    &mut bad_wr as *mut _,
                )
            } else {
                bd_ib_post_srq_recv(
                    srq,
                    self.header_ptr(),
                    &mut bad_wr as *mut _,
                )
            }
        };
        self.freeze_done_at(tail);
        if err != 0 {
            println!("[recv helper] error when post recv {}", err);
            return Err(Error::from_kernel_errno(err));
        }
        self.cur_idx = (tail + 1) % self.capacity;
        Ok(())
    }

    #[inline]
    pub fn pop_recvs(
        &mut self,
        recv_cq: *mut ib_cq,
        recv_cnt: usize,
        offset: usize,
    ) -> linux_kernel_module::KernelResult<(Option<*mut ib_wc>, usize)> {
        if recv_cq.is_null() {
            return Err(Error::from_kernel_errno(0));
        }

        let wc_header = self.get_wc_header();
        let wc_ptr = self.get_wc(offset);

        let ret = unsafe {
            bd_ib_poll_cq(recv_cq,
                          recv_cnt as i32,
                          wc_ptr)
        };

        if ret < 0 {
            Err(Error::from_kernel_errno(ret))
        } else {
            Ok((Some(wc_header), ret as usize))
        }
    }
}

impl<const N: usize> Default for RecvHelper<N> {
    fn default() -> Self {
        Self {
            wrs: Box::new_zeroed_slice_in(N as usize, VmallocAllocator),
            sges: Box::new_zeroed_slice_in(N as usize, VmallocAllocator),
            wcs: Box::new_zeroed_slice_in(N as usize, VmallocAllocator),
            cur_idx: 0,
            capacity: 0,
            en_sz: 0,
        }
    }
}

unsafe impl<const N: usize> Sync for RecvHelper<N> {}

unsafe impl<const N: usize> Send for RecvHelper<N> {}

