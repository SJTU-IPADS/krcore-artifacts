use KRdmaKit::rpc::data::IntValue;

/// Module RDMA meta kv.
/// With the help of module `RPC`, get the meta kv info
#[derive(Copy, Clone)]
pub enum RPCReqType {
    Dummy = 0,
    // register self dc meta information
    RegisterDCMeta,
    // deregister dc meta
    DeregisterDcMeta,
    // query remote dc meta info
    QueryDCMeta,
    // Connect RC via UD RPC
    ConnectRC,
    // Disconnect RC
    DisconnectRC,
}

impl IntValue for RPCReqType {
    fn value(&self) -> u8 {
        *self as u8
    }
}

pub mod payload {
    use KRdmaKit::rust_kernel_rdma_base::*;
    use KRdmaKit::cm::EndPoint;

    #[repr(C)]
    #[derive(Default)]
    pub struct RegDCMeta {
        // self gid
        pub gid: ib_gid,
        // port
        pub port: usize,
        pub local_point: EndPoint,
    }

    #[repr(C)]
    #[derive(Default)]
    pub struct DeRegDCMeta {
        // self gid
        pub gid: ib_gid,
    }

    #[repr(C)]
    #[derive(Default)]
    pub struct QueryDCMeta {
        // self gid
        pub gid: ib_gid,
    }

    #[repr(C)]
    #[derive(Copy, Clone, Debug, Default)]
    pub struct ConnectRC {
        pub gid: ib_gid,
        pub lid: u16,
        pub qpn: u32,
    }

    #[repr(C)]
    #[derive(Copy, Clone, Debug, Default)]
    pub struct DisconnectRC {
        pub qd: usize,
    }
}

pub mod reply {
    use KRdmaKit::rust_kernel_rdma_base::*;
    use KRdmaKit::cm::EndPoint;

    #[repr(C)]
    #[derive(Default)]
    pub struct RegDCMeta {
        pub meta_pa: u64,
    }

    #[repr(C)]
    #[derive(Default)]
    pub struct QueryDCMeta {
        pub point: EndPoint,
    }

    #[repr(C)]
    #[derive(Copy, Clone, Debug, Default)]
    pub struct ConnectRC {
        pub gid: ib_gid,
        pub lid: u16,
        pub qpn: u32,
        pub qd: usize,
    }
}

/// Caller which wrap the RPCClient
pub mod caller {
    use alloc::string::String;
    use core::ptr::null_mut;
    use nostd_async::{Runtime, Task};
    use KRdmaKit::cm::EndPoint;
    use KRdmaKit::net_util::str_to_gid;
    use KRdmaKit::qp::RC;
    use KRdmaKit::rpc::data::Header;
    use KRdmaKit::rust_kernel_rdma_base::*;
    use linux_kernel_module::{c_types, println};
    use rust_kernel_linux_util::bindings::memcpy;
    use crate::client::{get_global_rctrl, get_rpc_client};
    use crate::linux_kernel_module::random::getrandom;
    use crate::rpc::protocol::payload;
    use crate::rpc::protocol::reply::QueryDCMeta;
    use crate::rpc::reply;
    use crate::rpc::RPCReqType::DisconnectRC;

    pub fn call_reg_dc_meta(meta_point: &EndPoint)
                            -> Option<reply::RegDCMeta> {
        use crate::rpc::RPCReqType::RegisterDCMeta;
        let caller_client = get_rpc_client(1);
        let local_point = caller_client.get_end_point();
        let gid = local_point.gid;
        let mut header = Header::new(RegisterDCMeta, 1024);
        let runtime = Runtime::new();
        let mut randoms: [u8; 1] = [0x0; 1];
        let _ = getrandom(&mut randoms);
        let mut payload = payload::RegDCMeta { gid, port: randoms[0] as usize, local_point: local_point.self_clone() };
        let mut client_task = Task::new(async {
            caller_client.call::<payload::RegDCMeta, reply::RegDCMeta>(
                &meta_point,
                &mut header,
                &mut payload,
                500 * 1000).await
        });
        let c_handler = client_task.spawn(&runtime);
        c_handler.join()
    }

