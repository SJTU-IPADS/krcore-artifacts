#![no_std]
#![feature(get_mut_unchecked, allocator_api)]

struct KRdmaKitSyscallModule {
    _client: Option<client::Client>,
    // used for syscall
    _chrdev_registration: linux_kernel_module::chrdev::Registration,
}

extern crate alloc;

mod virtual_queue;
mod core;
pub mod client;
pub mod bindings;

use virtual_queue::VQ;

use KRdmaKit::rust_kernel_rdma_base::linux_kernel_module;
use KRdmaKit::rust_kernel_rdma_base::rust_kernel_linux_util::timer::KTimer;
use linux_kernel_module::{println, cstr};

impl linux_kernel_module::KernelModule for KRdmaKitSyscallModule {
    fn init() -> linux_kernel_module::KernelResult<Self> {
        let timer = KTimer::new();
        println!("KRdma kernel module init");
        let chrdev_registration = linux_kernel_module::chrdev::builder(cstr!("krdma"), 0..1)?
            .register_device::<VQ>(cstr!("krdma"))
            .build()?;

        let client = client::Client::create();
        println!("KRdma kernel module init done in {} usecs",
                 timer.get_passed_usec());

        Ok(Self {
            _client: client,
            _chrdev_registration: chrdev_registration,
        })
    }
}

#[inline]
pub fn op_code_table(op: u32) -> u32 {
    use KRdmaKit::rust_kernel_rdma_base::*;
    use crate::bindings::*;
    match op {
        lib_r_req::Read => ib_wr_opcode::IB_WR_RDMA_READ,
        lib_r_req::Write => ib_wr_opcode::IB_WR_RDMA_WRITE,
        lib_r_req::Send => ib_wr_opcode::IB_WR_SEND,
        lib_r_req::SendImm => ib_wr_opcode::IB_WR_SEND_WITH_IMM,
        lib_r_req::WriteImm => ib_wr_opcode::IB_WR_RDMA_WRITE_WITH_IMM,
        _ => unimplemented!(),
    }
}

impl Drop for KRdmaKitSyscallModule {
    fn drop(&mut self) {
        println!("Goodbye KRdma syscall module!");
    }
}

linux_kernel_module::kernel_module!(
    KRdmaKitSyscallModule,
    author: b"lfm",
    description: b"KRdmaKit syscall tier",
    license: b"GPL"
);
