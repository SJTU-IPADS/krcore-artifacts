#[allow(
    clippy::all,
    non_camel_case_types,
    non_upper_case_globals,
    non_snake_case,
    improper_ctypes,
    non_upper_case_globals,
    dead_code
)]
mod bindings {
    use linux_kernel_module::c_types;
    include!(concat!(env!("OUT_DIR"), "/bindings-",env!("CARGO_PKG_NAME"),".rs"));
}

pub use bindings::*;

impl PartialEq for ib_gid {
    fn eq(&self, other: &ib_gid) -> bool {
        unsafe { self.raw == other.raw }
    }
}

impl Eq for ib_gid {}
