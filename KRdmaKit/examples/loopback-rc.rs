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

}        