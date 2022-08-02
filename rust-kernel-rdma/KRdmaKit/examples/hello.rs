extern crate KRdmaKit;

use KRdmaKit::*;

fn main() {
    let d = UDriver::create().unwrap();
    println!("Num RDMA devices found: {}", d.iter().len());
}