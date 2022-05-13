#![no_std]

struct SampleTestModule {
}

use rust_kernel_linux_util::{linux_kernel_module};

impl linux_kernel_module::KernelModule for SampleTestModule {

    fn init() -> linux_kernel_module::KernelResult<Self> {

        use rust_kernel_linux_util as log; 
        log::set_max_level(log::LevelFilter::Trace); 
        log::info!("info"); 
        log::error!("error"); 
        log::warn!("warn"); 
        log::debug!("debug"); 

        Ok(Self {})
    }
}

linux_kernel_module::kernel_module!(
    SampleTestModule,
    author: b"xmm",
    description: b"Test log level in the kernel",
    license: b"GPL"
);
