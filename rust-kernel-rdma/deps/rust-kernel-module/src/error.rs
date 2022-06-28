use core::num::TryFromIntError;

use crate::bindings;
use crate::c_types;

#[derive(Debug)]
pub struct Error(c_types::c_int);

impl Error {
    pub const EINVAL: Self = Error(-(bindings::EINVAL as i32));
    pub const ENOMEM: Self = Error(-(bindings::ENOMEM as i32));
    pub const EFAULT: Self = Error(-(bindings::EFAULT as i32));
    pub const ESPIPE: Self = Error(-(bindings::ESPIPE as i32));
    pub const EAGAIN: Self = Error(-(bindings::EAGAIN as i32));

    pub fn from_kernel_errno(errno: c_types::c_int) -> Error {
        Error(errno)
    }

    pub fn to_kernel_errno(&self) -> c_types::c_int {
        self.0
    }
}


impl core::fmt::Display for Error {
    fn fmt(&self, fmt: &mut ::core::fmt::Formatter) -> core::fmt::Result {
        match self.0 as u32 {
            bindings::EINVAL => write!(fmt, "EINVAL"),
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
