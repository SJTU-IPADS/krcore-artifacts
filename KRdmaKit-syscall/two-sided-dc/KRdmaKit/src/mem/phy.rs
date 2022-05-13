use crate::mem::{va_to_pa, Memory};
use crate::device::RContext;
use crate::linux_kernel_module::{KernelResult};
use core::option::Option;
use alloc::boxed::Box;

/// Register memory using the physical memory
/// Note, the memory should be allocated from kmalloc
/// Denote as one large DMA block waited to use, and support the virtual/physical address translation.
#[derive(Debug, Clone)]
pub struct RMemPhy {
    // inner box
    inner_mem: Box<[core::mem::MaybeUninit<i8>]>,

    // physical DMA address.
    dma_buf: Option<u64>,
    // size of the block
    capacity: usize,
}

// KBlock only supports small memory capacity
// e.g., 4K or 16K, or < 1M
#[allow(dead_code)]
impl RMemPhy {
    pub fn new(capacity: usize) -> Self {
        // no need to persist this ptr, since the dma global mr could access the whole memory
        let ptr: Box<[core::mem::MaybeUninit<i8>]> =
            Box::new_zeroed_slice(capacity as usize);
        Self {
            inner_mem: ptr,
            capacity,
            dma_buf: None,
        }
    }
    #[inline]
    fn get_pa_unsafe(&self, off: u64) -> u64 {
        self.dma_buf.unwrap() + off
    }

    #[inline]
    pub fn get_dma_buf(&mut self) -> u64 {
        self.get_pa(0)
    }
}

impl Memory for RMemPhy {
    fn get_ptr(&self) -> *mut i8 {
        self.inner_mem[0].as_ptr() as _
    }

    fn get_pa(&mut self, off: u64) -> u64 {
        if self.dma_buf.is_none() {
            // Setup dma physical address
            unsafe { self.dma_buf = Some(va_to_pa(self.get_ptr())); }
        }

        self.get_pa_unsafe(off)
    }

    fn get_sz(&self) -> u64 {
        self.capacity as u64
    }

    fn pin_for_rdma(&mut self, _ctx: &RContext) -> KernelResult<()> {
        if self.dma_buf.is_none() {
            // Setup dma physical address
            self.dma_buf = Some(unsafe { va_to_pa(self.get_ptr()) });
        } else {
            // Do nothing
        }
        Ok(())
    }
}