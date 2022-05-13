#![no_std]

extern crate alloc;

use alloc::string::ToString;

use krdma_test::krdma_test;

use rust_kernel_linux_util as log;
use rust_kernel_linux_util::kthread;
use rust_kernel_linux_util::linux_kernel_module;
use rust_kernel_linux_util::linux_kernel_module::c_types::c_void;

static TARGET_CPU_ID: u32 = 0;

extern "C" fn normal_kthread(_param: *mut c_void) -> i32 {
    log::info!("running a normal kthread");
    0
}

extern "C" fn joinable_kthread(param: *mut c_void) -> i32 {
    log::info!("test kthread::should_stop()");
    while !kthread::should_stop() {
        kthread::yield_now();
    }
    log::info!("this kthread should stop, exiting...");
    param as i32
}

extern "C" fn binded_kthread(_param: *mut c_void) -> i32 {
    let cpu_id = kthread::get_cpu_id();
    if cpu_id != TARGET_CPU_ID {
        log::error!("this kthread should run on #{} cpu, but it runs on #{} cpu", TARGET_CPU_ID, cpu_id);
    } else {
        log::info!("this kthread is successfully bound to #{} cpu", TARGET_CPU_ID);
    }
    0
}

fn test_normal_kthread() {
    let builder = kthread::Builder::new()
                    .set_name("normal_kthread".to_string())
                    .set_parameter(0 as *mut c_void);
    let handler = builder.spawn(normal_kthread);
    if handler.is_ok() {
        log::info!("normal kthread spawned successfully");
    } else {
        log::error!("failed to spawn normal kthread");
    }
    // We cannot join the handler here because
    // the spawned kthread may have already exited
}

fn test_kthread_join() {
    let arg: u64 = 73;
    let builder = kthread::Builder::new()
                    .set_name("joinable_kthread".to_string())
                    .set_parameter(arg as *mut c_void);
    let handler = builder.spawn(joinable_kthread);
    if handler.is_err() {
        log::error!("failed to spawn test thread");
        return;
    }
    let handler = handler.unwrap();
    kthread::sleep(2);
    let ret = handler.join();
    if ret != (arg as i32) {
        log::error!("kthread join returns {}, expected {}", ret, arg);
    }
    log::info!("kthread joined successfully");
}

fn test_kthread_bind() {
    let builder = kthread::Builder::new()
                    .set_name("binded_kthread".to_string())
                    .set_parameter(0 as *mut c_void)
                    .bind(TARGET_CPU_ID);
    let handler = builder.spawn(binded_kthread);
    if handler.is_err() {
        log::error!("failed to spawn test thread");
        return;
    }
}

#[krdma_test(test_normal_kthread, test_kthread_join, test_kthread_bind)]
fn init() { 
    log::info!("in init"); 
}
