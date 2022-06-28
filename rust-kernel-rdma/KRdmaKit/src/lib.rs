#![no_std]
#![feature(get_mut_unchecked, new_uninit, allocator_api)]
#![cfg_attr(
    feature = "alloc_ref",
    feature(allocator_api, alloc_layout_extra, nonnull_slice_from_raw_parts)
)]

/// Communication manager that is used to bootstrap RDMA connections
pub mod cm;

/// Configuration operations
pub mod consts;
pub mod ctrl;

/// Abstraction for the RDMA-capable devices (RNIC)
pub mod device;
pub mod device_v1; // the new device implementation that will overwrite the old one

/// Analogy ib_context in the ibverbs.
/// Provides a high-level context abstraction but further
/// abstracts MR and PD in it.
pub mod context;

pub mod ib_path_explorer;
pub mod mem;
pub mod net_util;
pub mod qp;
pub mod random;

pub mod rpc;
pub mod thread_local;

extern crate alloc;

use consts::*;
use linux_kernel_module::{c_types, println};
pub use rust_kernel_rdma_base;
use rust_kernel_rdma_base::rust_kernel_linux_util::timer::KTimer;
use rust_kernel_rdma_base::*;

#[macro_export]
macro_rules! to_ptr {
    ($x:expr) => {
        &mut $x as *mut _
    };
}

use alloc::vec::Vec;

pub struct KDriver {
    client: ib_client,
    rnics: Vec<crate::device_v1::DeviceRef>,
}

pub type KDriverRef = Arc<KDriver>;

use crate::log::debug;
use alloc::sync::Arc;
pub use rust_kernel_rdma_base::rust_kernel_linux_util as log;

impl KDriver {
    pub fn devices(&self) -> &Vec<crate::device_v1::DeviceRef> {
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
                crate::device_v1::Device::new(*dev, &temp)
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
gen_add_dev_func!(_KRdiver_add_one, KDriver_add_one);

#[allow(non_snake_case)]
unsafe extern "C" fn _KRdiver_remove_one(dev: *mut ib_device, _client_data: *mut c_types::c_void) {
    log::info!("remove one dev {:?}", dev);
}

/// The error type of control plane operations
#[derive(thiserror_no_std::Error, Debug)]
pub enum ControlpathError {
    #[error("create context {0} error: {1}")]
    ContextError(&'static str, linux_kernel_module::Error),
}

/// profile for statistics
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
