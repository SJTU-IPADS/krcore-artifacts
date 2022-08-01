#![no_std]
#[cfg(all(feature = "kernel", feature = "user"))]
compile_error!("features `crate/kernel` and `crate/user` are mutually exclusive");

#[cfg(feature = "kernel")]
pub mod kernel;

#[cfg(feature = "kernel")]
pub use kernel::*;

#[cfg(feature = "user")]
pub mod user;

#[cfg(feature = "user")]
pub use user::*;