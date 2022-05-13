use kernel_module_testlib::{with_kernel_module};

#[test]
fn test_work() {
    // a dummy test func
    with_kernel_module(|| {
        println!("work");
    });
}
