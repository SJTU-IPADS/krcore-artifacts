#[cfg(not(feature = "user"))]
compile_error!("This example must run with feature `user` on");

use std::net::SocketAddr;
use std::str::from_utf8;
use std::sync::Arc;
use std::thread::{sleep, spawn, yield_now, JoinHandle};
use std::time::Duration;
use KRdmaKit::context::Context;
use KRdmaKit::memory_window::MWType;
use KRdmaKit::services_user::{
    CMMessage, ConnectionManagerHandler, ConnectionManagerServer, DefaultConnectionManagerHandler,
};
use KRdmaKit::{
    CMError, MemoryRegion, MemoryWindow, QueuePair, QueuePairBuilder, QueuePairStatus, UDriver,
};

// register RC => malloc and bind MW => unbind MW
// RC false, MW false => RC true, MW false => RC true, MW true => RC true, MW false
static mut RC: bool = false;
static mut MW: bool = false;

static mut ADDR: u64 = 0;
static mut RKEY: u32 = 0;

fn main() {
    let addr: SocketAddr = "127.0.0.1:10001".parse().expect("Failed to resolve addr");
    let running = Box::into_raw(Box::new(true));
    let (server, handle_1) = spawn_server_thread(addr);
    let running_addr = running as u64;
    let handle_2 = server_mw(server, running_addr);
    sleep(Duration::from_millis(300));
    client_ops(addr);
    sleep(Duration::from_millis(100));
    unsafe { *running = false };
    let _ = handle_1.join();
    let _ = handle_2.join();
    let _ = unsafe { Box::from_raw(running) };
}

pub fn server_mw(
    server: Arc<ConnectionManagerServer<UserDefinedHandler>>,
    running_addr: u64,
) -> JoinHandle<()> {
    spawn(move || {
        let handler = server.handler();
        let ctx = handler.ctx();
        let mr =
            MemoryRegion::new(ctx.clone(), 1024 * 1024 * 1024 * 2).expect("Failed to allocate MR");
        let buf = mr.get_virt_addr() as *mut [u8; 11];
        unsafe { (*buf).clone_from_slice("Hello world".as_bytes()) };

        let mut completions = [Default::default(); 10];

        while !unsafe { RC } {
            // RC ok
            yield_now();
        }

        let qp = handler.exp_get_qps()[0].clone();

        println!("=================BIND MW====================");
        let mw = MemoryWindow::new(ctx.clone(), MWType::Type1).unwrap();
        qp.bind_mw(&mr, &mw, 0..(mr.capacity() as _), 30, true)
            .unwrap();

        loop {
            let ret = qp
                .poll_send_cq(&mut completions)
                .expect("Failed to poll cq");
            if ret.len() > 0 {
                println!(
                    "bind_mw    \twr_id {}, status {}",
                    ret[0].wr_id, ret[0].status
                );
                break;
            }
        }

        unsafe { ADDR = mr.get_virt_addr() };
        unsafe { RKEY = mw.get_rkey() };
        unsafe { MW = true };

        while unsafe { *(running_addr as *const bool) } {
            // To enlarge mw's lifetime
            yield_now();
        }

        server.stop_listening();
        drop(mw);
        drop(mr);
    })
}

pub struct UserDefinedHandler {
    inner: DefaultConnectionManagerHandler,
}

impl ConnectionManagerHandler for UserDefinedHandler {
    fn handle_reg_rc_req(&self, raw: String) -> Result<CMMessage, CMError> {
        self.inner.handle_reg_rc_req(raw)
    }

    fn handle_dereg_rc_req(&self, raw: String) -> Result<CMMessage, CMError> {
        self.inner.handle_dereg_rc_req(raw)
    }

    fn handle_query_mr_req(&self, raw: String) -> Result<CMMessage, CMError> {
        self.inner.handle_query_mr_req(raw)
    }
}

impl UserDefinedHandler {
    pub fn ctx(&self) -> &Arc<Context> {
        self.inner.ctx()
    }

    pub fn exp_get_qps(&self) -> Vec<Arc<QueuePair>> {
        self.inner.exp_get_qps()
    }
}

pub fn spawn_server_thread(
    addr: SocketAddr,
) -> (
    Arc<ConnectionManagerServer<UserDefinedHandler>>,
    JoinHandle<std::io::Result<()>>,
) {
    let ctx = UDriver::create()
        .expect("failed to query device")
        .devices()
        .get(1)
        .expect("no rdma device available")
        .open_context()
        .expect("failed to create RDMA context");
    let server = ConnectionManagerServer::new(UserDefinedHandler {
        inner: DefaultConnectionManagerHandler::new(&ctx, 1),
    });
    let handle = server.spawn_listener(addr);
    return (server, handle);
}

