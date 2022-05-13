use alloc::sync::Arc;
use alloc::vec;
use hashbrown::HashMap;
use KRdmaKit::cm::{EndPoint};
use KRdmaKit::ctrl::RCtrl;
use KRdmaKit::device::{RContext};
use KRdmaKit::mem::{Memory, RMemPhy};
use KRdmaKit::qp::*;
use KRdmaKit::{Profile, rust_kernel_rdma_base, to_ptr};
// linux_kernel_module
use linux_kernel_module::{println};
// rust_kernel_rdma_base
use rust_kernel_rdma_base::*;
use rust_kernel_rdma_base::linux_kernel_module;
use crate::ALLRCONTEXTS;
use rust_kernel_rdma_base::bindings::*;
use crate::consts::{K, M};

pub fn test_dct_bench() {
    let ctx_idx = 0;
    let remote_service_id = 8;
    let mut ctrl =
        RCtrl::create(remote_service_id as u64, &mut ALLRCONTEXTS.get_mut()[ctx_idx]).unwrap();
    let ctx: &mut RContext = &mut ALLRCONTEXTS.get_mut()[ctx_idx];
    let pd = ctx.get_pd();
    let device = ctx.get_raw_dev();
    let config: Config = Default::default();
    // create CQ
    let mut cq_attr: ib_cq_init_attr = Default::default();
    cq_attr.cqe = config.max_cqe as u32;

    let cq = unsafe {
        ib_create_cq(
            device,
            None,
            None,
            core::ptr::null_mut(),
            &mut cq_attr as *mut _,
        )
    };

    // Recv CQ
    let recv_cq = unsafe {
        ib_create_cq(
            device,
            None,
            None,
            core::ptr::null_mut(),
            &mut cq_attr as *mut _,
        )
    };

    // srq
    let mut cq_attr: ib_srq_init_attr = Default::default();
    cq_attr.attr.max_wr = config.max_send_wr_sz as u32;
    cq_attr.attr.max_sge = config.max_send_sge as u32;
    let srq = unsafe { ib_create_srq(pd, &mut cq_attr as _) };

    let mut dctattr: ib_dct_init_attr = Default::default();
    dctattr.pd = pd;
    dctattr.cq = cq;
    dctattr.srq = srq;
    dctattr.dc_key = 73;
    dctattr.port = 1;
    dctattr.access_flags =
        ib_access_flags::IB_ACCESS_LOCAL_WRITE |
            ib_access_flags::IB_ACCESS_REMOTE_READ |
            ib_access_flags::IB_ACCESS_REMOTE_WRITE |
            ib_access_flags::IB_ACCESS_REMOTE_ATOMIC;
    dctattr.min_rnr_timer = 2;
    dctattr.tclass = 0;
    dctattr.flow_label = 0;
    dctattr.mtu = ib_mtu::IB_MTU_4096;
    dctattr.pkey_index = 0;
    dctattr.hop_limit = 1;
    dctattr.inline_size = 0;
    dctattr.create_flags = 0;

    let sz_list = vec![3740, 8, 604, 4360, 20, 2044, 8, 8, 2044, 4, 4, 28, 2044, 4, 4, 204, 2044, 4, 4, 192, 2048, 68, 584, 2048, 16, 44, 80, 2044, 4, 4, 464, 2044, 8, 8, 236, 2044, 4, 8, 536, 2048, 28, 72, 2052, 68, 2044, 4, 4, 768, 16, 2044, 4, 4, 132, 2044, 4, 4, 28, 2044, 4, 8, 60, 2044, 4, 4, 16, 2044, 4, 4, 512, 136, 2044, 4, 16, 516, 24, 2044, 4, 4, 768, 2156, 2044, 112, 48, 12, 376, 2048, 16, 28, 92, 2048, 4, 20, 256, 1280, 148, 2044, 16, 4, 244, 2048, 8, 24, 4, 24, 2044, 4, 8, 4944, 1056, 2044, 4, 4, 100, 2044, 4, 4, 152, 2048, 8, 4, 8, 2044, 4, 4, 12, 2044, 4, 4, 1792, 2048, 16, 8, 16, 96, 2044, 4, 4, 16, 152, 1812, 4, 28, 4, 4, 4, 132, 4];
    let mut res_hash: HashMap<usize, *mut ib_dct> = HashMap::new();
    for sz in sz_list.clone() {
        let dct = unsafe { safe_ib_create_dct(pd, &mut dctattr as _) };
        res_hash.insert(sz, dct);
    }
    let run_cnt = 1000;
    for sz in sz_list.clone() {
        println!("=========");
        println!("total len:\t{}", sz);
        let mut profile = Profile::new();
        for _ in 0..run_cnt {
            profile.reset_timer();
            let t = res_hash.get(&sz);

            profile.tick_record(0);
        }
        profile.increase_op(run_cnt);
        profile.report(1);

        let dct = res_hash.get(&sz).unwrap();
        unsafe { ib_exp_destroy_dct(*dct, core::ptr::null_mut()) };
    }
}