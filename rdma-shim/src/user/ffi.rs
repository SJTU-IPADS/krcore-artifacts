#[allow(non_camel_case_types)]

pub mod c_types {
    pub type c_int = libc::c_int;
    pub type c_char = libc::c_char;
    pub type c_long = libc::c_long;
    pub type c_longlong = libc::c_longlong;
    pub type c_short = libc::c_short;
    pub type c_uchar = libc::c_uchar;
    pub type c_uint = libc::c_uint;
    pub type c_ulong = libc::c_ulong;
    pub type c_ulonglong = libc::c_ulonglong;
    pub type c_ushort = libc::c_ushort;
    pub type c_schar = libc::c_schar;
    pub type c_size_t = libc::size_t;
    pub type c_ssize_t = libc::ssize_t;
    pub type c_void = core::ffi::c_void;
}
