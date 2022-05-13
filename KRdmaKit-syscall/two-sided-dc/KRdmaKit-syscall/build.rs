use std::env;
use std::path::PathBuf;

const INCLUDED_TYPES: &[&str] = &[
    "req_t",
    "reply_t",
    "connect_t",
    "req_connect_t",
    "core_req_t",
    "push_req_t",
    "push_core_req_t",
    "push_ext_req_t",
    "user_wc_t",
    "pop_reply_t",
    "bind_t",
    "bind_req_t",
    "pop_t",
    "pop_req_t",
    "pop_msgs_t",
    "push_recv_t",
    "push_recv_req_t",
];


const INCLUDED_ENUMS: &[&str] = &[
    "reply_status",
    "lib_r_cmd",
    "lib_r_req",
    "wc_consts",
];

fn handle_ofed_version() -> String {
    use std::process::Command;
    use std::str;

    let output = Command::new("ofed_info")
        .arg("-n")
        .output()
        .expect("failed to get ofed_info").stdout;

    let s = match str::from_utf8(&output) {
        Ok(v) => v,
        Err(e) => panic!("Invalid UTF-8 sequence: {}", e),
    };

    let result = str::replace(s, "-", "_");
    let result = str::replace(&result, ".", "_");
    let result = result.trim();

    println!("cargo:rustc-cfg=BASE_MLNX_OFED_LINUX_{}", result);
    format!("BASE_MLNX_OFED_LINUX_{}", result)
}

// Takes the CFLAGS from the kernel Makefile and changes all the include paths to be absolute
// instead of relative.
fn prepare_cflags(cflags: &str, kernel_dir: &str) -> Vec<String> {
    let cflag_parts = shlex::split(&cflags).unwrap();
    let mut cflag_iter = cflag_parts.iter();
    let mut kernel_args = vec![];
    while let Some(arg) = cflag_iter.next() {
        if arg.starts_with("-I") && !arg.starts_with("-I/") {
            kernel_args.push(format!("-I{}/{}", kernel_dir, &arg[2..]));
        } else if arg == "-include" {
            kernel_args.push(arg.to_string());
            let include_path = cflag_iter.next().unwrap();
            if include_path.starts_with('/') {
                kernel_args.push(include_path.to_string());
            } else {
                kernel_args.push(format!("{}/{}", kernel_dir, include_path));
            }
        } else {
            kernel_args.push(arg.to_string());
        }
    }
    //    println!("!!! {:?}", kernel_args);
    kernel_args
}

fn main() {
    println!("cargo:rust-cfg=out");

    println!("cargo:rerun-if-env-changed=CC");
    println!("cargo:rerun-if-env-changed=KDIR");
    println!("cargo:rerun-if-env-changed=c_flags");
    println!("cargo:rerun-if-env-changed=ofa_flags");

    let _ofed_ver = handle_ofed_version();

    let kernel_dir = env::var("KDIR").expect("Must be invoked from kernel makefile");
    let kernel_cflags = env::var("c_flags").expect("Add 'export c_flags' to Kbuild");
    let kbuild_cflags_module =
        env::var("KBUILD_CFLAGS_MODULE").expect("Must be invoked from kernel makefile");

    let ofa_flags = env::var("ofa_flags").expect("Add extra ofa flags to Kbuild");

    let cflags = format!("{} {} {}", ofa_flags, kernel_cflags, kbuild_cflags_module);
    let kernel_args = prepare_cflags(&cflags, &kernel_dir);

    let target = env::var("TARGET").unwrap();

    let mut builder = bindgen::Builder::default()
        .use_core()
        .ctypes_prefix("c_types")
        .derive_default(true)
        .size_t_is_usize(true)
        .rustfmt_bindings(true);

    builder = builder.clang_arg(format!("--target={}", target));
    for arg in kernel_args.iter() {
        builder = builder.clang_arg(arg.clone());
    }

    println!("cargo:rerun-if-changed=../include/common.h");
    builder = builder.header("../include/common.h");

    // non-rust translatable type
    builder = builder.opaque_type("xregs_state");
    builder = builder.opaque_type("desc_struct");

    for t in INCLUDED_TYPES {
        builder = builder.whitelist_type(t);
    }

    for t in INCLUDED_ENUMS {
        builder = builder.whitelist_type(t);
        builder = builder.constified_enum_module(t);
    };

    let bindings = builder.generate().expect("Unable to generate bindings");

    let out_path = PathBuf::from(env::var("OUT_DIR").unwrap());
    bindings
        .write_to_file(out_path.join("bindings-".to_owned() + env!("CARGO_PKG_NAME") + ".rs"))
        .expect("Couldn't write bindings!");

}
