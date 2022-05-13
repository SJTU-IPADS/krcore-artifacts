use KRdmaKit::rust_kernel_rdma_base::*;
use KRdmaKit::qp::{RC, RCOp, DCTargetMeta, DC, DCOp, DoorbellHelper, UD, UDOp};
use KRdmaKit::ib_path_explorer::IBExplorer;
use KRdmaKit::cm::{SidrCM, EndPoint};
use KRdmaKit::consts::{DEFAULT_RPC_HINT, UD_HEADER_SZ};
use crate::alloc::string::ToString;
use linux_kernel_module::{bindings, KernelResult, println};
use linux_kernel_module::c_types::{c_uint, c_ulong, c_long, c_int};
use linux_kernel_module::c_types::c_void;
use linux_kernel_module::bindings::{_copy_from_user, _copy_to_user};
use crate::bindings::*;
use crate::core::{qp_connect, post_recv, pop_recv, handle_pop_ret};
use crate::client::{get_global_test_mem_pa, get_global_rctrl, get_global_sa_client, get_global_rctrl_len, get_global_rcontext, get_bind_ctrl, MAX_SERVICE_NUM};

use alloc::sync::Arc;
use core::cmp::min;
use alloc::string::{String};
use hashbrown::HashMap;
use alloc::borrow::ToOwned;
// use alloc::prelude::v1::{ToString, Vec};
use core::pin::Pin;
use alloc::boxed::Box;
use KRdmaKit::ctrl::RCtrl;
use KRdmaKit::rust_kernel_rdma_base::linux_kernel_module::mutex::LinuxMutex;
use KRdmaKit::rust_kernel_rdma_base::linux_kernel_module::sync::Mutex;
use KRdmaKit::qp::RecvHelper;
use crate::op_code_table;
use core::ptr::null_mut;
use KRdmaKit::rust_kernel_rdma_base::rust_kernel_linux_util::timer::KTimer;
use KRdmaKit::mem::pa_to_va;

/// Virtual queue
#[allow(dead_code)]
pub struct VQ<'a> {
    virtual_queue: Option<Arc<RC>>,
    local_dc: Option<&'a Arc<DC>>,
    local_ud: Option<&'a Arc<UD>>,
    // local path_rec cache
    path_rec_cache: HashMap<String, sa_path_rec>,

    // local dct meta cache
    dct_meta_cache: HashMap<String, DCTargetMeta>,

    reg_mem_addr: Option<u64>,

    lkey: u32,

    bind_port: Option<usize>,
    // for twosided server side
    connect_port: Option<usize>,
    // cache up <vid, endpoint>
    // cached_client_endpoint: Vec<Option<EndPoint>>,
    cached_client_endpoint: HashMap<usize, EndPoint>,

    // for twosided client side
    remote_endpoint: Option<EndPoint>,
    put_ud_info: bool,
}


impl<'a> linux_kernel_module::file_operations::FileOperations for VQ<'a> {
    fn open(_file: *mut bindings::file) -> KernelResult<Self> {
        Ok(Self {
            virtual_queue: None,
            local_dc: None,
            local_ud: None,
            bind_port: None,
            path_rec_cache: Default::default(),
            dct_meta_cache: Default::default(),
            reg_mem_addr: Some(get_global_test_mem_pa(0 as usize)),
            lkey: unsafe { get_global_rcontext(0).get_lkey() },
            connect_port: None,
            cached_client_endpoint: Default::default(),
            remote_endpoint: None,
            put_ud_info: false,
        })
    }

