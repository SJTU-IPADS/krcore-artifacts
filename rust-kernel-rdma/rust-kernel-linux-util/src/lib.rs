#![no_std]

//! Utils used for native linux interface
#[allow(non_upper_case_globals)]
pub use linux_kernel_module;
/// core module, direct export the kernel's internal C functions related to RDMA
/// note that we shall not use `pub use bindings::*;`
/// this is because it will export functions/types related to linux kernel
pub mod bindings;
pub mod rwlock;
pub mod timer;
pub mod kthread;
pub mod string;

#[macro_use]
extern crate cfg_if;

pub mod level;
pub use level::LevelFilter;

#[macro_use]
pub mod macros;

#[allow(unused_imports)]
pub use linux_kernel_module::println;

cfg_if! {
    if #[cfg(feature = "static_log_check")] {
        // omit
        #[inline]
        pub fn set_max_level(_: LevelFilter) {
            unimplemented!()
        }

        #[inline(always)]
        pub fn max_level() -> LevelFilter {
            LevelFilter::max()
        }

    } else {
        use core::sync::atomic::Ordering;
        use core::sync::atomic::AtomicUsize;
        static MAX_LOG_LEVEL_FILTER: AtomicUsize = AtomicUsize::new(0);


    #[inline]
    pub fn set_max_level(level: LevelFilter) {
        MAX_LOG_LEVEL_FILTER.store(level as usize, Ordering::Relaxed)
    }

    #[inline(always)]
    pub fn max_level() -> LevelFilter {
        // Since `LevelFilter` is `repr(usize)`,
        // this transmute is sound if and only if `MAX_LOG_LEVEL_FILTER`
        // is set to a usize that is a valid discriminant for `LevelFilter`.
        // Since `MAX_LOG_LEVEL_FILTER` is private, the only time it's set
        // is by `set_max_level` above, i.e. by casting a `LevelFilter` to `usize`.
        // So any usize stored in `MAX_LOG_LEVEL_FILTER` is a valid discriminant.
        unsafe { core::mem::transmute(MAX_LOG_LEVEL_FILTER.load(Ordering::Relaxed)) }
    }

    } // end static log check
}

cfg_if! {
    if #[cfg(feature = "max_level_off")] {
        const MAX_LEVEL_INNER: LevelFilter = LevelFilter::Off;
    } else if #[cfg(feature = "max_level_error")] {
        const MAX_LEVEL_INNER: LevelFilter = LevelFilter::Error;
    } else if #[cfg(feature = "max_level_warn")] {
        const MAX_LEVEL_INNER: LevelFilter = LevelFilter::Warn;
    } else if #[cfg(feature = "max_level_info")] {
        const MAX_LEVEL_INNER: LevelFilter = LevelFilter::Info;
    } else if #[cfg(feature = "max_level_debug")] {
        const MAX_LEVEL_INNER: LevelFilter = LevelFilter::Debug;
    } else {
        const MAX_LEVEL_INNER: LevelFilter = LevelFilter::Trace;
    }
}
pub const STATIC_MAX_LEVEL: LevelFilter = MAX_LEVEL_INNER;
