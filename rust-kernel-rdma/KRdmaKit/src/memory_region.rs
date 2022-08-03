#[allow(unused_imports)]
use rdma_shim::bindings::*;

use core::ffi::c_void;

#[allow(unused_imports)]
use core::ptr::NonNull;

use alloc::boxed::Box;
use alloc::sync::Arc;

use crate::context::Context;

/// The largest capcity of kernel-space MR is limited:
/// - kernel only supports access RDMA via physical memory,
/// whose consecutive physical addr range is limited by `kmalloc`.
/// By default, kernel only supports a maxmimum of 4MB allocation.
pub const MAX_CAPACITY: usize = 4 * 1024 * 1024; // 4MB

/// A memory region that abstracts the memory used by RDMA
/// 
/// # Examples
/// 
/// - To allocate a memory region (1KB) and register it to RDMA:
/// 
/// ``` text,ignore
///         let ctx = crate::UDriver::create()
///            .expect("failed to query device")
///            .devices()
///            .into_iter()
///            .next()
///            .expect("no rdma device available")
///            .open_context()
///            .expect("failed to create RDMA context");
///
///        let mr = super::MemoryRegion::new(ctx.clone(), 1024);
/// ```
/// 
/// - The user can further pass a raw pointer to the memory region. 
/// However, it is highly unnsafe and the user should take care to manage the 
/// lifecycles of the memory region pointed by the raw pointer:
/// 
/// ```text,ignore
/// 
///        let mut test_buf: alloc::vec::Vec<u64> = alloc::vec![1, 2, 3, 4];
///        
///        let mr = unsafe {
///            super::MemoryRegion::new_from_raw(
///                ctx.clone(),
///                test_buf.as_mut_ptr() as _,
///                test_buf.len() * core::mem::size_of::<u64>(),
///                (rdma_shim::bindings::ib_access_flags::IBV_ACCESS_LOCAL_WRITE
///                    | ib_access_flags::IBV_ACCESS_REMOTE_READ
///                    | ib_access_flags::IBV_ACCESS_REMOTE_WRITE
///                    | ib_access_flags::IBV_ACCESS_REMOTE_ATOMIC)
///                    .0 as _,
///            )
///        }; 
///        // if the test_buf is deallocated later, the `mr` may still be used
/// ```
/// 
#[allow(dead_code)]
pub struct MemoryRegion {
    ctx: Arc<Context>,

    data: *mut c_void,
    capacity: usize,

    is_raw_ptr: bool,

    #[cfg(feature = "user")]
    mr: NonNull<ibv_mr>,
}

#[derive(Debug)]
pub struct LocalKey(pub u32);
#[derive(Debug)]
pub struct RemoteKey(pub u32);

impl MemoryRegion {
    /// Kernel part:
    ///
    ///
    /// User part:
    ///
    ///
    pub fn new(context: Arc<Context>, capacity: usize) -> Result<Self, crate::ControlpathError> {
        // kernel malloc has a max capacity
        // not a problem for the user-space applications
        #[cfg(feature = "kernel")]
        if capacity > MAX_CAPACITY {
            return Err(crate::ControlpathError::InvalidArg("MR size"));
        }

        #[allow(unused_mut)]        
        let mut data: Box<[core::mem::MaybeUninit<i8>]> = Box::new_uninit_slice(capacity);

        #[cfg(feature = "user")]
        let mr = NonNull::new(unsafe {
            rdma_shim::bindings::ibv_reg_mr(
                context.get_pd().as_ptr(),
                data.as_mut_ptr() as *mut _,
                capacity,
                // FIXME: maybe we should enable different permissions
                (rdma_shim::bindings::ib_access_flags::IBV_ACCESS_LOCAL_WRITE
                    | ib_access_flags::IBV_ACCESS_REMOTE_READ
                    | ib_access_flags::IBV_ACCESS_REMOTE_WRITE
                    | ib_access_flags::IBV_ACCESS_REMOTE_ATOMIC)
                    .0 as _,
            )
        })
        .ok_or(crate::ControlpathError::CreationError(
            "Failed to create MR",
            rdma_shim::Error::EFAULT,
        ))?;

        Ok(Self {
            ctx: context,
            data: Box::into_raw(data) as _,
            capacity: capacity,
            is_raw_ptr: false,
            #[cfg(feature = "user")]
            mr: mr,
        })
    }