    fn ioctrl(&mut self, cmd: c_uint, arg: c_ulong) -> c_long {
        let mut req: req_t = Default::default();
        unsafe {
            _copy_from_user(
                (&mut req as *mut req_t).cast::<c_void>(),
                arg as *mut c_void,
                core::mem::size_of_val(&req) as u64,
            )
        };
        let status = match cmd {
            lib_r_cmd::Nil => reply_status::ok,
            lib_r_cmd::Connect => {
                if self.virtual_queue.is_some() {
                    return reply_status::already_connected as i64;
                }
                let mut conn: connect_t = Default::default();
                unsafe {
                    _copy_from_user(
                        (&mut conn as *mut connect_t).cast::<c_void>(),
                        (arg + core::mem::size_of_val(&req) as u64) as *mut c_void,
                        core::mem::size_of_val(&conn) as u64,
                    )
                };
                // parse the str
                // 39 is the default GUID sz
                let mut addr_buf: [u8; 39] = [0; 39];
                unsafe {
                    _copy_from_user(
                        addr_buf.as_mut_ptr().cast::<c_void>(),
                        conn.addr as *mut c_void,
                        core::cmp::min(39, conn.addr_sz as u64),
                    )
                };
                // now get addr of GID format
                let addr = core::str::from_utf8(&addr_buf).unwrap();
                let service_id = conn.port % MAX_SERVICE_NUM as i32;
                self.connect_impl(conn.vid as usize, addr, service_id as usize)
            }
            lib_r_cmd::RegMR => {
                reply_status::ok
            }
            lib_r_cmd::Push => {
                let mut push_req: push_core_req_t =
                    unsafe { core::mem::MaybeUninit::uninit().assume_init() };

                let mut push_ext: push_ext_req_t =
                    unsafe { core::mem::MaybeUninit::uninit().assume_init() };
                unsafe {
                    // get mread_core_req
                    _copy_from_user(
                        (&mut push_req as *mut push_core_req_t).cast::<c_void>(),
                        (arg + core::mem::size_of_val(&req) as u64) as *mut c_void,
                        core::mem::size_of_val(&push_req) as u64,
                    );

                    _copy_from_user(
                        (&mut push_ext as *mut push_ext_req_t).cast::<c_void>(),
                        (arg + core::mem::size_of_val(&req) as u64 +
                            core::mem::size_of_val(&push_req) as u64
                        ) as *mut c_void,
                        core::mem::size_of_val(&push_ext) as u64,
                    );
                }
                let pop_at_once: bool = if push_ext.pop_res == 0 { false } else { true };
                let push_recv_cnt: u32 = push_ext.push_recv_cnt;

                let mut ret = reply_status::ok;
                if !self.is_bind_mode() && push_recv_cnt > 0 {
                    ret = self.push_recv_impl(push_recv_cnt as usize);
                }

                const DEFAULT_BATCH_SZ: usize = 64;
                let req_len = push_req.req_len as usize;
                // no need to handle
                let mut send_offset: usize = 0;
                let mut core_req_list: [core_req_t; DEFAULT_BATCH_SZ] =
                    [unsafe { core::mem::MaybeUninit::uninit().assume_init() }; DEFAULT_BATCH_SZ];
                let sizeof: usize = core::mem::size_of_val(&core_req_list[0]);  // sizeof each wqe
                // batch send
                while send_offset < req_len {
                    let send_len = min(DEFAULT_BATCH_SZ, req_len - send_offset);

                    // get req from ptr. max length is 36
                    unsafe {
                        _copy_from_user(
                            (&mut core_req_list[0] as *mut core_req_t).cast::<c_void>(),
                            (push_req.req_list as u64 + (send_offset * sizeof) as u64)
                                as *mut c_void,
                            (send_len * sizeof) as u64,
                        );
                    };
                    ret = match self.is_bind_mode() {
                        true => {
                            self.bind_server_push_impl(&core_req_list[0..send_len], pop_at_once)
                        }
                        false => {
                            if self.virtual_queue.is_some() {
                                self.rc_push_impl(&core_req_list[0..send_len])
                            } else {
                                // self.dc_push_impl(&core_req_list[0..send_len]) // todo: replace with UD
                                self.ud_push_impl(&core_req_list[0..send_len])
                            }
                        }
                    };
                    send_offset += send_len;
                    if reply_status::err == ret {
                        println!(
                            "cmd push err, send len = {}, offset = {}",
                            send_len, send_offset
                        );
                        break;
                    }
                }

                if !self.is_bind_mode() && pop_at_once && ret == reply_status::ok { // pop res
                    let pop_res = if self.virtual_queue.is_none() {
                        // dc
                        let qp = self.get_ud();
                        let mut op = UDOp::new(qp);
                        op.wait_til_comp()
                    } else {
                        let mut op = RCOp::new(self.virtual_queue.as_ref().unwrap());
                        op.wait_til_comp()
                    };
                    if pop_res.is_none() {
                        ret = reply_status::err;
                    }
                }
                ret
            }
            lib_r_cmd::PushRecv => {
                let mut push_req: push_recv_t =
                    unsafe { core::mem::MaybeUninit::uninit().assume_init() };

                unsafe {
                    // get mread_core_req
                    _copy_from_user(
                        (&mut push_req as *mut push_recv_t).cast::<c_void>(),
                        (arg + core::mem::size_of_val(&req) as u64) as *mut c_void,
                        core::mem::size_of_val(&push_req) as u64,
                    );
                }
                self.push_recv_impl(push_req.push_count as usize)
            }
            lib_r_cmd::Pop => {
                let mut vid: u32 = 0;
                unsafe {
                    _copy_from_user(
                        (&mut vid as *mut u32).cast::<c_void>(),
                        (arg + core::mem::size_of_val(&req) as u64) as *mut c_void,
                        core::mem::size_of_val(&vid) as u64,
                    );
                }
                self.pop_impl(&mut req, vid as usize)
            }
            lib_r_cmd::PopMsgs => {
                let mut least_pop_cnt = 0 as u32;
                let mut payload_sz = 0 as u32;
                unsafe {
                    _copy_from_user(
                        (&mut least_pop_cnt as *mut u32).cast::<c_void>(),
                        (arg + core::mem::size_of_val(&req) as u64) as *mut c_void,
                        core::mem::size_of_val(&least_pop_cnt) as u64,
                    );

                    _copy_from_user(
                        (&mut payload_sz as *mut u32).cast::<c_void>(),
                        (arg +
                            core::mem::size_of_val(&req) as u64 +
                            core::mem::size_of_val(&least_pop_cnt) as u64) as *mut c_void,
                        core::mem::size_of_val(&payload_sz) as u64,
                    );
                }
                self.pop_msg_impl(&mut req, least_pop_cnt, payload_sz)
            }
            lib_r_cmd::Binds => {
                let mut bind: bind_t = Default::default();
                unsafe {
                    _copy_from_user(
                        (&mut bind as *mut bind_t).cast::<c_void>(),
                        (arg + core::mem::size_of_val(&req) as u64) as *mut c_void,
                        core::mem::size_of_val(&bind) as u64,
                    )
                };
                let port = bind.port as usize;
                if self.bind_port.is_some() {
                    reply_status::already_bind
                } else if port > MAX_SERVICE_NUM {
                    reply_status::nil
                } else {
                    self.bind_port = Some(port);
                    reply_status::ok
                }
            }
            lib_r_cmd::UnBinds => {
                let mut bind: bind_t = Default::default();
                unsafe {
                    _copy_from_user(
                        (&mut bind as *mut bind_t).cast::<c_void>(),
                        (arg + core::mem::size_of_val(&req) as u64) as *mut c_void,
                        core::mem::size_of_val(&bind) as u64,
                    )
                };
                let port = bind.port as usize;
                // first check duplicate binding. if so, unlisten to the port
                let ret = if self.bind_port.is_none() || self.bind_port.unwrap() != port {
                    reply_status::not_bind
                } else {
                    // clean up the message buffer
                    pop_recv(self.bind_port.unwrap() * 2, 2048, 0);
                    self.bind_port = None;
                    reply_status::ok
                };
                ret
            }
            _ => {
                println!("unknown ioctrl cmd {}", cmd);
                reply_status::err
            }
        };
        let mut reply: reply_t = Default::default();
        reply.status = status as i32;

        // copy the reply and return to user
        unsafe {
            _copy_to_user(
                req.reply_buf,
                (&reply as *const reply_t).cast::<c_void>(),
                core::mem::size_of_val(&reply) as u64,
            ) as c_long
        }
    }

