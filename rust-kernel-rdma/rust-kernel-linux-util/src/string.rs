extern crate alloc;

use alloc::string::String;

use core::{str, slice};

pub unsafe fn strlen(ptr: *const u8) -> usize {
    let mut res: usize = 0;
    loop {
        if *ptr.add(res as usize) == 0 {
            return res;
        }
        res += 1;
    }
}

pub unsafe fn ptr2string(ptr: *const u8) -> String {
    let s = str::from_utf8_unchecked(slice::from_raw_parts(ptr, strlen(ptr)));
    String::from(s)
}
