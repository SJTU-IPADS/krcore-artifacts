//! 
//! This module contains helper routines to faciliate exchanging requests between RDMA peers.
//! Specifically, RDMA peers need to use other communication primitives (e.g., TCP/IP)
//! to exchange several conneciton metadata to help the bootstrap. 
//! Therefore, we use tokio as a bootstrap method. 
//! 
//! 
pub mod cm;
pub use cm::*;

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn basic_rc_service() {
        let ctx = crate::UDriver::create()
            .expect("failed to query device")
            .devices()
            .into_iter()
            .next()
            .expect("no rdma device available")
            .open_context()
            .expect("failed to create RDMA context");
        let _ = ConnectionManagerServer::new(&ctx, 1);
    }
}