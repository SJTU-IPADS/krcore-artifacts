pub mod bindings;
pub mod ffi;
pub mod utils;

pub use log;

use core::num::TryFromIntError;

use ffi::c_types::c_int;
use ffi::c_types;

#[derive(Debug)]
pub struct Error(pub c_int);

impl Error {
    pub const EINVAL: Self = Error(-(libc::EINVAL as c_int));
    pub const ENOMEM: Self = Error(-(libc::ENOMEM as c_int));
    pub const EFAULT: Self = Error(-(libc::EFAULT as c_int));
    pub const ESPIPE: Self = Error(-(libc::ESPIPE as c_int));
    pub const EAGAIN: Self = Error(-(libc::EAGAIN as c_int));

    pub fn from_kernel_errno(errno: c_types::c_int) -> Error {
        Error(errno)
    }

    pub fn to_kernel_errno(&self) -> c_types::c_int {
        self.0
    }
}

impl core::fmt::Display for Error {
    fn fmt(&self, fmt: &mut ::core::fmt::Formatter) -> core::fmt::Result {
        match self.0 as c_int {
            libc::EINVAL => write!(fmt, "EINVAL"),
            _ => write!(fmt, "Unknown error {}",self.0),
        }
    }
}

impl From<TryFromIntError> for Error {
    fn from(_: TryFromIntError) -> Error {
        Error::EINVAL
    }
}

pub type KernelResult<T> = Result<T, Error>;

#[cfg(test)]
mod tests {
    #[test]
    fn has_rdma() {
        let mut n = 0i32;
        let devices = unsafe { super::bindings::ibv_get_device_list(&mut n as *mut _) };
        
        // should have devices
        assert!(!devices.is_null());    
    }
}

