use alloc::boxed::Box;
use alloc::sync::Arc;
use alloc::vec;
use core::ptr::null_mut;
use KRdmaKit::cm::SidrCM;
use KRdmaKit::ctrl::RCtrl;
use KRdmaKit::device::{RContext};
use KRdmaKit::ib_path_explorer::IBExplorer;
use KRdmaKit::mem::{RMemVirt};
use KRdmaKit::qp::*;
use KRdmaKit::{random, rust_kernel_rdma_base};
use KRdmaKit::consts::DEFAULT_RPC_HINT;
// linux_kernel_module
use linux_kernel_module::{println};
use KRdmaKit::Profile;

// rust_kernel_rdma_base
use rust_kernel_rdma_base::*;
use rust_kernel_rdma_base::linux_kernel_module;
use crate::linux_kernel_module::bindings::GFP_KERNEL;
use crate::ALLRCONTEXTS;
use crate::consts::*;

fn wait_one_cq(cq: *mut ib_cq) {
    let mut wc: ib_wc = Default::default();
    let mut run_cnt = 0;
    loop {
        let ret = unsafe { bd_ib_poll_cq(cq, 1, &mut wc as *mut ib_wc) };
        if ret < 0 {
            linux_kernel_module::println!("ret < 0");
            break;
        } else if ret == 1 {
            if wc.status != 0 && wc.status != 5 {
                linux_kernel_module::println!("rc poll cq error, wc_status:{}", wc.status);
            }
            break;
        } else {
            run_cnt += 1;
            if run_cnt > 10000 {
                linux_kernel_module::println!("timeout");
                break;
            }
        }
    }
}

/// Basic payload-latency benchmark upon FRWR
pub fn bench_frwr() {
    let ctx: &mut RContext = &mut ALLRCONTEXTS.get_mut()[0];

    let mut explorer = IBExplorer::new();
    let remote_service_id = 50;
    let qd_hint = 73;
    let gid_addr = ctx.get_gid_as_string();
    let _ = explorer.resolve_v2(1, ctx, &gid_addr);
    // get path
    // thus the path resolver could be split out of other module
    let path_res = explorer.get_path_result().unwrap();
    // create Ctrl. serve for cm handshakes
    let _ctrl = RCtrl::create(remote_service_id, &mut ALLRCONTEXTS.get_mut()[0]).unwrap();

    // create RC at one side
    let rc = RC::new(&mut ALLRCONTEXTS.get_mut()[0], core::ptr::null_mut(), core::ptr::null_mut());

    let mut rc = rc.unwrap();
    let mrc = unsafe { Arc::get_mut_unchecked(&mut rc) };
    // connect to the server (Ctrl)
    let _ = mrc.connect(qd_hint + 1, path_res, remote_service_id as u64);


    let sz_list = vec![4 * K, 16 * K, 256 * K, 512 * K,
                       1 * M, 4 * M, 16 * M, 64 * M, 128 * M, 512 * M];

    fn normal_req(rc: &Arc<RC>, ctx: &RContext, l_pblock: &RMemVirt, val: usize) {
        let sz: u64 = 64;
        let lmr = l_pblock.get_inner_mr();
        let local_pa = l_pblock.get_dma_buf();
        let local_va = l_pblock.get_va_buf();
        let remote_pa = local_pa + sz;
        let remote_va = local_va + sz;
        let rkey = unsafe { (*lmr).rkey } as u32;
        let k_key = unsafe { ctx.get_lkey() };

        let mut op = RCOp::new(rc);
        println!("[before] value = {}", unsafe { *(local_va as *mut i8) });
        let ptr = remote_va as *mut usize;
        unsafe { (*ptr) = val as usize };
        let _ = op.read(local_pa, k_key,
                        64 as usize, remote_pa, rkey);

        println!("[after] value = {}", unsafe { *(local_va as *mut usize) });
    }

    fn run_single_test(rc: &Arc<RC>, sz: usize) {
        let qp = rc.get_qp();
        let mut profile = Profile::new();
        let en_len = 1 + (sz / PG_SZ);
        let ctx: &mut RContext = &mut ALLRCONTEXTS.get_mut()[0];
        let alloc_sz = sz;
        let running_cnt = 10;
        let flag = ib_access_flags::IB_ACCESS_LOCAL_WRITE |
            ib_access_flags::IB_ACCESS_REMOTE_READ |
            ib_access_flags::IB_ACCESS_REMOTE_WRITE;
        let mut ptr: Box<[core::mem::MaybeUninit<i8>], VmallocAllocator> =
            Box::new_zeroed_slice_in(alloc_sz as usize, VmallocAllocator);
        for _ in 0..running_cnt {
            profile.reset_timer();
            let mr = unsafe {
                ib_alloc_mr(ctx.get_pd(),
                            ib_mr_type::IB_MR_TYPE_MEM_REG, en_len as u32)
            };
            profile.tick_record(0);
            let l_pblock = RMemVirt::create_from_mr(ctx, mr, ptr[0].as_mut_ptr(), alloc_sz as u64);
            send_reg_mr(qp, l_pblock.get_inner_mr(), flag, true);
            wait_one_cq(rc.get_cq());
            profile.tick_record(1);
            // normal_req(&rc, ctx, &l_pblock, sz);
        }
        profile.increase_op(running_cnt);
        println!("RegMR sz: [ {} ]", sz);
        profile.report(2);
    }
    for sz in sz_list {
        run_single_test(&rc, sz);
    }
}


