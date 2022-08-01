pub use rust_user_rdma::*;

#[allow(non_camel_case_types)]
/// We follow the convention of the kernel to rename the user-space verbs types
pub type ib_device = ibv_device;
