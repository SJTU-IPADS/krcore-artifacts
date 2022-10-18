use std::net::SocketAddr;
use std::sync::Arc;
use std::sync::Mutex;
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
        let server_mr: Arc<Mutex<Option<MRMetadata>>> = Arc::new(Mutex::new(None));
        let addr: SocketAddr = "127.0.0.1:10001".parse().expect("Failed to resolve addr");
        let _handle = func::spawn_server_thread(addr, &server_mr);
        thread::sleep(Duration::from_millis(750));
        func::client_thread(addr, &server_mr);
        thread::sleep(Duration::from_millis(750));
    }
}

#[cfg(feature = "user")]
pub mod func {
    use crate::MRMetadata;
    use std::net::SocketAddr;
    use std::str::from_utf8;
    use std::sync::{Arc, Mutex};
    use std::thread::JoinHandle;
    use KRdmaKit::services_user::ConnectionManagerServer;
    use KRdmaKit::{MemoryRegion, QueuePairBuilder, QueuePairStatus, UDriver};

    pub fn spawn_server_thread(
        addr: SocketAddr,
        server_mr_metadata: &Arc<Mutex<Option<MRMetadata>>>,
    ) -> JoinHandle<std::io::Result<()>> {
        let ctx = UDriver::create()
            .expect("failed to query device")
            .devices()
            .into_iter()
            .next()
            .expect("no rdma device available")
            .open_context()
            .expect("failed to create RDMA context");
        let cm_server = ConnectionManagerServer::new(&ctx, 1);
        let handle = cm_server.spawn_listener(addr);
        let server_mr =
            Arc::new(MemoryRegion::new(ctx.clone(), 128).expect("Failed to allocate MR"));
        let mr_metadata = MRMetadata {
            mr: server_mr.clone(),
            raddr: unsafe { server_mr.get_rdma_addr() },
            rkey: server_mr.rkey().0,
        };
        let buf = server_mr.get_virt_addr() as *mut [u8; 11];
        unsafe { (*buf).clone_from_slice("Hello world".as_bytes()) };
        let _ = server_mr_metadata
            .lock()
            .expect("Failed to lock mtx")
            .insert(mr_metadata);
        return handle;
    }

    pub fn client_thread(addr: SocketAddr, server_mr_metadata: &Arc<Mutex<Option<MRMetadata>>>) {
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

        let mr_metadata = server_mr_metadata
            .lock()
            .expect("Failed to lock mtx")
            .take()
            .expect("Failed to read mr metadata");
        let client_mr = MemoryRegion::new(ctx.clone(), 128).expect("Failed to allocate MR");

        {
            println!("=================RDMA READ==================");
            let _ = qp.post_send_read(
                &client_mr,
                0..11,
                true,
                mr_metadata.raddr,
                mr_metadata.rkey,
                12345,
            );
            std::thread::sleep(std::time::Duration::from_millis(1000));
            let mut completions = [Default::default()];
            loop {
                let ret = qp
                    .poll_send_cq(&mut completions)
                    .expect("Failed to poll cq");
                if ret.len() > 0 {
                    break;
                }
            }
            println!(
                "sanity check ret {:?} wr_id {}",
                completions[0], completions[0].wr_id
            );
            let buf = (client_mr.get_virt_addr()) as *mut [u8; 11];
            let msg = from_utf8(unsafe { &*buf }).expect("failed to decode received message");
            println!("Message received : {}", msg);
        }
        {
            println!("=================RDMA WRITE=================");
            let buf = client_mr.get_virt_addr() as *mut [u8; 11];
            unsafe { (*buf).clone_from_slice("WORLD_HELLO".as_bytes()) };
            let _ = qp.post_send_write(
                &client_mr,
                0..11,
                true,
                mr_metadata.raddr + 32,
                mr_metadata.rkey,
                54321,
            );
            std::thread::sleep(std::time::Duration::from_millis(1000));
            let mut completions = [Default::default()];
            loop {
                let ret = qp
                    .poll_send_cq(&mut completions)
                    .expect("Failed to poll cq");
                if ret.len() > 0 {
                    break;
                }
            }
            println!(
                "sanity check ret {:?} wr_id {}",
                completions[0], completions[0].wr_id
            );
            let buf = (mr_metadata.mr.get_virt_addr() + 32) as *mut [u8; 11];
            let msg = from_utf8(unsafe { &*buf }).expect("failed to decode received message");
            println!("Message sent : {}", msg);
        }
    }
}
