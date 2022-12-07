use std::net::SocketAddr;
use std::thread::JoinHandle;
use KRdmaKit::services_user::{ConnectionManagerServer, DefaultConnectionManagerHandler};
use KRdmaKit::{MemoryRegion, UDriver};

fn main() {
    let addr: SocketAddr = "127.0.0.1:10001".parse().expect("Failed to resolve addr");
    let _ = spawn_server_thread(addr).join();
}

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
    let server_mr_1 = MemoryRegion::new(ctx.clone(), 1024 * 1024).expect("Failed to allocate MR");
    handler
        .register_mr(vec![("MR1".to_string(), server_mr_1)])
        .expect("Failed to register MR");
    let server = ConnectionManagerServer::new(handler);
    server.spawn_listener(addr)
}
