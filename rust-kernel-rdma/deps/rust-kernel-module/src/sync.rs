// SPDX-License-Identifier: MIT OR Apache-2.0
//
// Copyright (c) 2020-2021 Andre Richter <andre.o.richter@gmail.com>

//! Synchronization primitives.
//!
//! # Resources
//!
//!   - <https://doc.rust-lang.org/book/ch16-04-extensible-concurrency-sync-and-send.html>
//!   - <https://stackoverflow.com/questions/59428096/understanding-the-send-trait>
//!   - <https://doc.rust-lang.org/std/cell/index.html>

//--------------------------------------------------------------------------------------------------
// Public Definitions
//--------------------------------------------------------------------------------------------------

/// Any object implementing this trait guarantees exclusive access to the data wrapped within
/// the Mutex for the duration of the provided closure.
pub trait Mutex<'a, T: 'a> {
    /// The type of the data that is wrapped by this mutex.
    // type Data;

    /// Locks the mutex and grants the closure temporary mutable access to the wrapped data.
    fn lock_f<R>(&self, f: impl FnOnce(&'a mut T) -> R) -> R;
}
