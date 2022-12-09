use crate::context::Context;
#[allow(unused_imports)]
use rdma_shim::bindings::*;
use std::ptr::NonNull;
use std::sync::Arc;

/// When allocating a MemoryWindow, it is bound to a MR but not bound to a QP.
/// To bind a MW to a specific QP, you should use `post_bind_mw` or `bind_mw` method in QP.
#[allow(dead_code)]
pub struct MemoryWindow {
    ctx: Arc<Context>,
    inner_mw: NonNull<ibv_mw>,
}

/// Related to `ibv_mw_type`
pub enum MWType {
    Type1,
    Type2,
}

unsafe impl Send for MemoryWindow {}
unsafe impl Sync for MemoryWindow {}

impl MemoryWindow {
    /// Create an unbound MW from the given ctx and MW_TYPE
    pub fn new(context: Arc<Context>, mw_type: MWType) -> Result<Self, crate::ControlpathError> {
        let ibv_alloc_mw = unsafe { context.raw_ptr().as_ref().ops.alloc_mw.unwrap() };
        let mw_type = match mw_type {
            MWType::Type1 => ibv_mw_type::IBV_MW_TYPE_1,
            MWType::Type2 => ibv_mw_type::IBV_MW_TYPE_2,
        };
        let mw = NonNull::new(unsafe { ibv_alloc_mw(context.get_pd().as_ptr(), mw_type) }).ok_or(
            crate::ControlpathError::CreationError("Failed to create MR", rdma_shim::Error::EFAULT),
        )?;
        Ok(Self {
            ctx: context,
            inner_mw: mw,
        })
    }

    #[inline]
    pub fn inner_mw(&self) -> &NonNull<ibv_mw> {
        &self.inner_mw
    }

    #[inline]
    pub fn get_rkey(&self) -> u32 {
        unsafe { (*(self.inner_mw.as_ptr())).rkey }
    }

    #[inline]
    pub fn is_type_1(&self) -> bool {
        unsafe { (*self.inner_mw.as_ptr()).type_ == ibv_mw_type::IBV_MW_TYPE_1 }
    }

    #[inline]
    pub fn is_type_2(&self) -> bool {
        unsafe { (*self.inner_mw.as_ptr()).type_ == ibv_mw_type::IBV_MW_TYPE_2 }
    }
}

impl Drop for MemoryWindow {
    fn drop(&mut self) {
        let ibv_dealloc_mw = unsafe { self.ctx.raw_ptr().as_ref().ops.dealloc_mw.unwrap() };
        let errno = unsafe { ibv_dealloc_mw(self.inner_mw.as_ptr()) };
        if errno != 0 {
            eprintln!("Dealloc MW error : {}", errno)
        }
    }
}
