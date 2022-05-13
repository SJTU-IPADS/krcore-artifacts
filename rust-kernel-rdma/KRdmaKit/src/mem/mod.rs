//! Memory module.
//! Including the basic abstraction and operation to all of the memory resources may appear in the
//! applications.
//! handles memory registration in the kernel
mod phy;

/// Register the memory and access it as physical memory
pub use phy::RMemPhy;

mod virt;

pub use virt::RMemVirt;

// export
use crate::device::RContext;

use crate::rust_kernel_rdma_base::*;
use crate::rust_kernel_rdma_base::linux_kernel_module;
use linux_kernel_module::c_types::c_void;
use rust_kernel_linux_util::bindings::{bd_virt_to_phys, bd_phys_to_virt};
use core::slice;

#[inline]
pub unsafe fn va_to_pa(va: *mut i8) -> u64 {
    bd_virt_to_phys(va.cast::<c_void>())
}

#[inline]
pub unsafe fn pa_to_va(pa: *mut i8) -> u64 {
    bd_phys_to_virt(pa.cast::<c_void>())
}

pub trait Memory {
    /// Memory base address in form of virtual address.
    /// Usually this virt-address is transformed into physical address via `virt_to_phys`
    fn get_ptr(&self) -> *mut i8;

    /// Safely check if the DMA physical address exists. If not the physical will
    /// be transformed by the memory base address(virt_to_phys)
    fn get_pa(&mut self, off: u64) -> u64;

    /// Get the memory size.
    fn get_sz(&self) -> u64;

    /// Pin to one RDMA context
    fn pin_for_rdma(&mut self, ctx: &RContext) -> linux_kernel_module::KernelResult<()>;

    /// Not in use right now.
    /// Address translation in the single memory.
    /// Param `va` means the virtual address (same virtual memory space with the Memory).
    /// So the `va` should be no less than self memory base address, and no more than the whole memory
    /// limitation.
    ///
    fn va_to_pa(&self, va: *mut i8) -> u64 {
        let base = self.get_ptr() as u64;
        let ptr = va as u64;
        // assert!(ptr >= base);
        let _off = ptr - base;
        // assert!(off <= self.get_sz());
        //          self.get_pa_unsafe(off)
        unimplemented!();
    }

    /// Transform from `isize` type offset into the specific virtual address ptr.
    fn get_off(&self, off: isize) -> *mut i8 {
        unsafe { self.get_ptr().offset(off) }
    }

    /// Empty all of the memory unit.
    fn zero(&mut self) {
        let s = unsafe { slice::from_raw_parts_mut(self.get_ptr(), self.get_sz() as usize) };
        for i in 0..self.get_sz() {
            s[i as usize] = 0;
        }
    }
}

/// TmpMR serves as the simple
#[repr(C, align(8))]
#[derive(Copy, Clone, Debug, Default)]
pub struct TempMR {
    addr: u64,
    capacity: u32,
    rkey: u32,
}

impl TempMR {
    pub fn new(v: u64, c: u32, k: u32) -> Self {
        Self {
            addr: v,
            capacity: c,
            rkey: k,
        }
    }

    pub fn get_rkey(&self) -> u32 {
        self.rkey
    }

    pub fn get_addr(&self) -> u64 {
        self.addr
    }

    pub fn get_capacity(&self) -> u32 {
        self.capacity
    }
}

#[derive(Copy, Clone)]
pub struct RegAttr {
    pub buf: u64,
    pub size: u64,
    pub lkey: u32,
    pub rkey: u32,
}