use std::net::SocketAddr;
use std::str::from_utf8;
use std::sync::Arc;
use std::thread::{sleep, spawn, yield_now, JoinHandle};
use std::time::Duration;
use KRdmaKit::memory_window::MWType;
use KRdmaKit::rdma_shim::bindings::ibv_inc_rkey;
use KRdmaKit::services_user::{ConnectionManagerServer, DefaultConnectionManagerHandler};
use KRdmaKit::{MemoryRegion, MemoryWindow, QueuePairBuilder, QueuePairStatus, UDriver};

// register RC => malloc and bind MW => unbind MW
// RC false, MW false => RC true, MW false => RC true, MW true => RC true, MW false
static mut RC: bool = false;
static mut MW: bool = false;

static mut ADDR: u64 = 0;
static mut RKEY: u32 = 0;

fn main() {
    let addr: SocketAddr = "127.0.0.1:10001".parse().expect("Failed to resolve addr");
    let running = Box::into_raw(Box::new(true));
    let (server, handle_1) = spawn_server_thread(addr, running);
    let running_addr = running as u64;
    let handle_2 = server_mw(server, running_addr);
    sleep(Duration::from_millis(300));
    client_ops(addr);
    sleep(Duration::from_millis(100));
    unsafe { *running = false };
    let _ = handle_1.join();
    let _ = handle_2.join();
    unsafe { Box::from_raw(running) };
}

pub fn server_mw(
    server: Arc<ConnectionManagerServer<DefaultConnectionManagerHandler>>,
    running_addr: u64,
) -> JoinHandle<()> {
    spawn(move || {
        let handler = server.handler();
        let ctx = handler.ctx();
        let mr = MemoryRegion::new(ctx.clone(), 4096).expect("Failed to allocate MR");
        let buf = mr.get_virt_addr() as *mut [u8; 11];
        unsafe { (*buf).clone_from_slice("Hello world".as_bytes()) };

        let mut completions = [Default::default(); 10];

        while !unsafe { RC } {
            // RC ok
            yield_now();
        }

        let qp = handler.exp_get_qps()[0].clone();
        println!("\n=================RDMA BIND MW===============\n");

        let mw = MemoryWindow::new(ctx.clone(), MWType::Type1).unwrap();
        qp.bind_mw(&mr, &mw, 0..2048, 30, true).unwrap();

        // let mw = MemoryWindow::new(ctx.clone(), MWType::Type2).unwrap();
        // let rkey = ibv_inc_rkey(mw.get_rkey());
        //
        // println!("{}", mw.get_rkey());
        //
        // qp.post_bind_mw(&mr, &mw, 0..2048, rkey, 30, true).unwrap();

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

        println!("{}", mw.get_rkey());
        unsafe { ADDR = mr.get_virt_addr() };
        unsafe { RKEY = mw.get_rkey() };
        unsafe { MW = true };

        while unsafe { *(running_addr as *const bool) } {
            // To enlarge mw's lifetime
            yield_now();
        }

        drop(mw);
        drop(mr);
        println!("\nDrop MW and MR");
    })
}

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

    let rkey = unsafe { RKEY };
    let addr = unsafe { ADDR };

    let mr = MemoryRegion::new(ctx.clone(), 4096).expect("Failed to allocate MR");

    {
        println!("\n=================RDMA READ==================\n");
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
        println!("\n=================RDMA WRITE=================\n");
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
        println!("\n=================RDMA READ==================\n");
        let _ = qp1.post_send_read(&mr, 0..11, true, addr + 32, rkey, 90);
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
}
