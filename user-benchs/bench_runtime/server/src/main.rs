use std::net::SocketAddr;
use KRdmaKit::services_user::{ConnectionManagerServer, DefaultConnectionManagerHandler};
use KRdmaKit::{MemoryRegion, UDriver};

fn main() {
    let addr: SocketAddr = "127.0.0.1:10001".parse().expect("Failed to resolve addr");
    spawn_server_thread(addr);
}

pub fn spawn_server_thread(addr: SocketAddr) {
    let ctx = UDriver::create()
        .expect("failed to query device")
        .devices()
        .into_iter()
        .next()
        .expect("no rdma device available")
        .open_context()
        .expect("failed to create RDMA context");
    let mut handler = DefaultConnectionManagerHandler::new(&ctx, 1);
    let server_mr_1 = MemoryRegion::new(ctx.clone(), 1024 * 1024).expect("Failed to allocate MR");
    handler.register_mr(vec![("MR1".to_string(), server_mr_1)]);
    let server = ConnectionManagerServer::new(handler);

    let server1 = server.clone();
    ctrlc::set_handler(move || {
        server1.stop_listening();
        println!("Exit");
    })
    .expect("Error setting Ctrl-C handler");

    let _ = server.blocking_listener(addr);
}
