//! A basic runtime structure for Rust async programming
//!
//! This module provides runtime functionality for the cooperative scheduling
//!
//! # Organization
//!
//! - [`waitable::Waitable`] allows you to build your own waiting function by implementing this trait.
//! - [`worker_group::WorkerGroup`] controls [`worker::Worker`]'s lifetime, you can submit new workers to it and spawn tasks on a specific worker
//! - [`worker::Worker`] is the core struct of this runtime. Each `Worker` runs in a separate thread and has its own context. A worker must be started manually.
//! 
//!
//!
//!
//!
//!


mod error;
mod task;
mod tid;
mod wake_fn;

pub mod waitable;
pub mod worker_group;
pub mod worker;

pub use task::yield_now;
