use kernel_module_testlib::{with_kernel_module, dmesg_contains};

#[test]
fn test_kthread() {
    with_kernel_module(|| {
        assert_eq!(dmesg_contains(&String::from("error")),false);
    });
}
