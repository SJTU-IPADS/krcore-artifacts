use nix::libc::truncate;
use std::net::SocketAddr;
use std::sync::Arc;
use std::thread;
use std::time::Duration;
use KRdmaKit::MemoryRegion;

pub struct MRMetadata {
    pub mr: Arc<MemoryRegion>,
    pub raddr: u64,
    pub rkey: u32,
}

fn main() {
    #[cfg(not(feature = "user"))]
    println!("This example must run with feature `user` on");

    #[cfg(feature = "user")]
    {
        let addr: SocketAddr = "127.0.0.1:10001".parse().expect("Failed to resolve addr");
        let running = Box::into_raw(Box::new(true));
        let handle = func::spawn_server_thread(addr, running);
        thread::sleep(Duration::from_millis(300));
        func::client_thread(addr);
        thread::sleep(Duration::from_millis(100));
        unsafe { *running = false };
        let _ = handle.join();
        println!("\nServer Exit!!");
        unsafe { Box::from_raw(running) };
    }
}

#[cfg(feature = "user")]
pub mod func {
    use std::net::SocketAddr;
    use std::sync::Arc;
    use std::thread::JoinHandle;
    use std::time::SystemTime;
    use KRdmaKit::services_user::{ConnectionManagerServer, DefaultConnectionManagerHandler};
    use KRdmaKit::{MemoryRegion, QueuePairBuilder, QueuePairStatus, UDriver};

    pub fn spawn_server_thread(
        addr: SocketAddr,
        running: *mut bool,
    ) -> JoinHandle<std::io::Result<()>> {
        let ctx = UDriver::create()
            .expect("failed to query device")
            .devices()
            .into_iter()
            .next()
            .expect("no rdma device available")
            .open_context()
            .expect("failed to create RDMA context");
        let mut handler = DefaultConnectionManagerHandler::new(&ctx, 1);
        let server_mr_1 = MemoryRegion::new(ctx.clone(), 1024).expect("Failed to allocate MR");
        handler.register_mr(vec![("MR1".to_string(), server_mr_1)]);
        let server = ConnectionManagerServer::new(handler);
        let handle = server.spawn_listener(addr, running);
        let handler = server.handler();
        return handle;
    }

    pub fn client_thread(addr: SocketAddr) {
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
        let qp = builder.build_rc().expect("failed to create the client QP");
        let qp = qp.handshake(addr).expect("Handshake failed!");
        let a = qp.status().expect("Query status failed!");
        match a {
            QueuePairStatus::ReadyToSend => println!("Bring up succeeded"),
            _ => eprintln!("Error : Bring up failed"),
        }

        let mr_infos = qp.query_mr_info().expect("Failed to query MR info");
        println!("{:?}", mr_infos);
        let mr_metadata = mr_infos.inner().get("MR1").expect("Unregistered MR");

        let client_mr = MemoryRegion::new(ctx.clone(), 1024).expect("Failed to allocate MR");

        {
            println!("\n=================RDMA READ==================\n");

            for _ in 0..1000 {
                let _ = qp.post_send_read(
                    &client_mr,
                    0..512,
                    true,
                    mr_metadata.addr,
                    mr_metadata.rkey,
                    12345,
                );
                let mut completions = [Default::default()];
                loop {
                    let ret = qp
                        .poll_send_cq(&mut completions)
                        .expect("Failed to poll cq");
                    if ret.len() > 0 {
                        break;
                    }
                }
            }

            let mut vec = Vec::new();
            for _ in 0..1000 {
                let start = SystemTime::now();
                let _ = qp.post_send_read(
                    &client_mr,
                    0..512,
                    true,
                    mr_metadata.addr,
                    mr_metadata.rkey,
                    12345,
                );
                let mut completions = [Default::default()];
                loop {
                    let ret = qp
                        .poll_send_cq(&mut completions)
                        .expect("Failed to poll cq");
                    if ret.len() > 0 {
                        break;
                    }
                }
                let duration = SystemTime::now().duration_since(start).unwrap();
                vec.push(duration.as_micros());
            }
            let count = vec.len();
            let sum = vec.iter().sum::<u128>();

            println!("duration average : {} us", (sum as f64) / (count as f64));
        }
    }
}
