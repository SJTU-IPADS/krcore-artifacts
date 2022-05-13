use kernel_module_testlib::{with_kernel_module};

#[test]
fn test_simple() {
    // a dummy test func
    with_kernel_module(|| {
        println!("sampe test");
    });
}
