pub use rust_user_rdma::*;

#[allow(non_camel_case_types)]
mod wrapper_types {
    use super::*;

    /// We follow the convention of the kernel to rename the user-space verbs types
    /// types:
    pub type ib_device = ibv_device;
    pub type ib_device_attr = ibv_device_attr;

    pub type ib_pd = ibv_pd;
    pub type ib_ah = ibv_ah;
    pub type ib_gid = ibv_gid;

    pub type rdma_ah_attr = ibv_ah_attr;

    /// functions
    pub type rdma_create_ah_wrapper = ibv_create_ah;
}

pub use wrapper_types::*;