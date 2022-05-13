use core::convert::TryInto;
use core::mem;
use core::ops::Range;

use alloc::boxed::Box;
use alloc::vec;
use alloc::vec::Vec;

use crate::bindings;
use crate::c_types;
use crate::error::{Error, KernelResult};
use crate::file_operations;
use crate::types::CStr;

use crate::bindings::class;

#[warn(dead_code)]
static mut CL: *mut class = core::ptr::null_mut();

pub fn builder(name: CStr<'static>, minors: Range<u16>) -> KernelResult<Builder> {
    Ok(Builder {
        name,
        minors,
        file_ops: vec![],
        file_names: vec![],
    })
}

pub struct Builder {
    name: CStr<'static>,
    minors: Range<u16>,
    file_ops: Vec<&'static bindings::file_operations>,
    file_names: Vec<CStr<'static>>,
}

impl Builder {
    pub fn register_device<T: file_operations::FileOperations>(
        mut self,
        name: CStr<'static>,
    ) -> Builder {
        if self.file_ops.len() >= self.minors.len() {
            panic!("More devices registered than minor numbers allocated.")
        }
        self.file_ops
            .push(&file_operations::FileOperationsVtable::<T>::VTABLE);
        self.file_names.push(name);
        self
    }

    pub fn build(self) -> KernelResult<Registration> {
        let mut dev: bindings::dev_t = 0;
        let res = unsafe {
            bindings::alloc_chrdev_region(
                &mut dev,
                self.minors.start.into(),
                self.minors.len().try_into()?,
                self.name.as_ptr() as *const c_types::c_char,
            )
        };
        if res != 0 {
            return Err(Error::from_kernel_errno(res));
        }

        // Turn this into a boxed slice immediately because the kernel stores pointers into it, and
        // so that data should never be moved.

        // create the dev
        let mut _key = bindings::lock_class_key {};

        unsafe {
            let cl = bindings::__class_create(
                &mut bindings::__this_module,
                b"char\0".as_ptr() as *const i8,
                &mut _key as *mut bindings::lock_class_key,
            );
            if cl == core::ptr::null_mut() {
                // so that we can free the resources
                // not handled
            } else {
                (*cl).devnode = Some(dev_set_user);
                CL = cl;
            }
        }

        // note: currently we assumes the file is a device
        // so it is hard-coded
        let mut cdevs = vec![unsafe { mem::zeroed() }; self.file_ops.len()].into_boxed_slice();
        for (i, file_op) in self.file_ops.iter().enumerate() {
            unsafe {
                bindings::cdev_init(&mut cdevs[i], *file_op);
                cdevs[i].owner = &mut bindings::__this_module;
                let mut rc = bindings::cdev_add(&mut cdevs[i], dev + i as bindings::dev_t, 1);

                if rc == 0 {
                    // continue to create the device if no error happens previously
                    let dev_ret = bindings::device_create(
                        CL,
                        core::ptr::null_mut(),
                        dev + i as bindings::dev_t,
                        core::ptr::null_mut(),
                        self.file_names[i].as_ptr() as *const i8,
                        //                        b"rlibx\0".as_ptr() as *const i8,
                    );
                    if dev_ret == core::ptr::null_mut() {
                        rc = -1;
                        bindings::class_destroy(CL);
                    }
                }

                if rc != 0 {
                    // Clean up the ones that were allocated.
                    for j in 0..=i {
                        bindings::cdev_del(&mut cdevs[j]);
                    }
                    bindings::unregister_chrdev_region(dev, self.minors.len() as _);
                    return Err(Error::from_kernel_errno(rc));
                }
            }
        }

        Ok(Registration {
            dev,
            count: self.minors.len(),
            cdevs,
        })
    }
}

pub struct Registration {
    dev: bindings::dev_t,
    count: usize,
    cdevs: Box<[bindings::cdev]>,
}

// This is safe because Registration doesn't actually expose any methods.
unsafe impl Sync for Registration {}

impl Drop for Registration {
    fn drop(&mut self) {
        unsafe {
            for (i, dev) in self.cdevs.iter_mut().enumerate() {
                bindings::device_destroy(CL, self.dev + i as bindings::dev_t);
                bindings::cdev_del(dev);
            }
            if CL != core::ptr::null_mut() {
                bindings::class_destroy(CL);
            }
            bindings::unregister_chrdev_region(self.dev, self.count as _);
        }
    }
}

use crate::bindings::*;
#[allow(unused_variables)]
unsafe extern "C" fn dev_set_user(dev: *mut device, mode: *mut umode_t) -> *mut c_types::c_char {
    if !mode.is_null() {
        //        crate::println!("write user mode success");
        // allow user-mode access
        mode.write(S_IALLUGO as u16);
    }
    core::ptr::null_mut()
}
