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
    // for the MR, its layout is:
    // |0    ... 1024 | // client-side send buffer
    // |1024 ... 2048 | // server-side receiver buffer

    let server_qp = {
        let builder = QueuePairBuilder::new(&ctx);
        let server_qp = builder
            .build_ud()
            .expect("failed to build UD QP")
            .bring_up_ud()
            .expect("failed to bring up UD QP");
        println!("check the QP status: {:?}", server_qp.status());

        server_qp
            .post_recv(&mr, 1024..2048, server_buf as u64)
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
    )
    .expect("UD endpoint creation fails");

    println!("sanity check UD endpoint: {:?}", endpoint);

    // main example body: create the client-sided QP
    let client_qp = {
        let builder = QueuePairBuilder::new(&ctx);
        let client_qp = builder
            .build_ud()
            .expect("failed to build UD QP")
            .bring_up_ud()
            .expect("failed to bring up UD QP");

        client_qp
    };

    // very unsafe to construct a send message
    let client_buffer = mr.get_virt_addr() as *mut [u8; 11];
    unsafe {
        (*client_buffer).clone_from_slice("Hello world".as_bytes());
    };

    // really send the message
    client_qp
        .post_datagram(&endpoint, &mr, 0..11, 73, true)
        .expect("failed to post message");

    // now try to receive
    // sleep 1 second to ensure the message has arrived
    std::thread::sleep(std::time::Duration::from_millis(1000));

    let mut completions = [Default::default()];

    let res = server_qp
        .poll_recv_cq(&mut completions)
        .expect("failed to poll recv CQ");
    assert!(res.len() > 0);

    let GRH_SZ = 40; // UD will send an additional header ahead of the message
    let received_message = (mr.get_virt_addr() + 1024 + GRH_SZ) as *mut [u8; 11];
    let received_message = std::str::from_utf8(unsafe { & *received_message })
        .expect("failed to decode received message");

    println!("check received message: {}", received_message);
}
