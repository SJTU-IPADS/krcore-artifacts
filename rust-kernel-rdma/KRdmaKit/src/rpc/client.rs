use alloc::sync::Arc;
use core::cmp::max;
use core::ptr::null_mut;
use hashbrown::HashMap;
use crate::cm::EndPoint;
use crate::consts::{UD_HEADER_SZ};
use crate::device::RContext;
use crate::mem::{Memory, RMemPhy, TempMR};
use crate::qp::{RecvHelper, UD, UDOp};
use crate::rust_kernel_rdma_base::*;
use crate::rust_kernel_rdma_base::rust_kernel_linux_util::timer::KTimer;
use data::{Header, IntValue, ReqType};
use linux_kernel_module::{c_types, println};

pub mod data {
    use crate::cm::EndPoint;

    #[derive(Copy, Clone)]
    pub enum ReqType {
        Dummy = 0,
        HandShake,
        Hello,
        DisAbleListen,      // not to listen
    }

    pub trait IntValue {
        fn value(&self) -> u8;
    }

    impl IntValue for ReqType {
        fn value(&self) -> u8 {
            *self as u8
        }
    }

    /// Request header
    #[repr(C)]
    pub struct Header {
        pub req_type: u8,
        pub rpc_id: usize,
        pub end_point: EndPoint,
    }

    impl Header {
        pub fn new<T: IntValue>(req_type: T, rpc_id: usize) -> Self {
            Self {
                req_type: req_type.value(),
                rpc_id,
                end_point: Default::default(),
            }
        }
    }

    impl Default for Header {
        fn default() -> Self {
            Self {
                req_type: 0,
                rpc_id: 0,
                end_point: Default::default(),
            }
        }
    }

    #[repr(C)]
    pub struct Reply {}
}

const BUF_SZ: usize = 512;

/// Return back the reply length in bytes
type RpcHandlerT = fn(req_addr: u64, reply_addr: u64) -> u32;


/// RPC Client based on UD
///
/// TODO: maybe we could extend to DC / RC ?
pub struct RPCClient<'a, const N: usize> {
    queue_pair: &'a Arc<UD>,
    ctx: &'a RContext<'a>,
    endpoint: EndPoint,

    send_buffer: RMemPhy,
    recv_buffer: RMemPhy,
    recv_helper: Arc<RecvHelper<N>>,

    reg_handlers: HashMap<u8, RpcHandlerT>,
    listened: bool,
}

impl<'a, const N: usize> RPCClient<'a, N> {
    /// Register the handler into RPCClient
    pub fn reg_handler(&mut self, id: u8, rpc_handler: RpcHandlerT) {
        self.reg_handlers.insert(id, rpc_handler);
    }

    #[inline]
    pub fn get_end_point(&self) -> &EndPoint {
        &self.endpoint
    }
}

impl<'a, const N: usize> RPCClient<'a, N> {
    /// Default dummy handler for test example
    fn dummy_handler(req_addr: u64, reply_addr: u64) -> u32 {
        println!("[RPC dummy_handler] req va:{:x}, reply va:{:x}", req_addr, reply_addr);
        use rust_kernel_linux_util::bindings::memcpy;
        let mut num = 1024 as u64;
        let _req: *mut u64 = req_addr as *mut u64;
        unsafe {
            memcpy(
                (reply_addr as *mut i8).cast::<c_types::c_void>(),
                (&mut num as *mut u64).cast::<c_types::c_void>(),
                core::mem::size_of_val(&num) as u64,
            );
        }
        64
    }
    pub fn create(ctx: &'a RContext, ud: &'a Arc<UD>, dct_num: u32, mr: &TempMR) -> Self {
        let total = 1024 * 4;
        let lkey = unsafe { ctx.get_lkey() };
        let mut recv_buffer = RMemPhy::new(total);
        let base_addr = recv_buffer.get_dma_buf();

        let endpoint = EndPoint {
            qpn: ud.get_qp_num(),
            qkey: ud.get_qkey(),
            lid: ctx.get_lid(),
            gid: ctx.get_gid(),
            dct_num,
            mr: *mr,
            ah: null_mut(),
        };

        let mut res = Self {
            queue_pair: ud,
            ctx,
            send_buffer: RMemPhy::new(1024 * 4),
            recv_buffer,
            recv_helper: RecvHelper::create(BUF_SZ, lkey, base_addr),
            endpoint,
            reg_handlers: Default::default(),
            listened: true,
        };

        let recv = unsafe { Arc::get_mut_unchecked(&mut res.recv_helper) };
        let _ = recv.post_recvs(ud.get_qp(), null_mut(), N as usize);

        res.reg_handler(ReqType::Dummy.value(), RPCClient::<N>::dummy_handler);
        res
    }