    fn mmap(&mut self, _vma: *mut bindings::vm_area_struct) -> c_int {
        unimplemented!()
    }
}


impl<'a> VQ<'a> {
    /// Connect the QP according to the `addr`.
    /// The `qd` is a hint: if the `qd`'s corresponding Queue has been established in the kernel,
    /// then the virtual queue directly uses the kernel's physical queue.
    #[inline]
    fn connect_impl(&mut self, vid: usize, addr: &str, port: usize) -> u32 {
        // already migrate into RC
        if self.virtual_queue.is_some() {
            return reply_status::already_connected;
        }
        // let vid = vid % get_global_rctrl_len();
        let addr_p: Result<no_std_net::Guid, no_std_net::parser::AddrParseError> = (addr).parse();
        if addr_p.is_err() {
            return reply_status::addr_error;
        }
        self.reg_mem_addr = Some(get_global_test_mem_pa(port as usize));
        unsafe { self.lkey = get_global_rctrl(port).get_context().get_lkey(); }
        self.connect_port = Some(port);
        // connect by dct
        #[cfg(feature = "dct_qp")]
            {
                // first check local_dc
                if self.local_dc.is_none() {
                    let ctrl = get_global_rctrl(port as usize);
                    self.local_dc = ctrl.get_dc();
                    self.local_ud = ctrl.get_ud(DEFAULT_RPC_HINT);
                }
                // next check cache key
                let dct_meta_cache_k = String::from(addr.to_owned() + port.to_string().as_str());
                // dct meta info
                let dct_meta = match self.get_dct_meta_cache(&dct_meta_cache_k) {
                    Some(data) => data,
                    None => {
                        let ctrl = get_global_rctrl(port as usize);
                        let ctx = ctrl.get_context();
                        // get ib_path_rec
                        let path_res = self.explore_path(port, &String::from(addr));
                        if path_res.is_err() {
                            return reply_status::err;
                        }
                        let path_res = path_res.unwrap();
                        let mut sidr_cm =
                            SidrCM::new(ctx, core::ptr::null_mut()).unwrap();
                        let remote_info = sidr_cm.sidr_connect(
                            path_res, port as u64,
                            DEFAULT_RPC_HINT as u64);
                        if remote_info.is_err() {
                            return reply_status::err;
                        }
                        let point = remote_info.unwrap();
                        let meta = DCTargetMeta {
                            dct_num: point.dct_num,
                            lid: point.lid,
                            gid: point.gid,
                            mr: point.mr,
                        };

                        let mut ctrl = get_global_rctrl(port as usize);
                        let pd = ctrl.get_context().get_pd();
                        {
                            self.remote_endpoint = Some(EndPoint::new(
                                pd, point.qpn, point.qkey, point.lid,
                                point.gid, point.dct_num, &point.mr,
                            ));
                        }

                        self.update_dct_meta_cache(&dct_meta_cache_k, &meta);
                        meta
                    }
                };
                return reply_status::ok;
            }
        let path_res = self.explore_path(port, &String::from(addr));
        if path_res.is_err() {
            return reply_status::err;
        }
        let path_res = path_res.unwrap();
        return match qp_connect(vid, port, &path_res) {
            Ok(qp) => {
                #[cfg(feature = "virtual_queue")]
                    {
                        self.virtual_queue = qp;
                    }
                reply_status::ok
            }
            Err(_) => reply_status::err
        };
    }
}

