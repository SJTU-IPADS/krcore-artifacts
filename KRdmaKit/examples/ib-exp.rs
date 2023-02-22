#[allow(non_snake_case)]
extern crate KRdmaKit;

#[allow(unused_imports)]
use KRdmaKit::*;

/// Aims to use RDMA's advanced APIs
fn main() {
    #[cfg(feature = "exp")]
    {
        let ctx = UDriver::create()
            .expect("failed to query device")
            .devices()
            .into_iter()
            .next()
            .expect("no rdma device available")
            .open_context()
            .expect("failed to create RDMA context");

        let device_attr = ctx.get_exp_device_attr().unwrap();
        println!("check device max atomic arg {}", device_attr.exp_atomic_cap);
    }
}
