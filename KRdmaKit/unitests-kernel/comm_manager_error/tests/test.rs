use kernel_module_testlib::{with_kernel_module, dmesg_contains};

#[test]
fn test_cm() {
    with_kernel_module(|| {
        // log::error! will print ERROR in dmesg
        // however, this test will always print error log in the kernel
        // we only to ensure there is not "general protection fault" in the kernel
        assert_eq!(dmesg_contains(&String::from("general protection fault")),false);
    });
}
