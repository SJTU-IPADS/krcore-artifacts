use std::net::SocketAddr;
use std::sync::Arc;
use std::thread;
use std::thread::yield_now;
use std::time::Duration;
use KRdmaKit::memory_window::MemoryWindow;
use KRdmaKit::MemoryRegion;

// register RC => malloc and bind MW => unbind MW
// RC false, MW false => RC true, MW false => RC true, MW true => RC true, MW false
static mut RC: bool = false;
static mut MW: bool = false;

static mut ADDR: u64 = 0;

pub struct MRMetadata {
    pub mr: Arc<MemoryRegion>,
    pub raddr: u64,
    pub rkey: u32,
}

fn main() {
    let addr: SocketAddr = "127.0.0.1:10001".parse().expect("Failed to resolve addr");
    let running = Box::into_raw(Box::new(true));
    let (server, handle) = func::spawn_server_thread(addr, running);
    let running_addr = running as u64;
    thread::spawn(move || {
        let handler = server.handler();
        let mr = MemoryRegion::new(ctx.clone(), 4096).expect("Failed to allocate MR");
        let buf = mr.get_virt_addr() as *mut [u8; 11];
        unsafe { (*buf).clone_from_slice("Hello world".as_bytes()) };
        let mw = MemoryWindow::new(ctx.clone()).unwrap();

        let mut completions = [Default::default(); 10];

        while !unsafe { RC } {
            // RC ok
            yield_now();
        }

        let qp = handler.exp_get_qps()[0].clone();
        qp.post_bind_mw(&mr, &mw, 0..2048, 42, 1, true).unwrap();
        loop {
            let ret = qp
                .poll_send_cq(&mut completions)
                .expect("Failed to poll cq");
            if ret.len() > 0 {
                println!("post_bind_mw polled WC {}", ret[0].wr_id);
                break;
            }
        }
        unsafe { ADDR = mr.get_virt_addr() };
        unsafe { MW = true };

        while unsafe { *(running_addr as *const bool) } {
            // To enlarge mw's lifetime
            yield_now();
        }

        drop(mw);
        drop(mr);
    });

    thread::sleep(Duration::from_millis(300));
    func::client_ops(addr);
    thread::sleep(Duration::from_millis(100));
    unsafe { *running = false };
    let _ = handle.join();
    println!("\nServer Exit!!");
    unsafe { Box::from_raw(running) };
}

pub mod func {
    use crate::{ADDR, MW, RC};
    use std::net::SocketAddr;
    use std::str::from_utf8;
    use std::sync::Arc;
    use std::thread::{yield_now, JoinHandle};
    use KRdmaKit::memory_window::MemoryWindow;
    use KRdmaKit::services_user::{ConnectionManagerServer, DefaultConnectionManagerHandler};
    use KRdmaKit::{MemoryRegion, QueuePairBuilder, QueuePairStatus, UDriver};

    pub fn spawn_server_thread(
        addr: SocketAddr,
        running: *mut bool,
    ) -> (
        Arc<ConnectionManagerServer<DefaultConnectionManagerHandler>>,
        JoinHandle<std::io::Result<()>>,
    ) {
        let ctx = UDriver::create()
            .expect("failed to query device")
            .devices()
            .into_iter()
            .next()
            .expect("no rdma device available")
            .open_context()
            .expect("failed to create RDMA context");
        let server = ConnectionManagerServer::new(DefaultConnectionManagerHandler::new(&ctx, 1));
        let handle = server.spawn_listener(addr, running);
        return (server, handle);
    }

    pub fn client_ops(addr: SocketAddr) {
        let client_port: u8 = 1;
        let ctx = UDriver::create()
            .expect("failed to query device")
            .devices()
            .into_iter()
            .next()
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
            _ => eprintln!("Error : Bring up failed"),
        }

        unsafe { RC = true };
        while !unsafe { MW } {
            yield_now();
        }

        let rkey = 1;
        let addr = unsafe { ADDR };

        let mr = MemoryRegion::new(ctx.clone(), 4096).expect("Failed to allocate MR");

        {
            println!("\n=================RDMA READ==================\n");
            let _ = qp1.post_send_read(&mr, 0..11, true, addr, rkey, 12345);
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
                "post_send_read wr_id {}, msg [{}]",
                completions[0].wr_id, msg
            );
        }
        {
            println!("\n=================RDMA WRITE=================\n");
            let buf = mr.get_virt_addr() as *mut [u8; 11];
            unsafe { (*buf).clone_from_slice("WORLD_HELLO".as_bytes()) };
            let _ = qp1.post_send_write(&mr, 0..11, true, addr + 32, rkey, 54321);
            let mut completions = [Default::default()];
            loop {
                let ret = qp1
                    .poll_send_cq(&mut completions)
                    .expect("Failed to poll cq");
                if ret.len() > 0 {
                    break;
                }
            }
            println!("post_send_write wr_id {}", completions[0].wr_id);
        }
        {
            println!("\n=================RDMA READ==================\n");
            let _ = qp1.post_send_read(&mr, 0..11, true, addr + 32, rkey, 12345);
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
                "post_send_read wr_id {}, msg [{}]",
                completions[0].wr_id, msg
            );
        }
    }
}
