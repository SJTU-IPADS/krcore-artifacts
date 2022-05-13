#[allow(
clippy::all,
non_camel_case_types,
non_upper_case_globals,
non_snake_case,
improper_ctypes,
non_upper_case_globals,
dead_code
)]
mod bindings {
    use linux_kernel_module::c_types;
    include!(concat!(env!("OUT_DIR"), "/bindings-", env!("CARGO_PKG_NAME"), ".rs"));
}

pub use bindings::*;
use crate::linux_kernel_module::{KernelResult, Error, c_types};
use core::fmt;

#[allow(non_snake_case)]
pub fn rdma__msecs_to_jiffies(m: c_types::c_uint) -> c_types::c_ulong {
    unsafe { __msecs_to_jiffies(m) }
}

impl fmt::Debug for completion {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("completion")
            .field("done", &self.done)
            //            .field("wait", &self.wait)
            .finish()
    }
}


impl completion {
    pub fn init(&mut self) {
        unsafe { bd_init_completion(self as *mut completion) };
    }

    pub fn wait(&mut self, timeout_msecs: linux_kernel_module::c_types::c_int) -> KernelResult<()> {
        let ret = unsafe {
            wait_for_completion_interruptible_timeout(
                self as *mut completion,
                rdma__msecs_to_jiffies(timeout_msecs as u32) + 1,
            )
        };
        if ret == 0 {
            return Err(Error::from_kernel_errno(0));
        }
        if ret < 0 {
            return Err(Error::from_kernel_errno(ret as i32));
        }
        Ok(())
    }

    pub fn done(&mut self) {
        unsafe { complete(self as *mut completion) };
    }
}


