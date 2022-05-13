#![no_std]

//! A unified wrapper to the RDMA functions inside kernel. 
//!
//! Using RDMA inside kernel is non-trivial for rust. 
//! For example, different versions of OFED driver has different functions definitions, even for the same purpose.
//! Further, kernel RDMA functions are written in C, and some of the hidden struct need rust code to extract them, e.g., union. 
//!
//! This crate first provides bindings and then try to translate the functions in a unified definition.
//! The goal is to imitate user-level-RDMA as much as possible.
//! For example, user-space-RDMA may have something like `ib_alloc_pd`, while in kernel they maybe `__ib_alloc_pd`.
//!
#![feature(allocator_api, nonnull_slice_from_raw_parts, alloc_layout_extra)]

#[allow(non_upper_case_globals)]
#[warn(dead_code)]
const kModelName: &[u8] = b"rust-kernel-rdma-base\0";

pub use linux_kernel_module;
pub use rust_kernel_linux_util;

mod allocator;

pub use allocator::VmallocAllocator;

/// core module, direct export the kernel's internal C functions related to RDMA
/// note that we shall not use `pub use bindings::*;`
/// this is because it will export functions/types related to linux kernel
pub mod bindings;

/// module that contains functions transferred from raw kernel functions in a more unified way
mod funcs;

pub use funcs::*;

/// module that add utilities to raw C types
mod types;

pub use types::*;

/// Consts in RDMA usage
mod consts;

pub use consts::*;

#[cfg(BASE_MLNX_OFED_LINUX_4_4_2_0_7_0)]
mod ofed_4_4_2_0_7_0;

#[cfg(BASE_MLNX_OFED_LINUX_4_4_2_0_7_0)]
pub use ofed_4_4_2_0_7_0::*;

#[cfg(BASE_MLNX_OFED_LINUX_5_4_1_0_3_0)]
mod ofed_5_4_1_0_3_0;

#[cfg(BASE_MLNX_OFED_LINUX_5_4_1_0_3_0)]
pub use ofed_5_4_1_0_3_0::*;

#[cfg(BASE_MLNX_OFED_LINUX_4_9_3_1_5_0)]
mod ofed_4_9_3_1_5_0;

#[cfg(BASE_MLNX_OFED_LINUX_4_9_3_1_5_0)]
pub use ofed_4_9_3_1_5_0::*;

impl rdma_ah_attr { 
    pub unsafe fn get_ib_rlid(&self) -> u16 { 
        self.__bindgen_anon_1.ib.dlid
    }
}

pub use crate::bindings::rdma_ah_attr_type;
pub use crate::bindings::ib_wc_status;