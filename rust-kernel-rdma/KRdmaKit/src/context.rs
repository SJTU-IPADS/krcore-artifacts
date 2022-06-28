use rust_kernel_rdma_base::*;
use core::ptr::NonNull;

pub struct Context { 
    inner_device : crate::device_v1::DeviceRef,
    // kernel has little support for MR
    // so a global KMR is sufficient 
    kmr : NonNull<ib_mr>,

    pd : NonNull<ib_pd>, 
}

impl Context { 
    pub fn new(dev : &crate::device_v1::DeviceRef) -> Result<Self, crate::ControlpathError> { 
        unimplemented!();
    }
}

impl Drop for Context { 
    fn drop(&mut self) { 
        unsafe {             
            ib_dereg_mr(self.kmr.as_ptr());
            ib_dealloc_pd(self.pd.as_ptr());
        }
    }
}