pub fn bench_pin() {
    use rust_kernel_rdma_base::bindings::{get_free_page, get_user_pages};
    let alloc_sz = 4 * K;
    const PAGE_MASK: u64 = 4096 - 1;
    unsafe {
        let mut ptr: Box<[core::mem::MaybeUninit<i8>], VmallocAllocator> =
            Box::new_zeroed_slice_in(alloc_sz as usize, VmallocAllocator);
        let addr = ptr[0].as_mut_ptr() as u64 & PAGE_MASK;
        let page_list = get_free_page(GFP_KERNEL as i32) as *mut *mut page;
        let ret = get_user_pages(addr,
                                 1, 1, page_list, null_mut());
        println!("ret:{}", ret);
    }
}


/// Benchmark upon vma random split
pub fn bench_vma() {
    let ctx: &mut RContext = &mut ALLRCONTEXTS.get_mut()[0];
    let flag = ib_access_flags::IB_ACCESS_LOCAL_WRITE |
        ib_access_flags::IB_ACCESS_REMOTE_READ |
        ib_access_flags::IB_ACCESS_REMOTE_WRITE;
    let mut explorer = IBExplorer::new();
    let remote_service_id = 50;
    let qd_hint = 73;
    let gid_addr = ctx.get_gid_as_string();
    let _ = explorer.resolve_v2(1, ctx, &gid_addr);
    // thus the path resolver could be split out of other module
    let path_res = explorer.get_path_result().unwrap();
    // create Ctrl. serve for cm handshakes
    let _ctrl = RCtrl::create(remote_service_id, &mut ALLRCONTEXTS.get_mut()[0]).unwrap();

    // create RC at one side
    let rc = RC::new(&mut ALLRCONTEXTS.get_mut()[0], core::ptr::null_mut(), core::ptr::null_mut());

    let mut rc = rc.unwrap();
    let mrc = unsafe { Arc::get_mut_unchecked(&mut rc) };
    // connect to the server (Ctrl)
    let _ = mrc.connect(qd_hint + 1, path_res, remote_service_id as u64);
    let mut sz_list = vec![3740, 8, 604, 4360, 20, 2044, 8, 8, 2044, 4, 4, 28, 2044, 4, 4, 204, 2044, 4, 4, 192, 2048, 68, 584, 2048, 16, 44, 80, 2044, 4, 4, 464, 2044, 8, 8, 236, 2044, 4, 8, 536, 2048, 28, 72, 2052, 68, 2044, 4, 4, 768, 16, 2044, 4, 4, 132, 2044, 4, 4, 28, 2044, 4, 8, 60, 2044, 4, 4, 16, 2044, 4, 4, 512, 136, 2044, 4, 16, 516, 24, 2044, 4, 4, 768, 2156, 2044, 112, 48, 12, 376, 2048, 16, 28, 92, 2048, 4, 20, 256, 1280, 148, 2044, 16, 4, 244, 2048, 8, 24, 4, 24, 2044, 4, 8, 4944, 1056, 2044, 4, 4, 100, 2044, 4, 4, 152, 2048, 8, 4, 8, 2044, 4, 4, 12, 2044, 4, 4, 1792, 2048, 16, 8, 16, 96, 2044, 4, 4, 16, 152, 1812, 4, 28, 4, 4, 4, 132, 4];
    for i in 0..sz_list.len() {
        sz_list[i] = K * sz_list[i];
    }

    let running_cnt = 5;
    let qp = rc.get_qp();
    let len = sz_list.len();

    for idx in 0..len {
        let mut total_len = 0; // total
        for j in 0..=idx {
            total_len += sz_list[j];
        }
        let mut profile = Profile::new();
        if total_len >= 512 * M {
            break;
        }


        for _ in 0..running_cnt {
            for j in 0..=idx {
                // for each vma size
                let vma_sz = sz_list[j];
                let mut ptr: Box<[core::mem::MaybeUninit<i8>], VmallocAllocator> =
                    Box::new_zeroed_slice_in(vma_sz as usize, VmallocAllocator);
                let en_len = 1 + (vma_sz / PG_SZ);
                profile.reset_timer();
                let mr = unsafe {
                    ib_alloc_mr(ctx.get_pd(),
                                ib_mr_type::IB_MR_TYPE_MEM_REG, en_len as u32)
                };
                profile.tick_record(0);
                let l_pblock = RMemVirt::create_from_mr(ctx, mr,
                                                        ptr[0].as_mut_ptr(), vma_sz as u64);
                send_reg_mr(qp, l_pblock.get_inner_mr(), flag, true);
                wait_one_cq(rc.get_cq());
                profile.tick_record(1);
            }
        }
        profile.increase_op(running_cnt);
        println!("=========================");
        println!("number of vma: {}, total len: {}", 1 + idx, total_len);
        profile.report(2);
    }
}
