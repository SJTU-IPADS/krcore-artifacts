#![cfg_attr(not(feature = "user"), no_std)]
#![allow(non_snake_case)]
#![feature(
    get_mut_unchecked,
    new_uninit,
    allocator_api,
    trusted_random_access,
    stmt_expr_attributes,
    vec_into_raw_parts
)]
#![cfg_attr(
    feature = "alloc_ref",
    feature(allocator_api, alloc_layout_extra, nonnull_slice_from_raw_parts)
)]

#[cfg(all(feature = "kernel", feature = "user"))]
compile_error!("features `crate/kernel` and `crate/user` are mutually exclusive");

extern crate alloc;

pub use completion_queue::{CompletionQueue, SharedReceiveQueue};

/// Configuration operations
pub mod consts;

/// Abstraction for the completion queues & queue pairs
pub mod completion_queue;
pub mod queue_pairs;
pub use queue_pairs::{DatagramEndpoint, QueuePair, QueuePairBuilder, QueuePairStatus};

/// Abstraction for the memory regions
pub mod memory_region;
pub use memory_region::MemoryRegion;

/// Abstraction for the RDMA-capable devices (RNIC)
pub mod device; // the new device implementation that will overwrite the old one

#[cfg(feature = "kernel")]
/// Communication manager (CM) abstracts the CM implementation
/// This module is used to bootstrap RDMA connection
pub mod comm_manager;

/// Services abstract necessary remote end to facialiate QP bring up
#[cfg(feature = "kernel")]
pub mod services;

#[cfg(feature = "user")]
pub mod services_user;

/// Analogy ibv_context in the ibverbs.
/// Provides a high-level context abstraction but further
/// abstracts MR and PD in it.
pub mod context;

#[cfg(feature = "kernel")]
pub mod kdriver;

#[cfg(feature = "user")]
pub mod udriver;

#[cfg(feature = "user")]
pub use udriver::{KDriverRef, UDriver};

#[cfg(feature = "kernel")]
pub use kdriver::{KDriver, KDriverRef};

pub mod random;
pub mod utils;

pub use rdma_shim;
use rdma_shim::utils::KTimer;
pub(crate) use rdma_shim::Error;

#[allow(unused_imports)]
use consts::*;

#[macro_export]
macro_rules! to_ptr {
    ($x:expr) => {
        &mut $x as *mut _
    };
}

pub use rdma_shim::log;

/// The error type of control plane operations.
/// These mainly include error of creating QPs, MRs, etc.
#[derive(thiserror_no_std::Error, Debug)]
pub enum ControlpathError {
    #[error("create context {0} error: {1}")]
    ContextError(&'static str, Error),

    /// Used for identify create different resource error
    /// e.g., CQ, QP, etc.
    #[error("create {0} error: {1}")]
    CreationError(&'static str, Error),

    #[error("Invalid arg for {0}")]
    InvalidArg(&'static str),

    #[error("Query error: {0} w/ errono: {1}")]
    QueryError(&'static str, Error),
}

/// The error type of data plane operations
#[derive(thiserror_no_std::Error, Debug)]
pub enum DatapathError {
    #[error("post_send error with errorno {0}")]
    PostSendError(Error),

    #[error("post_send error with errorno {0}")]
    PostRecvError(Error),

    #[error("poll_cq error with errorno {0}")]
    PollCQError(Error),

    #[error("timeout error")]
    TimeoutError,

    #[error("qp type error")]
    QPTypeError,
}

/// The error type of communication manager related.
/// This captures errors carried on during handshake processes.
#[derive(thiserror_no_std::Error, Debug)]
pub enum CMError {
    #[error("Timeout")]
    Timeout,

    #[error("Creation error with errorno: {0}")]
    Creation(i32),

    #[error("Failed to handle callback: {0}")]
    CallbackError(u32),

    #[error("CM send error")]
    SendError(&'static str, Error),

    #[error("Create server error")]
    ServerError(&'static str, Error),

    #[error("Invalid arg on {0}: {1}")]
    InvalidArg(&'static str, alloc::string::String),

    #[error("Unknown error")]
    Unknown,
}

/// profile for the network operations
pub struct Profile {
    timer: KTimer,
    total_op: u64,
    total_time_usec: [u64; 10],
}

impl Profile {
    pub fn new() -> Self {
        Self {
            timer: KTimer::new(),
            total_op: 0,
            total_time_usec: [0; 10],
        }
    }

    pub fn append_profile(&mut self, profile: &Profile) {
        self.total_op += profile.total_op;
        for i in 0..10 {
            self.total_time_usec[i] += profile.total_time_usec[i];
        }
    }

    #[inline]
    pub fn reset_timer(&mut self) {
        self.timer.reset();
    }

    #[inline]
    pub fn increase_op(&mut self, op_cnt: u64) {
        self.total_op += op_cnt;
    }

    #[inline]
    pub fn tick_record(&mut self, tick_idx: usize) {
        self.total_time_usec[tick_idx % 10] += self.timer.get_passed_usec() as u64;
    }

    #[inline]
    pub fn get_total_op_count(&self) -> u64 {
        self.total_op
    }

    #[inline]
    pub fn report(&self, tick_num: usize) {
        for i in 0..tick_num {
            let latency = self.total_time_usec[i] as f64 / self.total_op as f64;
            log::info!(
                "Profile[tick:{}] information: total time {} us, total op {}, \
        latency: {:.5} us/op",
                i,
                self.total_time_usec[i],
                self.total_op,
                latency
            );
        }
    }
}

impl Default for Profile {
    fn default() -> Self {
        Self {
            timer: KTimer::new(),
            total_op: 0,
            total_time_usec: [0; 10],
        }
    }
}

unsafe impl Sync for Profile {}

unsafe impl Send for Profile {}