pub fn client_ops(addr: SocketAddr) {
    let client_port: u8 = 1;
    let ctx = UDriver::create()
        .expect("failed to query device")
        .devices()
        .get(1)
        .expect("no rdma device available")
        .open_context()
        .expect("failed to create RDMA context");
    let mut builder = QueuePairBuilder::new(&ctx);
    builder
        .allow_remote_rw()
        .allow_remote_atomic()
        .set_port_num(client_port);
    let qp1 = builder
        .build_rc()
        .expect("failed to create the client QP")
        .handshake(addr)
        .expect("Handshake failed!");
    match qp1.status().expect("Query status failed!") {
        QueuePairStatus::ReadyToSend => println!("Bring up succeeded"),
        _ => panic!("Error : Bring up failed"),
    }

    let mut builder = QueuePairBuilder::new(&ctx);
    builder
        .allow_remote_rw()
        .allow_remote_atomic()
        .set_port_num(client_port);
    let qp2 = builder
        .build_rc()
        .expect("failed to create the client QP")
        .handshake(addr)
        .expect("Handshake failed!");
    match qp2.status().expect("Query status failed!") {
        QueuePairStatus::ReadyToSend => println!("Bring up succeeded"),
        _ => panic!("Error : Bring up failed"),
    }

    unsafe { RC = true };
    while !unsafe { MW } {
        yield_now();
    }

    let rkey = unsafe { RKEY };
    let addr = unsafe { ADDR };

    let mr = MemoryRegion::new(ctx.clone(), 128).expect("Failed to allocate MR");

    {
        println!("=================RDMA READ==================");
        let _ = qp1.post_send_read(&mr, 0..11, true, addr, rkey, 42);
        let mut completions = [Default::default()];
        loop {
            let ret = qp1
                .poll_send_cq(&mut completions)
                .expect("Failed to poll cq");
            if ret.len() > 0 {
                break;
            }
        }

        let buf = (mr.get_virt_addr()) as *mut [u8; 11];
        let msg = from_utf8(unsafe { &*buf }).expect("failed to decode received message");
        println!(
            "post_send_read\twr_id {}, status {}, msg [{}]",
            completions[0].wr_id, completions[0].status, msg
        );
        unsafe { (*buf).clone_from_slice(&[0; 11]) };
    }
    {
        println!("=================RDMA READ==================");
        let _ = qp2.post_send_read(&mr, 0..11, true, addr, rkey, 24);
        let mut completions = [Default::default()];
        loop {
            let ret = qp2
                .poll_send_cq(&mut completions)
                .expect("Failed to poll cq");
            if ret.len() > 0 {
                break;
            }
        }

        let buf = (mr.get_virt_addr()) as *mut [u8; 11];
        let msg = from_utf8(unsafe { &*buf }).expect("failed to decode received message");
        println!(
            "post_send_read\twr_id {}, status {}, msg [{}]",
            completions[0].wr_id, completions[0].status, msg
        );
        unsafe { (*buf).clone_from_slice(&[0; 11]) };
    }
    {
        println!("=================RDMA WRITE=================");
        let buf = mr.get_virt_addr() as *mut [u8; 11];
        unsafe { (*buf).clone_from_slice("WORLD_HELLO".as_bytes()) };
        let _ = qp1.post_send_write(&mr, 0..11, true, addr + 32, rkey, 88);
        let mut completions = [Default::default()];
        loop {
            let ret = qp1
                .poll_send_cq(&mut completions)
                .expect("Failed to poll cq");
            if ret.len() > 0 {
                break;
            }
        }
        println!(
            "post_send_write\twr_id {}, status {}",
            completions[0].wr_id, completions[0].status
        );
        unsafe { (*buf).clone_from_slice(&[0; 11]) };
    }
    {
        println!("=================RDMA READ==================");
        let _ = qp2.post_send_read(&mr, 0..11, true, addr + 32, rkey, 90);
        let mut completions = [Default::default()];
        loop {
            let ret = qp2
                .poll_send_cq(&mut completions)
                .expect("Failed to poll cq");
            if ret.len() > 0 {
                break;
            }
        }
        let buf = (mr.get_virt_addr()) as *mut [u8; 11];
        let msg = from_utf8(unsafe { &*buf }).expect("failed to decode received message");
        println!(
            "post_send_read\twr_id {}, status {}, msg [{}]",
            completions[0].wr_id, completions[0].status, msg
        );
        unsafe { (*buf).clone_from_slice(&[0; 11]) };
    }
}
