#![no_std]
#![feature(get_mut_unchecked, new_uninit, allocator_api, trusted_random_access,stmt_expr_attributes)]
#![cfg_attr(
    feature = "alloc_ref",
    feature(allocator_api, alloc_layout_extra, nonnull_slice_from_raw_parts)
)]

extern crate alloc;
use alloc::sync::Arc;

pub use completion_queue::{CompletionQueue, SharedReceiveQueue};

/// Configuration operations
pub mod consts;

/// Abstraction for the RDMA-capable devices (RNIC)
pub mod device; // the new device implementation that will overwrite the old one

/// Communication manager (CM) abstracts the CM implementation
/// This module is used to bootstrap RDMA connection
pub mod comm_manager;

/// Analogy ib_context in the ibverbs.
/// Provides a high-level context abstraction but further
/// abstracts MR and PD in it.
pub mod context;

/// Services abstract necessary remote end to facialiate QP bring up
pub mod services;

/// Abstraction for the completion queues 
pub mod completion_queue;

/// Abstraction for the QPs, including UD, RC and DC. 
pub mod queue_pairs;

/// Abstraction for the memory regions
pub mod memory_region;

pub mod utils;
pub mod random;

pub use rdma_shim; 
use rdma_shim::utils::{sleep, KTimer};
use rdma_shim::bindings::*;
use rdma_shim::ffi::c_types;
pub(crate) use rdma_shim::{Error, println};

use consts::*;

#[macro_export]
macro_rules! to_ptr {
    ($x:expr) => {
        &mut $x as *mut _
    };
}

use alloc::vec::Vec;

pub struct KDriver {
    client: ib_client,
    rnics: Vec<device::DeviceRef>,
}

pub type KDriverRef = Arc<KDriver>;

pub use rdma_shim::log;

impl KDriver {
    pub fn devices(&self) -> &Vec<device::DeviceRef> {
        &self.rnics
    }

    /// ! warning: this function is **not** thread safe
    pub unsafe fn create() -> Option<Arc<Self>> {
        let mut temp = Arc::new(KDriver {
            client: core::mem::MaybeUninit::zeroed().assume_init(),
            rnics: Vec::new(),
        });

        // First, we query all the ib_devices
        {
            let temp_inner = Arc::get_mut_unchecked(&mut temp);

            _NICS = Some(Vec::new());

            temp_inner.client.name = b"kRdmaKit\0".as_ptr() as *mut c_types::c_char;
            temp_inner.client.add = Some(KDriver_add_one);
            temp_inner.client.remove = Some(_KRdiver_remove_one);

            let err = ib_register_client((&mut temp_inner.client) as _);
            if err != 0 {
                return None;
            }
        }

        // next, weconstruct the nics
        // we need to do this to avoid move out temp upon constructing the devices
        let rnics = get_temp_rnics()
            .into_iter()
            .map(|dev| {
                device::Device::new(*dev, &temp)
                    .expect("Query ib_device pointers should never fail")
            })
            .collect();

        // modify the temp again
        {
            let temp_inner = Arc::get_mut_unchecked(&mut temp);
            temp_inner.rnics = rnics;
            _NICS.take();
        }

        log::debug!("KRdmaKit driver initialization done. ");

        Some(temp)
    }
}

impl Drop for KDriver {
    fn drop(&mut self) {
        unsafe { ib_unregister_client(&mut self.client) };
    }
}

/* helper functions & parameters for bootstrap */
static mut _NICS: Option<Vec<*mut ib_device>> = None;

unsafe fn get_temp_rnics() -> &'static mut Vec<*mut ib_device> {
    match _NICS {
        Some(ref mut x) => &mut *x,
        None => panic!(),
    }
}

#[allow(non_snake_case)]
unsafe extern "C" fn _KRdiver_add_one(dev: *mut ib_device) {
    //    let nic = crate::device::RNIC::create(dev, 1);
    //    get_temp_rnics().push(nic.ok().unwrap());
    get_temp_rnics().push(dev);
}
rdma_shim::gen_add_dev_func!(_KRdiver_add_one, KDriver_add_one);

#[allow(non_snake_case)]
unsafe extern "C" fn _KRdiver_remove_one(dev: *mut ib_device, _client_data: *mut c_types::c_void) {
    log::info!("remove one dev {:?}", dev);
}

/// The error type of control plane operations
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
            println!(
                "Profile[tick:{}] information: total time {} us, total op {}, \
        latency: {:.5} us/op",
                i, self.total_time_usec[i], self.total_op, latency
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

impl core::fmt::Debug for KDriver {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        f.debug_struct("KDriver")
            .field("num_device", &self.rnics.len())
            .finish()
    }
}
