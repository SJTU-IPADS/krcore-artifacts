pub use rust_kernel_rdma_base::linux_kernel_module;

/// RDMA bindings
pub mod bindings { 
    pub use rust_kernel_rdma_base::bindings::*;
    pub use rust_kernel_rdma_base::ib_destroy_qp;
}

pub mod ffi { 
    pub use super::linux_kernel_module::{c_types, println};
}

pub mod utils { 
    pub use rust_kernel_rdma_base::rust_kernel_linux_util::timer::KTimer;
    pub use rust_kernel_rdma_base::rust_kernel_linux_util::kthread::sleep;
    pub use crate::linux_kernel_module::mutex::LinuxMutex;
    pub use crate::linux_kernel_module::sync::Mutex;
    
    pub use rust_kernel_rdma_base::rust_kernel_linux_util as log;
}

pub use rust_kernel_rdma_base::linux_kernel_module::println;

pub use rust_kernel_rdma_base::linux_kernel_module::Error;