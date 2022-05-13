use kernel_module_testlib::{with_kernel_module,assert_dmesg_contains,dmesg_contains};

#[test]
fn test_create_dct() {
    // a dummy test func
    with_kernel_module(|| {
        // ok strings
        let test_strs2 = [String::from("client ok"), String::from("create cq ok")];
        assert_dmesg_contains(&test_strs2);

        // bad msgs
        assert_eq!(dmesg_contains(&String::from("null cq")),false);
        assert_eq!(dmesg_contains(&String::from("null qp")),false);

        // other err messages
        assert_eq!(dmesg_contains(&String::from("err")),false);

    });
}