    /// New from a raw pointer provided by the user
    /// Note:
    /// - This function is highly unsafe, because:
    /// 1) we don't check the integrity of the pointer
    /// 2) we don't check the lifecycle of a good pointer
    /// User should be able to ensure the correctness of the above two factors
    ///
    #[cfg(feature = "user")]
    pub unsafe fn new_from_raw(
        context: Arc<Context>,
        ptr: *mut rdma_shim::ffi::c_types::c_void,
        capacity: usize,
        mr_flags: u32,
    ) -> Result<Self, crate::ControlpathError> {
        let mr = NonNull::new(rdma_shim::bindings::ibv_reg_mr(
            context.get_pd().as_ptr(),
            ptr,
            capacity,
            mr_flags as _,
        ))
        .ok_or(crate::ControlpathError::CreationError(
            "Failed to create MR",
            rdma_shim::Error::EFAULT,
        ))?;

        Ok(Self {
            ctx: context,
            data: ptr,
            capacity: capacity,
            is_raw_ptr: true,
            mr: mr,
        })
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
        rdma_shim::rust_kernel_linux_util::bindings::bd_virt_to_phys(self.data as _)
    }

    pub fn get_virt_addr(&self) -> u64 {
        self.data as u64
    }


    #[inline]
    pub fn rkey(&self) -> RemoteKey {
        #[cfg(feature = "kernel")]
        return RemoteKey(self.ctx.rkey());

        #[cfg(feature = "user")]
        return RemoteKey(unsafe { self.mr.as_ref().rkey });
    }

    #[inline]
    pub fn lkey(&self) -> LocalKey {
        #[cfg(feature = "kernel")]
        return LocalKey(self.ctx.lkey());

        #[cfg(feature = "user")]
        return LocalKey(unsafe { self.mr.as_ref().lkey });
    }

    /// Total size of the memory region
    pub fn capacity(&self) -> usize {
        self.capacity
    }
}

impl Drop for MemoryRegion {
    fn drop(&mut self) {
        #[cfg(feature = "user")]
        unsafe { ibv_dereg_mr(self.mr.as_ptr()) };

        if self.is_raw_ptr {
            // user-passed raw pointer, do nothing
        } else {
            // will just free it
            unsafe { Box::from_raw(self.data) };
        }
    }
}

#[cfg(feature = "user")]
#[cfg(test)]
mod tests {
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
        assert_eq!(mr.unwrap().capacity(), 1024);
    }

    #[test]
    fn test_mr_raw() {
        let ctx = crate::UDriver::create()
            .expect("failed to query device")
            .devices()
            .into_iter()
            .next()
            .expect("no rdma device available")
            .open_context()
            .expect("failed to create RDMA context");

        use rdma_shim::bindings::ib_access_flags;

        let mut test_buf: alloc::vec::Vec<u64> = alloc::vec![1, 2, 3, 4];
        
        let mr = unsafe {
            super::MemoryRegion::new_from_raw(
                ctx.clone(),
                test_buf.as_mut_ptr() as _,
                test_buf.len() * core::mem::size_of::<u64>(),
                (rdma_shim::bindings::ib_access_flags::IBV_ACCESS_LOCAL_WRITE
                    | ib_access_flags::IBV_ACCESS_REMOTE_READ
                    | ib_access_flags::IBV_ACCESS_REMOTE_WRITE
                    | ib_access_flags::IBV_ACCESS_REMOTE_ATOMIC)
                    .0 as _,
            )
        }; 

        assert!(mr.is_ok());

        let mr = unsafe {
            super::MemoryRegion::new_from_raw(
                ctx.clone(),
                test_buf.as_mut_ptr() as _,
                1024 * 1024 * 1024,
                (rdma_shim::bindings::ib_access_flags::IBV_ACCESS_LOCAL_WRITE
                    | ib_access_flags::IBV_ACCESS_REMOTE_READ
                    | ib_access_flags::IBV_ACCESS_REMOTE_WRITE
                    | ib_access_flags::IBV_ACCESS_REMOTE_ATOMIC)
                    .0 as _,
            )
        };         
        assert!(mr.is_err());

    }
}
