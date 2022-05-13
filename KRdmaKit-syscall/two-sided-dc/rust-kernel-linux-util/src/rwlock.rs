// from https://ipads.se.sjtu.edu.cn:1312/distributed-rdma-serverless/rust-linux-kernel-module/-/blob/master/src/rwlock.rs
use core::cell::UnsafeCell;

use crate::bindings::rwlock_t;

pub struct ReadWriteLock<T> {
    data: UnsafeCell<T>,
    inner: RWLockInner,
}

#[allow(dead_code)]
impl<T> ReadWriteLock<T> {
    pub fn new(data: T) -> Self {
        Self {
            data: UnsafeCell::new(data),
            inner: RWLockInner::new(),
        }
    }
}

#[allow(dead_code)]
impl<'a, T: 'a> ReadWriteLock<T> {
    pub fn rlock_f<R>(&self, f: impl FnOnce(&'a T) -> R) -> R {
        unsafe {
            self.inner.read_lock();
            let ret = f(&*self.data.get());
            self.inner.read_unlock();
            ret
        }
    }

    pub fn wlock_f<R>(&self, f: impl FnOnce(&'a mut T) -> R) -> R {
        unsafe {
            self.inner.write_lock();
            let ret = f(&mut *self.data.get());
            self.inner.write_unlock();
            ret
        }
    }
}

pub struct RWLockInner {
    core: rwlock_t,
}

// FIXME: shall we provides the irqsave version?
impl RWLockInner {
    pub fn new() -> Self {
        let mut rw: rwlock_t = Default::default();
        unsafe { crate::bindings::bd_rwlock_init(&mut rw as *mut _) };
        Self { core: rw }
    }

    pub fn read_lock(&self) {
        unsafe { crate::bindings::bd_read_lock(&self.core as *const _ as *mut _) };
    }

    pub fn read_unlock(&self) {
        unsafe { crate::bindings::bd_read_unlock(&self.core as *const _ as *mut _) };
    }

    pub fn write_lock(&self) {
        unsafe { crate::bindings::bd_write_lock(&self.core as *const _ as *mut _) };
    }

    pub fn write_unlock(&self) {
        unsafe { crate::bindings::bd_write_unlock(&self.core as *const _ as *mut _) };
    }
}

unsafe impl<T> Send for ReadWriteLock<T> where T: Send {}
unsafe impl<T> Sync for ReadWriteLock<T> where T: Send {}