    pub fn call_query_dc_meta(meta_point: &EndPoint, query_gid: &String) -> Option<QueryDCMeta> {
        use crate::rpc::RPCReqType::QueryDCMeta;
        let caller_client = get_rpc_client(1);
        let _gid = caller_client.get_end_point().gid;
        let mut header = Header::new(QueryDCMeta, 1024);

        let runtime = Runtime::new();
        let mut payload = payload::QueryDCMeta { gid: str_to_gid(query_gid) };
        let mut client_task = Task::new(async {
            caller_client.call::<payload::QueryDCMeta, reply::QueryDCMeta>(
                &meta_point,
                &mut header,
                &mut payload,
                5 * 1000).await
        });
        let c_handler = client_task.spawn(&runtime);
        c_handler.join()
    }

    pub fn call_dereg_dc_meta(meta_point: &EndPoint) -> Option<u64> {
        use crate::rpc::RPCReqType::DeregisterDcMeta;
        let caller_client = get_rpc_client(1);
        let gid = caller_client.get_end_point().gid;
        let mut header = Header::new(DeregisterDcMeta, 1024);

        let runtime = Runtime::new();
        let mut payload = payload::DeRegDCMeta { gid };
        let mut client_task = Task::new(async {
            caller_client.call::<payload::DeRegDCMeta, u64>(
                &meta_point,
                &mut header,
                &mut payload,
                5 * 1000).await
        });
        let c_handler = client_task.spawn(&runtime);
        c_handler.join()
    }

    pub fn call_connect_rc(meta_point: &EndPoint, local_qpn: u32) -> Option<reply::ConnectRC> {
        use crate::rpc::RPCReqType::ConnectRC;
        let caller_client = get_rpc_client(1);
        let gid = caller_client.get_end_point().gid;
        let lid = caller_client.get_end_point().lid;
        let mut header = Header::new(ConnectRC, 1024);
        let runtime = Runtime::new();
        let mut payload = payload::ConnectRC { gid, lid, qpn: local_qpn };
        let mut client_task = Task::new(async {
            caller_client.call::<payload::ConnectRC, reply::ConnectRC>(
                &meta_point,
                &mut header,
                &mut payload,
                15 * 1000).await
        });
        let c_handler = client_task.spawn(&runtime);
        c_handler.join()
    }


    pub fn call_disconnect_rc(meta_point: &EndPoint, qd: usize) -> Option<u32> {
        use crate::rpc::RPCReqType::DisconnectRC;
        let caller_client = get_rpc_client(1);
        let mut header = Header::new(DisconnectRC, 1024);
        let runtime = Runtime::new();
        let mut payload = payload::DisconnectRC { qd };
        let mut client_task = Task::new(async {
            caller_client.call::<payload::DisconnectRC, u32>(
                &meta_point,
                &mut header,
                &mut payload,
                5 * 1000).await
        });
        let c_handler = client_task.spawn(&runtime);
        c_handler.join()
    }
}


/// Handlers for reg
pub mod handler {
    use alloc::boxed::Box;
    use core::ptr::null_mut;
    use KRdmaKit::cm::EndPoint;
    use KRdmaKit::mem::pa_to_va;
    use KRdmaKit::net_util::gid_to_str;
    use KRdmaKit::qp::{RC, rc_connector};
    use KRdmaKit::rust_kernel_rdma_base::*;
    use linux_kernel_module::{c_types, println};
    use rust_kernel_linux_util::bindings::memcpy;
    use crate::client::{evict_remote_dc_meta, get_global_meta_kv_mem, get_global_rctrl, get_global_test_mem_len, get_global_test_mem_pa, get_remote_dc_meta, get_remote_dc_meta_unsafe, get_rpc_client, put_remote_dc_meta};
    use KRdmaKit::rpc::data::IntValue;
    use KRdmaKit::rpc::RPCClient;
    use crate::rpc::reply::{ConnectRC, QueryDCMeta, RegDCMeta};
    use crate::rpc::RPCReqType::*;

