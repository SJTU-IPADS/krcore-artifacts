//! Consts used in the whole KRdmaKit module
use rust_kernel_rdma_base::*;

pub const IB_PD_UNSAFE_GLOBAL_RKEY: usize = 0x01;
pub const CONNECT_TIME_OUT_MS: linux_kernel_module::c_types::c_int = 5000; // connect time out. set 5 seconds

pub const AF_INET: sa_family_t = 2;
pub const AF_UNSPEC: sa_family_t = 0;

pub const MAX_RD_ATOMIC: usize = 16;

// max kmalloc size in kernel module (4M)
pub const MAX_KMALLOC_SZ: usize = 1024 * 1024 * 4;
pub const K_REG_ALWAYS: bool = true;

// RPC related
pub const DEFAULT_RPC_HINT: usize = 73;
pub const UD_HEADER_SZ: usize = 40;

pub const DEFAULT_TWO_SIDED_RC_VID: usize = 1024;