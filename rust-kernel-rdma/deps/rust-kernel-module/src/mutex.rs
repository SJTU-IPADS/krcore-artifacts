use crate::bindings::{mutex_init_helper, mutex, mutex_lock, mutex_trylock, mutex_unlock};

// slightly wrap the Mutex in linux kernel to make it more convenient
type LinuxMutexInner = mutex;

impl LinuxMutexInner {
    // this is really dangerous here
    // currently, after the lock has been created using LinuxMutexInner::new(),
    // it must be called w/ a init() to work
    pub fn init(&self) {
        unsafe {
            mutex_init_helper((self as *const Self) as *mut Self);
        }
    }

    pub fn lock(&self) {
        unsafe {
            mutex_lock((self as *const Self) as *mut Self);
        }
    }

    // try to lock
    // return 1 if success; else return 0
    pub fn try_lock(&mut self) -> usize {
        let ret = unsafe { mutex_trylock(self as *mut Self) as usize };
        ret
    }

    pub fn unlock(&self) {
        unsafe {
            mutex_unlock((self as *const Self) as *mut Self);
        }
    }
}

// Rust-style Mutex based on LinuxMutexInner

use core::cell::UnsafeCell;

pub struct LinuxMutex<T> {
    data: UnsafeCell<T>,
    mutex: LinuxMutexInner,
}

//--------------------------------------------------------------------------------------------------
// Public Code
//--------------------------------------------------------------------------------------------------

unsafe impl<T> Send for LinuxMutex<T> where T: Send {}

unsafe impl<T> Sync for LinuxMutex<T> where T: Send {}

impl<T> LinuxMutex<T> {
    /// Create an instance.
    /// note: the lock cannot be used now, need to call init
    pub fn new(data: T) -> Self {
        Self {
            data: UnsafeCell::new(data),
            mutex: Default::default(),
        }
    }

    pub fn init(&self) {
        self.mutex.init();
    }
}

//------------------------------------------------------------------------------
// OS Interface Code
//------------------------------------------------------------------------------

use crate::sync::Mutex;

impl<'a, T: 'a> Mutex<'a, T> for LinuxMutex<T> {
    fn lock_f<R>(&self, f: impl FnOnce(&'a mut T) -> R) -> R {
        unsafe {
            self.mutex.lock();
            let ret = f(&mut *self.data.get());
            self.mutex.unlock();
            ret
        }
    }
}