impl<'a> VQ<'a> {
    #[inline]
    fn rc_push_impl(&mut self, req_list: &[core_req_t]) -> u32 {
        if self.virtual_queue.is_none() {
            // should not be here!
            println!("vq not exist");
            return reply_status::nil;
        }
        let qp = self.virtual_queue.as_ref().unwrap();
        let local_pa = self.reg_mem_addr.unwrap();
        let remote_mr = qp.get_remote_mr();
        let lkey = self.lkey;
        let rkey = remote_mr.get_rkey() as u32;
        let mut op = RCOp::new(qp);

        let mut res: u32 = reply_status::ok;
        for idx in 0..req_list.len() {
            let req = req_list[idx];

            // For all of the params

            let mut laddr = local_pa + (req.addr as u64);
            let raddr = req.remote_addr as u64 + remote_mr.get_addr();
            let length: usize = req.length as usize;
            let vid = req.vid as u32;
            let op_code: u32 = op_code_table(req.type_);

            // inline check
            let mut send_flag: i32 = if length < 64 {
                // pa to va while inline sending
                match op_code {
                    ib_wr_opcode::IB_WR_RDMA_READ => 0,
                    ib_wr_opcode::IB_WR_RDMA_WRITE => {
                        laddr = unsafe { pa_to_va(laddr as *mut i8) } as u64;
                        ib_send_flags::IB_SEND_INLINE
                    }
                    ib_wr_opcode::IB_WR_RDMA_WRITE_WITH_IMM => {
                        laddr = unsafe { pa_to_va(laddr as *mut i8) } as u64;
                        ib_send_flags::IB_SEND_INLINE
                    }
                    _ => 0
                }
            } else {
                0
            };
            send_flag |= match req.send_flags {
                0 => send_flag | 0,
                _ => send_flag | ib_send_flags::IB_SEND_SIGNALED,
            };


            if op.push_with_imm(
                op_code, laddr, lkey, length,
                raddr, rkey, vid, send_flag,
            ).is_err() {
                res = reply_status::err;
                break;
            }
        }

        return res;
    }

