use kernel_module_testlib::{with_kernel_module, assert_dmesg_contains};

#[test]
fn test_dmesg() {
    // a dummy test func
    with_kernel_module(|| {
    });
}
