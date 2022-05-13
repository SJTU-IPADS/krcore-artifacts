use std::env;
use std::path::PathBuf;

// types from customized syscalls
const INCLUDED_TYPES: &[&str] = &["LibSwapCmd"];

// enums from kernel
const INCLUDED_KERNEL_ENUMS: &[&str] = &[
    "ib_qp_type",
    "ib_access_flags",
    "ib_sig_type",
    "ib_mr_type",
    "ib_port_state",
    "ib_qp_attr_mask",
    "ib_qp_state",
    "ib_cm_event_type",
    "ib_wr_opcode",
    "ib_send_flags",
    "ib_cm_sidr_status",
    "ib_mtu",
    "ib_event_type",
    "ib_wc_status",    
    "rdma_ah_attr_type",
];

// types from kernel
const INCLUDED_KERNEL_TYPES: &[&str] = &[
    "dma_addr_t",
    "ib_device",
    "ib_device_attr",
    "port_attr",
    "ib_client",
    "ib_sa_client",
    "ib_gid",
    "ib_pd",
    // CQ related
    "ib_cq_init_attr",
    "ib_cq",
    // QP related
    "ib_qp",
    "ib_qp_state",
    "ib_sge",
    "ib_reg_wr",
    "ib_rdma_wr",
    "ib_send_wr",
    "ib_recv_wr",
    "ib_ud_wr",
    "ib_wc",
    "ib_ah",
    "rdma_ah_attr",
    // CM related
    "ib_cm_state",
    "ib_cm_id",
    "sa_path_rec",
    "ib_sa_query",
    "ib_cm_event",
    "ib_cm_handler",
    "ib_cm_req_param",
    "ib_cm_rep_param",
    "ib_cm_sidr_rep_param",
    "ib_cm_sidr_req_param",

    //#[cfg(feature = "dct")]
    "ib_dc_wr",
    "ib_dct_attr",
    "ib_cq_attr",

    // DCT related
    "ib_dct",
];

const INCLUDED_KERNEL_FUNCS: &[&str] = &[
    // context related
    "ib_register_client",
    "ib_unregister_client",
    "__ib_alloc_pd",
    "ib_dealloc_pd",
    "ib_dealloc_pd_user",

    // device related
    "ib_query_port",
    // mr related
    "ib_dereg_mr",
    "ib_dereg_mr_user",
    "ib_alloc_mr",
    "ib_alloc_mr_user",
    "ib_get_dma_mr",
    "bd_ib_dma_map_sg",
    "ib_map_mr_sg",
    "sg_init_table",
    "bd_virt_to_page",
    "bd_alloc_pages",
    "bd_free_pages",
    "bd_put_page",
    "get_free_page",
    "get_user_pages",
    "bd_sg_set_page",
    "bd_page_address",
    "vmalloc_to_page",
    "bd_get_page",
    "gfp_highuser",
    "page_size",
    "dma_from_device",
    // cq related
    "ib_create_cq",
    "bd_ib_create_cq",
    "__ib_alloc_cq",
    "ib_free_cq",
    "ib_free_cq_user",
    "bd_ib_poll_cq",
    "bd_ib_post_send",
    "bd_set_send_wr_imm",
    "bd_ib_post_recv",
    "bd_ib_post_srq_recv",
    // qp related
    "ib_create_qp",
    "ib_create_qp_user",
    "ib_query_qp",
    "ib_modify_qp",
    "ib_destroy_qp",
    "ib_destroy_qp_user",
    "rdma_destroy_ah",
    "rdma_destroy_ah_user",

    "bd_rdma_ah_set_dlid",
    "bd_set_recv_wr_id",
    "bd_get_recv_wr_id",
    "bd_get_wc_wr_id",
    "rdma_create_ah",
    // cm related
    "ib_create_cm_id",
    "ib_destroy_cm_id",
    "ib_cm_listen",
    "ib_cm_init_qp_attr",
    "ib_sa_path_rec_get",
    "path_rec_service_id",
    "path_rec_dgid",
    "path_rec_sgid",
    "path_rec_numb_path",
    "ib_send_cm_rep",
    "ib_send_cm_req",
    "ib_send_cm_dreq",
    "ib_send_cm_drep",
    "ib_send_cm_rtu",
    "ib_send_cm_sidr_rep",
    "ib_send_cm_sidr_req",
    "ib_sa_register_client",
    "ib_sa_unregister_client",
    // DCT related
    "ib_exp_create_dct",
    "ib_exp_destroy_dct",
    // "ib_exp_modify_dct",
    // "ib_exp_modify_dct_qp_init",
    // "ib_exp_modify_dct_qp_rtr",
    // "ib_exp_modify_cq",
    // "ib_exp_get_dct_qp",
    "base_ib_create_qp_dct",
    "ptr_is_err",
    "base_ib_create_qp",
    "ib_create_srq",
    "ib_create_srq_user",
    "ib_destroy_srq",
    "ib_destroy_srq_user",
];

const INCLUDED_VARS: &[&str] = &[
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

    let ofed_ver = handle_ofed_version();

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
    builder.define(&ofed_ver, None);
    println!("cargo:rerun-if-changed=src/native/kernel_helper.c");

    builder.file("src/native/kernel_helper.c");

    for arg in kernel_args.iter() {
        builder.flag(&arg);
    }
    builder.compile("helpers");
}
