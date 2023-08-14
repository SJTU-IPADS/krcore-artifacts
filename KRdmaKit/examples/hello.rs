#[allow(non_snake_case)]
extern crate KRdmaKit;
use KRdmaKit::*;

fn main() {
    println!(
        "Num RDMA devices found: {}",
        UDriver::create().unwrap().devices().len()
    );
}
