use std::env;
use std::path::PathBuf;

// types from customized syscalls
const INCLUDED_TYPES: &[&str] = &[
    "LibSwapCmd",
    "rwlock_t",
];

// enums from kernel
const INCLUDED_KERNEL_ENUMS: &[&str] = &[];

// types from kernel
const INCLUDED_KERNEL_TYPES: &[&str] = &[
    "timeval",
    "scatterlist",
    "page",
];

const INCLUDED_KERNEL_FUNCS: &[&str] = &[
    "bd_virt_to_phys",
    "bd_phys_to_virt",
    "bd_cpu_to_be64",
    "kthread_run",
    "kthread_stop",
    "kthread_create",
    "kthread_should_stop",
    "bd_kthread_run",
    "schedule_timeout_interruptible",
    "get_hz",
    "wait_for_completion_interruptible_timeout",
    "__msecs_to_jiffies",
    "bd_init_completion",
    "bd__builtin_bswap64",
    "bd_ssleep",
    "complete",
    "do_gettimeofday",
    "bd_rwlock_init",
    "bd_read_lock",
    "bd_write_lock",
    "bd_read_unlock",
    "bd_write_unlock",
    "bd_get_cpu",
    "vmalloc",
    "vfree",
    "memcpy",
    // "sys_open",
    // "sys_close",
];

const INCLUDED_VARS: &[&str] = &[];


fn handle_ofed_version() {
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

    handle_ofed_version();

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

    println!("cargo:rerun-if-changed=src/native/kernel_helper.h");

    builder = builder.header("src/native/kernel_helper.h");

    // non-rust translatable type
    builder = builder.opaque_type("xregs_state");
    builder = builder.opaque_type("desc_struct");

    for t in INCLUDED_TYPES {
        builder = builder.whitelist_type(t);
    }

    for t in INCLUDED_KERNEL_ENUMS {
        builder = builder.whitelist_type(t);
        builder = builder.constified_enum_module(t);
    };

    for t in INCLUDED_KERNEL_TYPES {
        builder = builder.whitelist_type(t);
    }

    for f in INCLUDED_KERNEL_FUNCS {
        builder = builder.whitelist_function(f);
    }

    for v in INCLUDED_VARS {
        builder = builder.whitelist_var(v);
    }

    let bindings = builder.generate().expect("Unable to generate bindings");

    let out_path = PathBuf::from(env::var("OUT_DIR").unwrap());
    bindings
        .write_to_file(out_path.join("bindings-".to_owned() + env!("CARGO_PKG_NAME") + ".rs"))
        .expect("Couldn't write bindings!");

    // build kernel_helper.c
    let mut builder = cc::Build::new();
    builder.compiler(env::var("CC").unwrap_or_else(|_| "clang".to_string()));
    builder.target(&target);
    builder.warnings(false);
    println!("cargo:rerun-if-changed=src/native/kernel_helper.c");

    builder.file("src/native/kernel_helper.c");

    for arg in kernel_args.iter() {
        builder.flag(&arg);
    }
    builder.compile("helpers");
}