    #[inline]
    fn dc_push_impl(&mut self, req_list: &[core_req_t]) -> u32 {
        let s_lkey = self.lkey;
        if self.local_dc.is_none() {
            // should not be here!
            println!("dc not connected");
            return reply_status::nil;
        }
        let dc = self.get_dc();
        let point = self.remote_endpoint.as_ref().unwrap();
        let local_pa = self.reg_mem_addr.unwrap();
        // For all of the params
        let remote_mr = point.mr;
        let lkey = s_lkey;
        let rkey = remote_mr.get_rkey() as u32;

        let mut op = DCOp::new(dc);

        let mut res: u32 = reply_status::ok;
        for idx in 0..req_list.len() {
            let req = req_list[idx];

            // For all of the params
            let mut laddr = local_pa + (req.addr as u64);
            let raddr = req.remote_addr as u64 + remote_mr.get_addr();

            // let vid = req.vid as u32;
            let length: usize = req.length as usize;
            let op_code: u32 = op_code_table(req.type_);

            // inline check
            let mut send_flag: i32 = if length < 64 {
                // pa to va while inline sending
                match op_code {
                    ib_wr_opcode::IB_WR_RDMA_READ => 0,
                    ib_wr_opcode::IB_WR_RDMA_WRITE => {
                        laddr = unsafe { pa_to_va(laddr as *mut i8) } as u64;
                        ib_send_flags::IB_SEND_INLINE
                    }
                    ib_wr_opcode::IB_WR_RDMA_WRITE_WITH_IMM => {
                        laddr = unsafe { pa_to_va(laddr as *mut i8) } as u64;
                        ib_send_flags::IB_SEND_INLINE
                    }
                    _ => 0
                }
            } else {
                0
            };
            send_flag |= match req.send_flags {
                0 => send_flag | 0,
                _ => send_flag | ib_send_flags::IB_SEND_SIGNALED,
            };


            if op.push(op_code, laddr,
                       lkey, length, raddr, rkey,
                       point, send_flag).is_err() {
                res = reply_status::err;
                break;
            }
        }

        return res;
    }


