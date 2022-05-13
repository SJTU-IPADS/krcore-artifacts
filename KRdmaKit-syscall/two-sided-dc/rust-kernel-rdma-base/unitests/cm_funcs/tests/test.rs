use kernel_module_testlib::{with_kernel_module,dmesg_contains};

#[test]
fn test_cm_funcs_exists() {
    // a dummy test func
    with_kernel_module(|| {
        // other err messages
        assert_eq!(dmesg_contains(&String::from("null")),false);
    });
}
