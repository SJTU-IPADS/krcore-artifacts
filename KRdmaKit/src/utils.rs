use crate::alloc::string::ToString;
use rdma_shim::ffi::c_types::c_char;

use alloc::string::String;

pub fn convert_c_str_to_string<'s>(string: &'s [c_char]) -> String {
    let mut count = 0;

    for i in 0..string.len() {
        if string[i] == 0 {
            break;
        } else {
            count += 1;
        }
    }

    if count == 0 {
        String::from("\0")
    } else {
        unsafe {
            core::str::from_utf8_unchecked(core::slice::from_raw_parts(string.as_ptr() as _, count))
                .to_string()
        }
    }
}



