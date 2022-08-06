use kernel_module_testlib::{with_kernel_module, dmesg_contains};

#[test]
fn test_dmesg() {
    with_kernel_module(|| {
        // log::error! will print ERROR in dmesg
        assert_eq!(dmesg_contains(&String::from("ERROR")),false);
    });
}
