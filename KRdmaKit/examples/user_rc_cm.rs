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
        let _handle = func::spawn_server_thread(addr);
        thread::sleep(Duration::from_millis(500));
        func::client_thread(addr);
        thread::sleep(Duration::from_millis(500));
    }
}

#[cfg(feature = "user")]
pub mod func {
    use std::net::SocketAddr;
    use std::str::from_utf8;
    use std::thread::JoinHandle;
    use KRdmaKit::services_user::{ConnectionManagerServer, DefaultConnectionManagerHandler};
    use KRdmaKit::{MemoryRegion, QueuePairBuilder, QueuePairStatus, UDriver};

    pub fn spawn_server_thread(addr: SocketAddr) -> JoinHandle<std::io::Result<()>> {
        let ctx = UDriver::create()
            .expect("failed to query device")
            .devices()
            .into_iter()
            .next()
            .expect("no rdma device available")
            .open_context()
            .expect("failed to create RDMA context");
        let handler = DefaultConnectionManagerHandler::new(&ctx, 1);
        let server_mr_1 = MemoryRegion::new(ctx.clone(), 128).expect("Failed to allocate MR");
        let server_mr_2 = MemoryRegion::new(ctx.clone(), 128).expect("Failed to allocate MR");
        let buf = server_mr_2.get_virt_addr() as *mut [u8; 11];
        handler
            .register_mr(vec![
                ("MR1".to_string(), server_mr_1),
                ("MR2".to_string(), server_mr_2),
            ])
            .expect("Failed to register MR");
        let server = ConnectionManagerServer::new(handler);
        unsafe { (*buf).clone_from_slice("Hello world".as_bytes()) };
        let handle = server.spawn_listener(addr);
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
        let mr_metadata = mr_infos.inner().get("MR2").expect("Unregistered MR");

        let client_mr = MemoryRegion::new(ctx.clone(), 128).expect("Failed to allocate MR");

        {
            println!("\n=================RDMA READ==================\n");
            let _ = qp.post_send_read(
                &client_mr,
                0..11,
                true,
                mr_metadata.addr,
                mr_metadata.rkey,
                12345,
            );
            std::thread::sleep(std::time::Duration::from_millis(500));
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
            println!("\n=================RDMA WRITE=================\n");
            let buf = client_mr.get_virt_addr() as *mut [u8; 11];
            unsafe { (*buf).clone_from_slice("WORLD_HELLO".as_bytes()) };
            let _ = qp.post_send_write(
                &client_mr,
                0..11,
                true,
                mr_metadata.addr + 32,
                mr_metadata.rkey,
                54321,
            );
            std::thread::sleep(std::time::Duration::from_millis(500));
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
            let buf = (mr_metadata.addr + 32) as *mut [u8; 11];
            let msg = from_utf8(unsafe { &*buf }).expect("failed to decode received message");
            println!("Message sent : {}", msg);
        }
        {
            println!("\n=================RDMA ATOMIC================\n");
            let remote = unsafe { (mr_metadata.addr as *mut u64).as_mut().unwrap() };
            *remote = 10000;
            let local = unsafe { (client_mr.get_virt_addr() as *mut u64).as_mut().unwrap() };
            *local = 0;
            println!("Local val {} Remote val {}", *local, *remote);
            let val = 100;
            let _ = qp.post_send_faa(
                &client_mr,
                0,
                true,
                mr_metadata.addr,
                mr_metadata.rkey,
                1111,
                val,
            );
            println!("FAA {}", val);
            std::thread::sleep(std::time::Duration::from_millis(500));
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
            println!("Local val {} Remote val {}", *local, *remote);

            let _ = qp.post_send_cas(
                &client_mr,
                0,
                true,
                mr_metadata.addr,
                mr_metadata.rkey,
                2222,
                *local + val,
                0,
            );
            println!("CAS {} -> {}", *local + val, 0);
            std::thread::sleep(std::time::Duration::from_millis(500));
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
            println!("Local val {} Remote val {}", *local, *remote);
            let val = 6789;
            let _ = qp.post_send_cas(
                &client_mr,
                0,
                true,
                mr_metadata.addr,
                mr_metadata.rkey,
                3333,
                0,
                val,
            );
            println!("CAS {} -> {}", 0, val);
            std::thread::sleep(std::time::Duration::from_millis(500));
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
            println!("Local val {} Remote val {}", *local, *remote);
        }
    }
}