    /// Fill the handler table entries
    #[inline(always)]
    pub fn fill_handler_table<'a, const N: usize>(rpc_client: &'a mut RPCClient<'a, N>) {
        rpc_client.reg_handler(RegisterDCMeta.value(), reg_dc_meta_handler);
        rpc_client.reg_handler(DeregisterDcMeta.value(), dereg_dc_meta_handler);
        rpc_client.reg_handler(QueryDCMeta.value(), query_dc_meta_handler);
        rpc_client.reg_handler(ConnectRC.value(), connect_rc_handler);
        rpc_client.reg_handler(DisconnectRC.value(), disconnect_rc_handler);
    }

    #[inline]
    pub fn reg_dc_meta_handler(req_va: u64, reply_va: u64) -> u32 {
        use crate::rpc::protocol::payload;

        let mut payload: payload::RegDCMeta = Default::default();

        unsafe {
            memcpy(
                (&mut payload as *mut payload::RegDCMeta).cast::<c_types::c_void>(),
                (req_va as *mut i8).cast::<c_types::c_void>(),
                core::mem::size_of_val(&payload) as u64,
            );
        }
        println!("[register meta] {:?}", payload.local_point);
        put_remote_dc_meta(&gid_to_str(payload.gid), &payload.local_point);
        // write to mr
        let idx: usize = payload.port as usize;
        let kv_meta_pa = get_global_test_mem_pa(idx);
        let endpoint_sz = core::mem::size_of::<EndPoint>() as u64;
        let entries_offset: u64 = core::mem::size_of::<u64>() as u64;

        for idx in 0..get_global_test_mem_len() {
            let kv_meta_pa = get_global_test_mem_pa(idx);
            let kv_meta_va = unsafe { pa_to_va(kv_meta_pa as *mut i8) };
            let metas = crate::client::META_INFO.get_mut();
            let mut idx = 0 as u64;
            let mut meta_len: u64 = metas.len() as u64;

            for k in metas.keys() {
                let meta = metas.get(k).unwrap();
                unsafe {
                    memcpy(
                        ((kv_meta_va + entries_offset + idx * endpoint_sz) as *mut EndPoint).cast::<c_types::c_void>(),
                        (&mut meta.self_clone() as *mut EndPoint).cast::<c_types::c_void>(),
                        endpoint_sz,
                    );
                }
                idx += 1;
            }

            // copy len
            unsafe {
                memcpy(
                    (kv_meta_va as *mut u64).cast::<c_types::c_void>(),
                    (&mut meta_len as *mut u64).cast::<c_types::c_void>(),
                    entries_offset,
                );
            }
        }
        let mut reply: RegDCMeta = RegDCMeta { meta_pa: kv_meta_pa };
        unsafe {
            memcpy(
                (reply_va as *mut i8).cast::<c_types::c_void>(),
                (&mut reply as *mut RegDCMeta).cast::<c_types::c_void>(),
                core::mem::size_of_val(&reply) as u64,
            );
        }
        64
    }

    #[inline]
    pub fn dereg_dc_meta_handler(req_va: u64, reply_va: u64) -> u32 {
        use crate::rpc::protocol::payload;

        let mut payload: payload::DeRegDCMeta = Default::default();

        unsafe {
            memcpy(
                (&mut payload as *mut payload::DeRegDCMeta).cast::<c_types::c_void>(),
                (req_va as *mut i8).cast::<c_types::c_void>(),
                core::mem::size_of_val(&payload) as u64,
            );
        }
        println!("[evict meta] gid:{:?}", payload.gid);
        evict_remote_dc_meta(&gid_to_str(payload.gid));
        let mut reply = 0 as u64;
        unsafe {
            memcpy(
                (reply_va as *mut i8).cast::<c_types::c_void>(),
                (&mut reply as *mut u64).cast::<c_types::c_void>(),
                core::mem::size_of_val(&reply) as u64,
            );
        }
        64
    }

    #[inline]
    pub fn query_dc_meta_handler(req_va: u64, reply_va: u64) -> u32 {
        use crate::rpc::protocol::payload;

        let mut payload: payload::QueryDCMeta = Default::default();

        unsafe {
            memcpy(
                (&mut payload as *mut payload::QueryDCMeta).cast::<c_types::c_void>(),
                (req_va as *mut i8).cast::<c_types::c_void>(),
                core::mem::size_of_val(&payload) as u64,
            );
        }
        let gid_str = gid_to_str(payload.gid);
        let point = get_remote_dc_meta(&gid_str);
        return if point.is_none() {
            println!("[query_dc_meta_handler] not found for gid:{:?}", payload.gid);
            0
        } else {
            use crate::rpc::protocol::reply;
            let mut reply: reply::QueryDCMeta = Default::default();
            reply.point = point.unwrap().self_clone();
            println!("query result {:?}", reply.point);
            let reply_len: u64 = core::mem::size_of_val(&reply) as u64;
            unsafe {
                memcpy(
                    (reply_va as *mut i8).cast::<c_types::c_void>(),
                    (&mut reply as *mut reply::QueryDCMeta).cast::<c_types::c_void>(),
                    reply_len,
                );
            }
            reply_len as u32
        };
    }


    #[inline]
    pub fn connect_rc_handler(req_va: u64, reply_va: u64) -> u32 {
        use crate::rpc::protocol::{payload, reply};

        let mut payload: payload::ConnectRC = Default::default();

        unsafe {
            memcpy(
                (&mut payload as *mut payload::ConnectRC).cast::<c_types::c_void>(),
                (req_va as *mut i8).cast::<c_types::c_void>(),
                core::mem::size_of_val(&payload) as u64,
            );
        }

        let ctrl = get_global_rctrl(0 as usize);
        let ctx = ctrl.get_context();
        let rc = RC::new_with_srq(
            Default::default(),
            ctx, null_mut(),
            null_mut(),
            ctrl.get_recv_cq(),
        );
        let qp = rc.unwrap();
        let ib_qp = qp.get_qp();
        let _ = rc_connector::bring_to_init(ib_qp);
        let _ = rc_connector::bring_init_to_rtr(ib_qp, payload.qpn, payload.lid, payload.gid);
        let _ = rc_connector::bring_rtr_to_rts(ib_qp);

        // send back reply
        let node = get_rpc_client(0);
        let new_key = ctrl.get_next_in_qp_key(0);
        let mut reply: reply::ConnectRC = ConnectRC {
            gid: node.get_end_point().gid,
            lid: node.get_end_point().lid,
            qpn: qp.get_qp_num(),
            qd: new_key as usize,
        };
        // reg
        ctrl.mutex.lock();
        ctrl.registered_rcs.insert(new_key as usize, qp);
        ctrl.mutex.unlock();
        let reply_len: u64 = core::mem::size_of_val(&reply) as u64;
        unsafe {
            memcpy(
                (reply_va as *mut i8).cast::<c_types::c_void>(),
                (&mut reply as *mut reply::ConnectRC).cast::<c_types::c_void>(),
                reply_len,
            );
        }
        reply_len as u32
    }


    #[inline]
    pub fn disconnect_rc_handler(req_va: u64, reply_va: u64) -> u32 {
        use crate::rpc::protocol::{payload, reply};

        let mut payload: payload::DisconnectRC = Default::default();

        unsafe {
            memcpy(
                (&mut payload as *mut payload::DisconnectRC).cast::<c_types::c_void>(),
                (req_va as *mut i8).cast::<c_types::c_void>(),
                core::mem::size_of_val(&payload) as u64,
            );
        }

        let ctrl = get_global_rctrl(0 as usize);
        // TODO: Duplication check
        ctrl.dereg_rc(payload.qd as usize);
        let mut reply = 0 as u32;
        let reply_len: u64 = core::mem::size_of_val(&reply) as u64;
        unsafe {
            memcpy(
                (reply_va as *mut i8).cast::<c_types::c_void>(),
                (&mut reply as *mut u32).cast::<c_types::c_void>(),
                reply_len,
            );
        }
        reply_len as u32
    }
}
