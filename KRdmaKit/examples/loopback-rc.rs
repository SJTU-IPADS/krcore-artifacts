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

    let inner_qpn = client_qp.inner_qp_num();

    let connected_qp = client_qp.bring_up_rc(port_attr.lid as _, gid, inner_qpn, 0).expect("failed to bring up RC");

    println!("RC connection passes {:?}", connected_qp.status() );
}        