    #[inline]
    fn ud_push_impl(&mut self, req_list: &[core_req_t]) -> u32 {
        let s_lkey = self.lkey;
        if self.local_ud.is_none() || self.connect_port.is_none() {
            // should not be here!
            println!("ud not connected");
            return reply_status::nil;
        }
        let point = self.remote_endpoint.as_ref().unwrap();
        let local_pa = self.reg_mem_addr.unwrap();
        let mut laddr = local_pa;
        let va = unsafe { pa_to_va(laddr as *mut i8) };

        let node: *mut EndPoint = va as *mut EndPoint;
        if !self.put_ud_info {
            let ud = self.get_ud();
            let ctrl = get_global_rctrl(self.connect_port.unwrap());
            let point = EndPoint {
                qpn: ud.get_qp_num(),
                qkey: ud.get_qkey(),
                lid: ctrl.get_context().get_lid(),
                gid: ctrl.get_context().get_gid(),
                dct_num: ctrl.get_dc_num(),
                mr: ctrl.get_self_test_mr(),
                ah: null_mut(),
            };
            unsafe { *node = point };
            self.put_ud_info = true;
        }
        let ud = self.get_ud();
        // For all of the params
        let lkey = s_lkey;
        let mut op = UDOp::new(ud);

        let mut res: u32 = reply_status::ok;
        for idx in 0..req_list.len() {
            let req = req_list[idx];

            // For all of the params

            let length: usize = req.length as usize;
            let op_code: u32 = op_code_table(req.type_);
            let vid = req.vid;

            // inline check
            let mut send_flag: i32 = if length < 64 {
                // pa to va while inline sending
                match op_code {
                    ib_wr_opcode::IB_WR_RDMA_READ => 0,
                    ib_wr_opcode::IB_WR_RDMA_WRITE => {
                        laddr = va as u64;
                        ib_send_flags::IB_SEND_INLINE
                    }
                    ib_wr_opcode::IB_WR_RDMA_WRITE_WITH_IMM => {
                        laddr = va as u64;
                        ib_send_flags::IB_SEND_INLINE
                    }
                    _ => 0
                }
            } else {
                0
            };
            send_flag |= match req.send_flags {
                0 => send_flag | 0,
                _ => send_flag | ib_send_flags::IB_SEND_SIGNALED,
            };

            if op.push(op_code, laddr, lkey,
                       point, length, vid, send_flag).is_err() {
                res = reply_status::err;
                break;
            }
        }

        return res;
    }


    #[inline]
    fn bind_server_push_impl(&mut self, req_list: &[core_req_t], pop_at_once: bool) -> u32 {
        let mut res: u32 = reply_status::ok;
        let ctrl = self.get_bind_ctrl_unsafe();
        let local_pa = self.reg_mem_addr.unwrap();
        let mut laddr = local_pa;
        let lkey = self.lkey;
        let rkey = unsafe { ctrl.get_context().get_rkey() };
        const HOST_LEN: usize = 12;
        let mut rc_pop_cnt_cache: [u32; HOST_LEN] = [0; HOST_LEN];
        let mut ud_pop_cnt_cache: [u32; HOST_LEN] = [0; HOST_LEN];

        for idx in 0..req_list.len() {
            let req = req_list[idx];
            let vid = req.vid as usize % HOST_LEN;
            if vid == 0 {
                println!("invalid vid:{}", vid);
            }
            // if not connected, use the ud
            if !self.check_bind(vid) {
                let wr_id = req.remote_addr as u64 + UD_HEADER_SZ as u64; // with 40 bytes offset
                let endpoint = match self.get_client_endpoint_cache(&vid) {
                    Some(point) => {
                        point
                    }
                    None => {
                        let message_va = wr_id;
                        let point = (message_va as *mut EndPoint);
                        self.update_client_endpoint_cache(&vid, ctrl.get_context().get_pd(), point);
                        self.get_client_endpoint_cache(&vid).unwrap()
                    }
                };
                // Single UD push. TODO: migrate to DC in the future
                let qp = ctrl.get_ud(DEFAULT_RPC_HINT).unwrap();
                let mut op = UDOp::new(qp.as_ref());
                let send_flag: i32 = if ud_pop_cnt_cache[vid] == 0 {
                    ib_send_flags::IB_SEND_SIGNALED
                } else {
                    0
                };
                ud_pop_cnt_cache[vid] += 1;

                let op_code: u32 = op_code_table(req.type_);

                if op.push(op_code, laddr, lkey, endpoint,
                           0, 0, send_flag).is_err() {
                    res = reply_status::err;
                    break;
                }
            } else {
                // Single Two sided RC push

                let qp = ctrl.get_trc(vid).unwrap();
                let mut op = RCOp::new(qp);

                // For all of the params
                let laddr = local_pa + (req.addr as u64);
                let raddr = req.remote_addr as u64 + get_global_test_mem_pa(0);
                let length: usize = req.length as usize;
                let op_code: u32 = op_code_table(req.type_);
                let send_flag: i32 = if rc_pop_cnt_cache[vid] == 0 {
                    ib_send_flags::IB_SEND_SIGNALED
                } else {
                    0
                };
                rc_pop_cnt_cache[vid] += 1;

                if op.push_with_imm(op_code, laddr, lkey, length,
                                    raddr, rkey, vid as u32, send_flag,
                ).is_err() {
                    res = reply_status::err;
                    break;
                }
            }
        }

        if res == reply_status::ok {
            for vid in 1..HOST_LEN {
                if ud_pop_cnt_cache[vid] > 0 {
                    // post first
                    ctrl.ud_post_recv(ud_pop_cnt_cache[vid] as usize);

                    // UD pop
                    if pop_at_once {
                        let qp = ctrl.get_ud(DEFAULT_RPC_HINT).unwrap();
                        let mut op = UDOp::new(qp.as_ref());
                        if op.wait_til_comp().is_none() {
                            res = reply_status::err;
                            break;
                        }
                    }
                }

                if rc_pop_cnt_cache[vid] > 0 {
                    // post first
                    ctrl.trc_post_recv(vid, rc_pop_cnt_cache[vid] as usize);
                    if pop_at_once {
                        let qp = ctrl.get_trc(vid).unwrap();

                        let mut op = RCOp::new(qp);
                        if op.wait_til_comp().is_none() {
                            res = reply_status::err;
                            break;
                        }
                    }
                }
            }
        }


        return res;
    }
}