    /// Call in async.
    /// Param endpoint: Remote routing info. Insert into RPC header
    /// Param header: RPC header
    /// Param payload: RPC payload data with type `Req`
    /// Param timeout_us: Timeout of the RPC calling (in us)
    pub async fn call<Req: Sized, Reply: Sized + Default>(
        &mut self,
        endpoint: &EndPoint,
        header: &mut data::Header,
        payload: &mut Req,
        timeout_us: u64,
    ) -> Option<Reply> {
        use rust_kernel_linux_util::bindings::memcpy;
        let data_p = (payload as *mut Req).cast::<c_types::c_void>();
        let header_p = (header as *mut data::Header).cast::<c_types::c_void>();
        let header_len = core::mem::size_of::<data::Header>() as usize;
        let data_len = core::mem::size_of::<Req>() as usize;
        let reply_len = core::mem::size_of::<Reply>() as usize;

        let send_va = self.send_buffer.get_ptr();
        let _recv_va = self.recv_buffer.get_ptr();
        let send_addr = self.send_buffer.get_dma_buf();
        let _recv_addr = self.recv_buffer.get_dma_buf();
        let lkey = unsafe { self.ctx.get_lkey() };
        let mut op = UDOp::new(self.queue_pair);

        // assemble header
        header.end_point = EndPoint {
            qpn: self.endpoint.qpn,
            qkey: self.endpoint.qkey,
            lid: self.endpoint.lid,
            gid: self.endpoint.gid,
            dct_num: self.endpoint.dct_num,
            mr: self.endpoint.mr,
            ah: null_mut(),
        };

        unsafe {
            // header
            memcpy((send_va as *mut i8).cast::<c_types::c_void>(), header_p, header_len as u64);
            // payload
            memcpy(((send_va as u64 + header_len as u64) as *mut i8).cast::<c_types::c_void>(),
                   data_p,
                   data_len as u64);
        }
        let total_send_len = max(64, header_len + data_len);
        let _ = op.send(send_addr, lkey, endpoint, total_send_len);

        let now = KTimer::new();
        let reply = loop {
            let wc_header = unsafe { Arc::get_mut_unchecked(&mut self.recv_helper) }.get_wc_header();
            let res = op.pop_recv_cq(1, wc_header);
            if res.is_none() {
                // yield to another task
                futures_micro::yield_once().await;
                let passed_us = now.get_passed_usec() as u64;
                if passed_us >= timeout_us {
                    println!("Call timeout. Waiting for {} us", passed_us);
                    break None;
                }
            } else {
                let wc = unsafe { *wc_header };
                let va = wc.get_wr_id() as u64 + UD_HEADER_SZ as u64;
                self.post_recv(1).await;

                let mut reply: Reply = Default::default();
                // copy reply to the return value
                unsafe {
                    memcpy(
                        (&mut reply as *mut Reply).cast::<c_types::c_void>(),
                        (va as *mut i8).cast::<c_types::c_void>(),
                        reply_len as u64,
                    );
                }
                break Some(reply);
            }
        };

        // finish: return back
        return reply;
    }
}

impl<'a, const N: usize> RPCClient<'a, N> {
    async fn post_recv(&mut self, post_cnt: u64) {
        let recv = unsafe { Arc::get_mut_unchecked(&mut self.recv_helper) };
        let _ = recv.post_recvs(self.queue_pair.get_qp(), null_mut(), post_cnt as usize);
    }

    /// Used in RPC server-side
    /// Busy-polling the rpc buffer
    pub async fn poll_all(&mut self) {
        use rust_kernel_linux_util::bindings::{memcpy};

        let send_addr = self.send_buffer.get_dma_buf();
        let send_va = self.send_buffer.get_ptr();

        let lkey = unsafe { self.ctx.get_lkey() };
        let mut op = UDOp::new(self.queue_pair);
        let wc_header = unsafe { Arc::get_mut_unchecked(&mut self.recv_helper) }.get_wc_header();

        let timer = KTimer::new();
        loop {
            let pop_res = op.pop_recv_cq(N as u32, wc_header);
            if pop_res.is_none() {
                futures_micro::yield_once().await;

                let mut exit = !self.listened;
                // If with rpc_server feature enabled, busy pulling until rmmod the kernel module.
                // Otherwise, polling duration is 5ms.
                #[cfg(not(feature = "rpc_server"))]
                {
                    exit = exit || timer.get_passed_usec() >= 5 * 1000;
                }
                if exit {
                    if !self.listened {
                        println!("Gracefully exit polling, Bye :)");
                    }
                    break;
                }
            } else {
                let pop_cnt = pop_res.unwrap();
                let wc_base_addr = wc_header as u64;
                let wc_sz = core::mem::size_of::<ib_wc>() as u64;
                self.post_recv(pop_cnt as u64).await;
                for i in 0..pop_cnt {
                    let wc_p = (wc_base_addr + wc_sz * i as u64) as *mut ib_wc;
                    let wc = unsafe { *(wc_p) };

                    // start address of Header
                    let va = wc.get_wr_id() as u64 + UD_HEADER_SZ as u64;
                    // post back
                    let va_ptr = va as *mut i8;
                    let mut header: Header = Default::default();
                    let header_len = core::mem::size_of_val(&header) as usize;
                    let payload_va = header_len as u64 + va;
                    unsafe {
                        memcpy((&mut header as *mut Header).cast::<c_types::c_void>(),
                               va_ptr.cast::<c_types::c_void>(),
                               header_len as u64,
                        );
                    };
                    // println!("wc[{}] va={:x}, rpc_id:{}, header len:{}", i, va, header.rpc_id, header_len);
                    let req_type: u8 = header.req_type;
                    let len = match self.reg_handlers.get(&req_type) {
                        Some(handler) => {
                            // handle
                            max(
                                64, async {
                                    handler(payload_va, send_va as u64) as u32
                                }.await,
                            )
                        }
                        None => {
                            64 as u32
                        }
                    };
                    let mut remote_point = header.end_point;
                    // now send back
                    remote_point.bind_pd(self.ctx.get_pd());
                    let _ = op.send(send_addr, lkey, &remote_point, len as usize);
                }
            }
        };
    }
}

impl<'a, const N: usize> Drop for RPCClient<'a, N> {
    fn drop(&mut self) {
        self.listened = false;
    }
}
