#[allow(unused_imports)]
use rdma_shim::bindings::*;

use core::ptr::NonNull;

use alloc::sync::Arc;
use alloc::vec::Vec;

use crate::context::Context;

/// The largest capcity of kernel-space MR is limited:
/// - kernel only supports access RDMA via physical memory,
/// whose consecutive physical addr range is limited by `kmalloc`.
/// By default, kernel only supports a maxmimum of 4MB allocation.
pub const MAX_CAPACITY: usize = 4 * 1024 * 1024; // 4MB

/// A memory region that abstracts the memory used by RDMA
#[allow(dead_code)]
pub struct MemoryRegion {
    ctx: Arc<Context>,
    data: Vec<i8>,
    raw_ptr: bool,

    #[cfg(feature = "user")]
    mr: NonNull<ibv_mr>,
}

#[derive(Debug)]
pub struct LocalKey(pub u32);
#[derive(Debug)]
pub struct RemoteKey(pub u32);

impl MemoryRegion {
    pub fn new(context: Arc<Context>, capacity: usize) -> Result<Self, crate::ControlpathError> {
        // kernel malloc has a max capacity
        // not a problem for the user-space applications
        #[cfg(feature = "kernel")]
        if capacity > MAX_CAPACITY {
            return Err(crate::ControlpathError::InvalidArg("MR size"));
        }

        #[cfg(feature = "user")]
        let mr = NonNull::new(core::ptr::null_mut()).ok_or(
            crate::ControlpathError::CreationError("Failed to create MR", rdma_shim::Error::EFAULT),
        )?;

       let mut data = Vec::with_capacity(capacity);
        data.resize(capacity, 0); // FIXME: do we really need this resize? 

        Ok(Self {
            ctx: context,
            data : data,
            raw_ptr: false,
            #[cfg(feature = "user")]
            mr: mr,
        })
    }

    pub unsafe fn new_from_raw(
        context: Arc<Context>,
        ptr: *mut rdma_shim::ffi::c_types::c_void,
        capacity: usize,
    ) -> Result<Self, crate::ControlpathError> {
        unimplemented!();
    }

    /// Acquire an address that can be used for communication
    /// It is **unsafe** because if the MemoryRegion is destroyed,
    /// then the address will be invalid.
    ///
    /// It is only used in the kernel: user-space has MR support,
    /// and can directly use the virtual address
    #[cfg(feature = "kernel")]
    #[inline]
    pub unsafe fn get_rdma_addr(&self) -> u64 {
        rdma_shim::rust_kernel_linux_util::bindings::bd_virt_to_phys(self.data.as_ptr() as _)
    }

    #[inline]
    pub fn rkey(&self) -> RemoteKey {
        #[cfg(feature = "kernel")]
        return RemoteKey(self.ctx.rkey());

        unimplemented!()
    }

    #[inline]
    pub fn lkey(&self) -> LocalKey {
        #[cfg(feature = "kernel")]
        return LocalKey(self.ctx.lkey());

        unimplemented!()
    }
}

impl Drop for MemoryRegion {
    fn drop(&mut self) {
        #[cfg(feature = "user")]
        if self.raw_ptr {
            let old_data = core::mem::take(&mut self.data);
            core::mem::forget(old_data);
        }
    }
}

#[cfg(feature = "user")]
#[cfg(test)]
mod tests {
    use rdma_shim::log;

    #[test]
    fn test_mr_basic() {
        let ctx = crate::UDriver::create()
            .expect("failed to query device")
            .devices()
            .into_iter()
            .next()
            .expect("no rdma device available")
            .open_context()
            .expect("failed to create RDMA context");

        let mr = super::MemoryRegion::new(ctx.clone(), 1024);
        assert!(mr.is_ok());
    }
}