impl<'a> VQ<'a> {
    #[inline]
    fn explore_path(&mut self, vid: usize, addr: &String) -> KernelResult<sa_path_rec> {
        let path_cache = self.get_path_rec_cache(addr);

        if path_cache.is_some() {
            Ok(path_cache.unwrap())
        } else {
            let remote_service_id = vid as u64;
            let ctrl = get_global_rctrl(remote_service_id as usize);
            let ctx = ctrl.get_context();
            let sa_client = get_global_sa_client();
            let mut explorer = IBExplorer::new();

            explorer.resolve(remote_service_id as u64, ctx, addr,
                             sa_client.get_inner_sa_client())?;
            let ret = explorer.get_path_result().unwrap();
            // insert cache
            self.update_path_rec_cache(addr, ret);
            Ok(ret)
        }
    }

    #[inline]
    fn update_dct_meta_cache(&mut self, key: &String, v: &DCTargetMeta) {
        self.dct_meta_cache.insert((*key).clone(), v.clone());
    }

    #[inline]
    fn evict_dct_meta_cache(&mut self, key: &String) {
        self.dct_meta_cache.remove(key);
    }

    #[inline]
    fn get_dct_meta_cache(&self, key: &String) -> Option<DCTargetMeta> {
        self.dct_meta_cache.get(key).map(|v| v.clone())
    }

    #[inline]
    fn update_path_rec_cache(&mut self, k: &String, v: sa_path_rec) {
        self.path_rec_cache.insert((*k).clone(), v);
    }

    #[inline]
    fn get_path_rec_cache(&self, k: &String) -> Option<sa_path_rec> {
        self.path_rec_cache.get(k).map(|v| v.clone())
    }

    #[inline]
    fn get_client_endpoint_cache(&self, k: &usize) -> Option<&EndPoint> {
        if !self.cached_client_endpoint.contains_key(k) {
            None
        } else {
            self.cached_client_endpoint.get(k)
            // self.cached_client_endpoint[*k].as_ref()
        }
    }

    #[inline]
    fn update_client_endpoint_cache(&mut self, k: &usize, pd: *mut ib_pd, point: *mut EndPoint) {
        // if self.cached_client_endpoint.is_empty() {
        //     for _ in 0..15 {
        //         self.cached_client_endpoint.push(None);
        //     }
        // }
        // unsafe {
        //     self.cached_client_endpoint[*k] = Option::from(EndPoint::new(
        //         pd, (*point).qpn, (*point).qkey,
        //         (*point).lid, (*point).gid, (*point).dct_num, &(*point).mr,
        //     ));
        // }
        unsafe {
            self.cached_client_endpoint.insert(*k, EndPoint::new(
                pd, (*point).qpn, (*point).qkey,
                (*point).lid, (*point).gid, (*point).dct_num, &(*point).mr,
            ));
        }
    }

