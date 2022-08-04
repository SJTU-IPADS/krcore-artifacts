#[allow(non_snake_case)]
extern crate KRdmaKit;

use KRdmaKit::*;

fn main() {
    let ctx = UDriver::create()
        .expect("failed to query device")
        .devices()
        .into_iter()
        .next()
        .expect("no rdma device available")
        .open_context()
        .expect("failed to create RDMA context");

    println!("create context done: {:?}", ctx);

    // check the device attribute
    let dev_attr = ctx.get_device_attr().expect("Query device attr error");
    println!("check dev attr: {:?}", dev_attr);

    // check the port attribute at port 1
    // by default, the port number starts at 1, and there should be at least one valid port
    let port_attr = ctx
        .get_port_attr(1)
        .expect("Query port attr 1 at device 0 error");
    println!("check port attr 1: {:?}", port_attr);

    // check the gid
    let gid = ctx
        .query_gid(1, 0)
        .expect("Query the gid of port 1 at device 0 error");
    println!("check gid at port 1 at device 0: {:?}", gid);

    // check the address handler
    let address_handler = ctx
        .create_address_handler(1, 0, port_attr.lid as _, gid)
        .expect("Failed to create address handler");
    println!("check the address handler {:?}", address_handler);

    // check we can create the cq
    let cq = CompletionQueue::create(&ctx, 128).expect("fail to create cq");
    println!("check CQ creation {:?}", cq);

    // create a sample MR
    let mr = MemoryRegion::new(ctx.clone(), 4096).expect("failed to allocate MR");
    println!("check MR rkey and lkey: {:?} {:?}", mr.rkey(), mr.lkey());
    let server_buf = mr.get_virt_addr() as *mut i8;

    // main example body: create the server-side QP
    let server_qp = {
        let builder = QueuePairBuilder::new(&ctx);
        let server_qp = builder
            .build_ud()
            .expect("failed to build UD QP")
            .bring_up_ud()
            .expect("failed to bring up UD QP");
        println!("check the QP status: {:?}", server_qp.status());

        server_qp
            .post_recv(&mr, 0..1024, server_buf as u64)
            .expect("failed to register recv buffer");

        server_qp
    };

    // main example body: create the client-side endpoint
    // 1: the local port number
    let endpoint = DatagramEndpoint::new(
        &ctx,
        1,
        server_qp.lid().unwrap(),
        server_qp.gid().unwrap(),
        server_qp.qp_num(),
        server_qp.qkey(),
    );

    println!("sanity check UD endpoint: {:?}",endpoint); 

    unimplemented!();
}
