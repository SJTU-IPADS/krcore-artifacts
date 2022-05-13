#![allow(clippy::mut_from_ref)]

use core::cell::UnsafeCell;
pub struct ThreadLocal<T> {
    data: UnsafeCell<T>,
}
unsafe impl<T: Send + Sync> Send for ThreadLocal<T> {}
unsafe impl<T: Send + Sync> Sync for ThreadLocal<T> {}

impl<T> ThreadLocal<T> {
    #[inline]
    pub fn new(data: T) -> ThreadLocal<T> {
        Self {
            data: UnsafeCell::new(data),
        }
    }

    // we assume that self is thread_local
    #[inline(always)]
    pub fn get_mut(&self) -> &mut T {
        unsafe { &mut *self.data.get() }
    }
}

impl<'a, T> ThreadLocal<T> {
    pub fn get_ref(&'a self) -> &'a T {
        unsafe { &*self.data.get() }
    }
}

use core::ops::{Deref, DerefMut};

impl<T> Deref for ThreadLocal<T> {
    type Target = T;
    fn deref(&self) -> &T {
        unsafe { &*self.data.get() as &T }
    }
}

impl<T> DerefMut for ThreadLocal<T> {
    fn deref_mut(&mut self) -> &mut T {
        unsafe { &mut *self.data.get() as &mut T }
    }
}
