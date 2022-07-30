#![no_std]

#[cfg(feature = "kernel")]
pub mod kernel;

#[cfg(feature = "kernel")]
pub use kernel::*;

#[cfg(feature = "user")]
pub mod user;

#[cfg(feature = "user")]
pub use user::*;
