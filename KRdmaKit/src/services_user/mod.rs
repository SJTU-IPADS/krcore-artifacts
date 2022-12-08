//!
//! This module contains helper routines to faciliate exchanging requests between RDMA peers.
//! Specifically, RDMA peers need to use other communication primitives (e.g., TCP/IP)
//! to exchange several conneciton metadata to help the bootstrap.
//! Therefore, we use tokio as a bootstrap method.
//!
//!
extern crate async_trait;
extern crate serde;
extern crate serde_derive;
extern crate serde_json;

pub mod cm;
pub use cm::*;

use crate::queue_pairs::QPType;
use crate::CMError;
use async_trait::async_trait;
use rdma_shim::user::log;
use serde_derive::{Deserialize, Serialize};
use std::net::SocketAddr;
use std::sync::Arc;
use std::{io, thread};
use tokio::io::{AsyncReadExt, AsyncWriteExt, BufReader};
use tokio::net::tcp::WriteHalf;
use tokio::net::{TcpListener, TcpStream};

#[allow(unused)]
#[async_trait]
pub trait ConnectionManagerHandler: Send + Sync {
    async fn handle_reg_rc_req(&self, raw: String) -> Result<CMMessage, CMError> {
        unimplemented!()
    }
    async fn handle_reg_rc_res(&self, raw: String) -> Result<CMMessage, CMError> {
        unimplemented!()
    }
    async fn handle_dereg_rc_req(&self, raw: String) -> Result<CMMessage, CMError> {
        unimplemented!()
    }
    async fn handle_query_mr_req(&self, raw: String) -> Result<CMMessage, CMError> {
        unimplemented!()
    }
    async fn handle_query_mr_res(&self, raw: String) -> Result<CMMessage, CMError> {
        unimplemented!()
    }
    async fn handle_error(&self, raw: String) -> Result<CMMessage, CMError> {
        unimplemented!()
    }
}

#[derive(Clone, Serialize, Deserialize, Debug)]
pub enum CMMessageType {
    RegRCReq,
    RegRCRes,
    DeregRCReq,
    QueryMRReq,
    QueryMRRes,
    Error,
    NeverSend, // No need to send back this type of message
    Test,
}

impl CMMessageType {
    #[inline]
    pub fn should_send_back(&self) -> bool {
        match self {
            CMMessageType::NeverSend => false,
            _ => true,
        }
    }
}

#[derive(Clone, Serialize, Deserialize, Debug)]
pub struct CMMessage {
    pub message_type: CMMessageType,
    pub serialized: String,
}

impl CMMessage {
    pub fn Error() -> Self {
        Self {
            message_type: CMMessageType::Error,
            serialized: Default::default(),
        }
    }

    #[inline]
    pub fn should_send_back(&self) -> bool {
        self.message_type.should_send_back()
    }
}

unsafe impl Send for CMMessage {}
unsafe impl Sync for CMMessage {}

#[derive(Copy, Clone)]
pub struct CommStruct {
    pub qp_type: QPType,
    pub addr: SocketAddr,
    pub key: u64,
}

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
        let handler = DefaultConnectionManagerHandler::new(&ctx, 1);
        let server = ConnectionManagerServer::new(Arc::new(handler));
    }
}

pub struct ConnectionManagerServer<T: ConnectionManagerHandler> {
    handler: T,
}

unsafe impl<T: ConnectionManagerHandler> Send for ConnectionManagerServer<T> {}
unsafe impl<T: ConnectionManagerHandler> Sync for ConnectionManagerServer<T> {}

/// ConnectionManagerServer in user-mode.
impl<T: ConnectionManagerHandler + 'static> ConnectionManagerServer<T> {
    /// Create a ConnectionManagerServer with a specific `ConnectionManagerHandler` instance.
    ///
    /// To make the server work, call `ConnectionManagerServer::spawn_rc_server`.
    pub fn new(handler: T) -> Arc<Self> {
        Arc::new(Self { handler })
    }

    /// Spawn an listening thread to accept TCP connection and to handle RC-QP connection by this TCP stream.
    ///
    /// Param: `addr` is the socket address which the thread listens on.
    ///
    /// Return a `std::thread::JoinHandle<tokio::io::Result<()>>`
    /// The server thread will never exit unless something went wrong with the network IO (TCP stream).
    pub fn spawn_listener(
        self: &Arc<Self>,
        addr: SocketAddr,
    ) -> thread::JoinHandle<io::Result<()>> {
        let server = self.clone();
        thread::spawn(move || {
            tokio::runtime::Builder::new_multi_thread()
                .enable_all()
                .build()
                .unwrap()
                .block_on(server.listener_inner(addr))
        })
    }

    async fn listener_inner(self: &Arc<Self>, addr: SocketAddr) -> io::Result<()> {
        let listener = TcpListener::bind(addr).await?;
        loop {
            let (stream, _) = listener.accept().await?;
            let server = self.clone();
            let _handle = tokio::spawn(server.service_handler(stream));
        }
    }

    async fn service_handler(self: Arc<Self>, mut stream: TcpStream) -> Result<(), CMError> {
        let (read, mut write) = stream.split();
        let mut buf_reader = BufReader::new(read);

        loop {
            let mut buf = [0; 2048];
            let bytes_read = buf_reader.read(&mut buf).await.map_err(|_| {
                log::error!("IO error");
                CMError::Creation(0)
            })?;
            if bytes_read == 0 {
                // Disconnection
                return Ok(());
            }
            let message: CMMessage = serde_json::from_slice(&buf[0..bytes_read]).map_err(|_| {
                log::error!("Failed to do deserialization");
                CMError::Creation(0)
            })?;

            let msg_type = message.message_type;
            let raw = message.serialized;
            let handler = &self.handler;
            let msg = match msg_type {
                CMMessageType::RegRCReq => handler.handle_reg_rc_req(raw).await,
                CMMessageType::DeregRCReq => handler.handle_dereg_rc_req(raw).await,
                CMMessageType::QueryMRReq => handler.handle_query_mr_req(raw).await,
                _ => {
                    log::error!("Req type error");
                    Err(CMError::Creation(0))
                }
            }
            .unwrap_or(CMMessage::Error());
            then_send(&mut write, msg).await?;
        }
    }

    pub fn handler(&self) -> &T {
        &self.handler
    }
}

#[inline]
pub(crate) async fn then_send(
    write: &mut WriteHalf<'_>,
    message: CMMessage,
) -> Result<(), CMError> {
    if !message.should_send_back() {
        return Ok(());
    }

    let serialized = serde_json::to_vec(&message).map_err(|_| CMError::Creation(0))?;
    write.write(&serialized.as_slice()).await.map_err(|_| {
        log::error!("Send response error");
        CMError::Creation(0)
    })?;
    Ok(())
}
