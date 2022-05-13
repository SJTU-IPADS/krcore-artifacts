#![no_std]
#![feature(get_mut_unchecked, new_uninit, min_const_generics, allocator_api)]

pub mod device;
pub mod consts;
pub mod thread_local;
pub mod mem;
pub mod qp;
pub mod net_util;
pub mod cm;
pub mod ib_path_explorer;
pub mod ctrl;
pub mod random;

extern crate alloc;


pub use rust_kernel_rdma_base;
use consts::*;
use rust_kernel_rdma_base::*;
use linux_kernel_module::{println};
use rust_kernel_rdma_base::rust_kernel_linux_util::timer::KTimer;

#[macro_export]
macro_rules! to_ptr {
    ($x:expr) => {
        &mut $x as *mut _
    };
}
static mut SA_CLIENT: ib_sa_client =
    unsafe { core::mem::transmute([0u8; core::mem::size_of::<ib_sa_client>()]) };


/// Subnet administrator client
///
/// Allow clients to communicate with the _sa_.
/// The _sa_ contains important
/// information, such as path records, that are needed to establish connections.
pub struct SAClient {}


impl SAClient {
    pub fn create() -> Self {
        unsafe { ib_sa_register_client(to_ptr!(SA_CLIENT)) };
        println!("sa client registered");
        Self {}
    }

    #[inline]
    pub fn get_inner_sa_client(&self) -> *mut ib_sa_client {
        unsafe { to_ptr!(SA_CLIENT) }
    }

    #[inline]
    pub fn reset(&self) {
        unsafe { ib_sa_unregister_client(to_ptr!(SA_CLIENT)) };
        println!("sa client exit");
    }
}


impl Drop for SAClient {
    fn drop(&mut self) {
        self.reset();
    }
}

unsafe impl Sync for SAClient {}

unsafe impl Send for SAClient {}


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
            println!("Profile[tick:{}] information: total time {} us, total op {}, \
        latency:{:.5} us/op", i, self.total_time_usec[i], self.total_op, latency);
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

