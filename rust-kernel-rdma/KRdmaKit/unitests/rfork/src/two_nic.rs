use alloc::sync::Arc;
use KRdmaKit::cm::SidrCM;
use KRdmaKit::ctrl::RCtrl;
use KRdmaKit::device::RContext;
use KRdmaKit::ib_path_explorer::IBExplorer;
use KRdmaKit::mem::{Memory, RMemPhy};
use KRdmaKit::qp::{UD, UDOp};
use crate::{ALLRCONTEXTS, println};

pub fn demo_use_two_nics() {
    use crate::ib_wc;
    let ctx: &mut RContext = &mut ALLRCONTEXTS.get_mut()[0];
    let ctx_1: &mut RContext = &mut ALLRCONTEXTS.get_mut()[1];

    // preparation
    let mut explorer = IBExplorer::new();

    let gid_addr = ctx.get_gid_as_string();
    let _ = explorer.resolve_v2(1, ctx, &gid_addr);

    let path_res = explorer.get_path_result().unwrap();
    // param
    let remote_service_id = 50;
    let qd_hint = 73;

    let mut ctrl = RCtrl::create(remote_service_id, &mut ALLRCONTEXTS.get_mut()[0]).unwrap();
    // end of preparation
    // let cm = SidrCM::new(ctx, core::ptr::null_mut());

    let mut ud = UD::new(ctx).unwrap();
    let server_ud = UD::new(ctx).unwrap();
    ctrl.reg_ud(qd_hint, server_ud);
    println!("create UD success");

    let mut sidr_cm = SidrCM::new(ctx, core::ptr::null_mut()).unwrap();
    // sidr handshake. Only provide the `path` and the `service_id`
    let remote_info = sidr_cm.sidr_connect(
        path_res, remote_service_id as u64, qd_hint as u64);

    if remote_info.is_err() {
        return;
    }
    let mqp = unsafe { Arc::get_mut_unchecked(&mut ud) };
    // ================= start to send message =====================
    let point = remote_info.unwrap();

    let mut test_mr = RMemPhy::new(1024 * 1024);
    // 2. get all of the params for later operation
    let lkey = unsafe { ctx.get_lkey() } as u32;
    let laddr = test_mr.get_pa(0) as u64;   // use pa for RCOp
    let raddr = test_mr.get_pa(16) as u64;

    // va of the mr. These addrs are used to read/write value in test function
    let va_buf = (test_mr.get_ptr() as u64) as *mut i8;
    let target_buf = (test_mr.get_ptr() as u64 + 16 as u64) as *mut i8;

    // now write a value
    let write_value = 122 as i8;
    unsafe { *(va_buf) = write_value };  // client side , set value 4

    for i in 0..64 {
        let buf = (target_buf as u64 + i as u64) as *mut i8;
        unsafe { *(buf) = 0 };
    }
    unsafe { *(target_buf) = 4 };

    println!("[before] value = {}", unsafe { *(target_buf) });

    let mut send_op = UDOp::new(mqp); // recv buffer could be small at send side
    // server side
    let ctx_key = unsafe { ctx.get_lkey() };
    let server_ud = ctrl.get_ud(qd_hint as usize).unwrap();
    let mut recv_op = UDOp::new(server_ud.as_ref());
    let res = recv_op.post_recv(raddr, ctx_key, 512);
    if res.is_err() {
        println!("post recv error");
    }

    let _ = send_op.send(laddr as u64, ctx_key, &point,
                         64 as usize);
    {
        let mut wcs: ib_wc = Default::default();
        let mut cnt = 0;
        loop {
            let res = recv_op.pop_recv_cq(1, &mut wcs as _);
            if res.is_some() {
                println!("succ, wc status:{}", wcs.status);
                break;
            } else {
                cnt += 1;
                if cnt > 1000 {
                    println!("time out while pop recv cq");
                    break;
                }
            }
        }
    }
}