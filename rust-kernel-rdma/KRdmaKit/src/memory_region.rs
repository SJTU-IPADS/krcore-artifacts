use alloc::boxed::Box;
use alloc::sync::Arc;

use crate::context::Context;

/// The largest capcity of kernel-space MR is limited:
/// - kernel only supports access RDMA via physical memory,
/// whose consecutive physical addr range is limited by `kmalloc`.
/// By default, kernel only supports a maxmimum of 4MB allocation.
pub const MAX_CAPACITY: usize = 4 * 1024 * 1024; // 4MB

/// A memory region that abstracts the memory used by RDMA
pub struct MemoryRegion {
    ctx: Arc<Context>,
    data: Box<[core::mem::MaybeUninit<i8>]>,
}

#[derive(Debug)]
pub struct LocalKey(u32);
#[derive(Debug)]
pub struct RemoteKey(u32);

impl MemoryRegion {
    pub fn new(context: Arc<Context>, capacity: usize) -> Result<Self, crate::ControlpathError> {
        if capacity > MAX_CAPACITY {
            return Err(crate::ControlpathError::InvalidArg("MR size"));
        }
        Ok(Self {
            ctx: context,
            data: Box::new_zeroed_slice(capacity),
        })
    }

    /// Acquire an address that can be used for communication
    /// It is **unsafe** because if the MemoryRegion is destroyed,
    /// then the address will be invalid.
    #[inline]
    pub unsafe fn get_rdma_addr(&self) -> u64 {
        crate::rust_kernel_linux_util::bindings::bd_virt_to_phys(self.data.as_ptr() as _)
    }

    #[inline]
    pub fn rkey(&self) -> RemoteKey {
        RemoteKey(self.ctx.rkey())
    }

    #[inline]
    pub fn lkey(&self) -> LocalKey {
        LocalKey(self.ctx.lkey())
    }
}
