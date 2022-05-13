#![no_std]

extern crate alloc;

use linux_kernel_module::filesystem::{self, FileSystem, FileSystemFlags};
use linux_kernel_module::{self, cstr, CStr};

struct TestFSModule {
    _fs_registration: filesystem::Registration<TestFS>,
}

struct TestFS {}

impl FileSystem for TestFS {
    const NAME: CStr<'static> = cstr!("testfs");
    const FLAGS: FileSystemFlags = FileSystemFlags::empty();
}

impl linux_kernel_module::KernelModule for TestFSModule {
    fn init() -> linux_kernel_module::KernelResult<Self> {
        let fs_registration = filesystem::register::<TestFS>()?;
        Ok(TestFSModule {
            _fs_registration: fs_registration,
        })
    }
}

linux_kernel_module::kernel_module!(
    TestFSModule,
    author: b"Fish in a Barrel Contributors",
    description: b"A module for testing filesystem::register",
    license: b"GPL"
);
