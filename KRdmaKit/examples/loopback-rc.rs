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

    // create an RCQP
    let client_port: u8 = 1;
    let mut builder = QueuePairBuilder::new(&ctx);
    builder
        .allow_remote_rw()
        .allow_remote_atomic()
        .set_port_num(client_port);

    let client_qp = builder.build_rc().expect("failed to create the client QP");

    // query the address info
    let port_attr = ctx
        .get_port_attr(client_port)
        .expect("Query port attr 1 at device 0 error");

    // check the gid
    let gid = ctx
        .query_gid(client_port, 0)
        .expect("Query the gid of port 1 at device 0 error");

    let qpn = client_qp.inner_qp_num();
    let connected_qp = client_qp
        .bring_up_rc(port_attr.lid as _, gid, qpn, 0)
        .expect("failed to bring up RC");

    println!("RC connection passes {:?}", connected_qp.status());

    // now sending a value using the connected QP
    let mr = MemoryRegion::new(ctx.clone(), 4096).expect("failed to allocate MR");

    let remote_addr = 1024;
    let test_buf = (mr.get_virt_addr() + remote_addr) as *mut [u8; 11];
    unsafe {
        (*test_buf).clone_from_slice("Hello world".as_bytes());
    };

    // sending the RDMA request
    let _ = connected_qp
        .post_send_read(
            &mr,
            0..11,
            true,
            mr.get_virt_addr() + remote_addr,
            mr.rkey().0,
        )
        .expect("Failed to post send read request");

    // sleep 1 second to ensure the RDMA is completed
    std::thread::sleep(std::time::Duration::from_millis(1000));

    let mut completions = [Default::default()];

    // pool the completion
    loop {
        let ret = connected_qp
            .poll_send_cq(&mut completions)
            .expect("failed to poll cq");
        if ret.len() > 0 {
            break;
        }
    }

    println!("sanity check ret {:?}", completions[0]);

    let test_msg = (mr.get_virt_addr()) as *mut [u8; 11];
    let test_msg =
        std::str::from_utf8(unsafe { &*test_msg }).expect("failed to decode received message");

    println!("check received message: {}", test_msg);
}