    #[inline]
    fn get_dc(&self) -> &DC {
        self.local_dc.as_ref().unwrap()
    }

    #[inline]
    fn get_ud(&self) -> &UD {
        self.local_ud.as_ref().unwrap()
    }
}

impl<'a> VQ<'a> {
    #[inline]
    fn check_bind(&self, vid: usize) -> bool {
        let ctrl = self.get_bind_ctrl();
        if ctrl.is_none() {
            return false;
        }
        return ctrl.unwrap().get_trc(vid).is_some();
    }

    #[inline]
    fn get_bind_ctrl(&self) -> Option<&'static mut Pin<Box<RCtrl<'static>>>> {
        if self.bind_port.is_none() {
            None
        } else {
            Some(get_bind_ctrl(self.bind_port.unwrap()))
        }
    }

    #[inline]
    fn get_bind_ctrl_unsafe(&self) -> &'static mut Pin<Box<RCtrl<'static>>> {
        get_bind_ctrl(self.bind_port.unwrap())
    }

    #[inline]
    fn is_bind_mode(&self) -> bool {
        self.bind_port.is_some()
    }
}


impl<'a> VQ<'a> {
    #[inline]
    fn push_recv_impl(&self, push_cnt: usize) -> u32 {
        let ret = if self.is_bind_mode() {
            // Bind mode
            if !self.check_bind(1) { // use backup UD
                reply_status::not_connected
            } else {
                post_recv(self.bind_port.unwrap() * 2, // note: use nic_0 right now! so we port * 2
                          push_cnt, 1)
            }
        } else {
            // Normal
            if self.connect_port.is_none() {
                reply_status::not_connected
            } else {
                let ctrl = get_global_rctrl(self.connect_port.unwrap());
                let qp = if self.virtual_queue.is_some() {
                    self.virtual_queue.as_ref().unwrap().get_qp()
                } else {
                    self.get_ud().get_qp()
                };
                let recv_buffer = ctrl.get_recv_buffer();
                let recv = unsafe { Arc::get_mut_unchecked(recv_buffer) };
                match recv.post_recvs(qp, null_mut(), push_cnt) {
                    Ok(_) => {
                        reply_status::ok
                    }
                    Err(_) => reply_status::err
                }
            }
        };
        ret
    }

    #[inline]
    fn pop_msg_impl(&self, req: &mut req_t, least_pop_cnt: u32, payload_sz: u32) -> u32 {
        let mut ret = reply_status::ok;
        let mut retry = 0;
        let mut act_pop_cnt = 0 as usize;
        // self.timer.reset();
        loop {
            let (pop_ret, pop_cnt) = {
                if self.is_bind_mode() {
                    pop_recv(2 * self.bind_port.unwrap(), 2048, 0)
                } else {
                    if self.connect_port.is_none() {
                        (None, 0)
                    } else {
                        pop_recv(self.connect_port.unwrap(), 2048 as usize, act_pop_cnt)
                    }
                }
            };
            if self.is_bind_mode() { // break when binding mode
                ret = handle_pop_ret(pop_ret, req, pop_cnt as usize, payload_sz);
                break;
            } else {
                act_pop_cnt += pop_cnt;
                if act_pop_cnt >= least_pop_cnt as usize || retry > 50000 {
                    ret = handle_pop_ret(pop_ret, req, act_pop_cnt as usize, payload_sz);
                    break;
                }
            }
            retry += 1;
        }
        return ret;
    }


    #[inline]
    fn pop_impl(&self, req: &mut req_t, vid: usize) -> u32 {
        let pop_ret = if self.is_bind_mode() {
            if !self.check_bind(vid as usize) {
                None
            } else {
                let ctrl = self.get_bind_ctrl_unsafe();
                let rc = ctrl.get_trc(vid as usize).unwrap();
                let mut op = RCOp::new(rc);
                op.pop()
            }
        } else {
            if self.virtual_queue.is_none() {
                let mut op = UDOp::new(self.get_ud());
                op.pop()
            } else {
                let mut op = RCOp::new(self.virtual_queue.as_ref().unwrap());
                op.pop()
            }
        };
        handle_pop_ret(pop_ret, req, 1, 8)
    }
}

