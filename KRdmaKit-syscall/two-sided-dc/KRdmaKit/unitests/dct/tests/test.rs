use kernel_module_testlib::{with_kernel_module};
use kernel_module_testlib::dmesg_contains;

#[test]
fn test_dmesg() {
    // a dummy test func
    with_kernel_module(|| {
        // other err messages
        assert_eq!(dmesg_contains(&String::from("err")),false);
    });
}
