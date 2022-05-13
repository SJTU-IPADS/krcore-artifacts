use kernel_module_testlib::{with_kernel_module,assert_dmesg_contains,dmesg_contains};

#[test]
fn test_getdev() {
    // a dummy test func
    with_kernel_module(|| {
        // ok strings
        let test_strs2 = [String::from("client ok"), String::from("Dev ok")];
        assert_dmesg_contains(&test_strs2);

        // bad msgs
        assert_eq!(dmesg_contains(&String::from("client err")),false);
        assert_eq!(dmesg_contains(&String::from("Dev empty")),false);
        assert_eq!(dmesg_contains(&String::from("err")),false);
    });
}
