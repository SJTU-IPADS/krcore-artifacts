pub use rust_kernel_rdma_base::linux_kernel_module;
pub use rust_kernel_rdma_base::rust_kernel_linux_util;

/// RDMA bindings
pub mod bindings { 
    pub use rust_kernel_rdma_base::bindings::*;
    pub use rust_kernel_rdma_base::ib_create_qp;
    pub use rust_kernel_rdma_base::ib_destroy_qp;

    /// DCT-related bindings
    #[cfg(feature = "dct")]
    mod dct { 
        pub use rust_kernel_rdma_base::{ib_create_qp_dct, safe_ib_create_dct};
    }
    pub use dct::*;
}

pub mod ffi { 
    pub use super::linux_kernel_module::{c_types, println};
}

pub mod utils { 
    pub use rust_kernel_rdma_base::rust_kernel_linux_util::timer::KTimer;
    pub use rust_kernel_rdma_base::rust_kernel_linux_util::bindings::completion; 
    pub use rust_kernel_rdma_base::rust_kernel_linux_util::kthread::sleep;

    pub use crate::linux_kernel_module::mutex::LinuxMutex;
    pub use crate::linux_kernel_module::sync::Mutex;   
}

pub use rust_kernel_rdma_base::linux_kernel_module::println;
pub use rust_kernel_rdma_base::rust_kernel_linux_util as log;

pub use rust_kernel_rdma_base::linux_kernel_module::Error;
pub use rust_kernel_rdma_base::gen_add_dev_func;