#![no_std]

//! Utils used for native linux interface
#[allow(non_upper_case_globals)]

pub use linux_kernel_module;
pub mod timer;

/// core module, direct export the kernel's internal C functions related to RDMA
/// note that we shall not use `pub use bindings::*;`
/// this is because it will export functions/types related to linux kernel
pub mod bindings;
pub mod rwlock;
