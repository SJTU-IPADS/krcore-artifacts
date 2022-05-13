//use core::alloc::{Layout, AllocRef, AllocError};
use core::alloc::{AllocError, Allocator};
use core::ptr::NonNull;

use core::alloc::Layout;

use crate::linux_kernel_module::c_types;

use crate::rust_kernel_linux_util::bindings;

#[derive(Copy, Clone, Default, Debug)]
pub struct VmallocAllocator;

unsafe impl Allocator for VmallocAllocator {
    fn allocate(&self, layout: Layout) -> Result<NonNull<[u8]>, AllocError> {
        match layout.size() {
            0 => Ok(NonNull::slice_from_raw_parts(layout.dangling(), 0)),
            // SAFETY: `layout` is non-zero in size,
            size => {
                let raw_ptr = unsafe { bindings::vmalloc(size as u64) } as *mut u8;
                let ptr = NonNull::new(raw_ptr).ok_or(AllocError)?;
                Ok(NonNull::slice_from_raw_parts(ptr, size))
            }
        }
    }

    unsafe fn deallocate(&self, ptr: NonNull<u8>, _layout: Layout) {
        bindings::vfree(ptr.as_ptr() as *const c_types::c_void);
    }
}