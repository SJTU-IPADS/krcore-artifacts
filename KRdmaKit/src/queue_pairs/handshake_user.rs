use crate::services_user::{
    ibv_gid_wrapper, then_send_async, then_send_sync, CMMessage, CommStruct, MRInfos,
    RCConnectionData,
};
use std::io::{Read, Write};
use std::net::SocketAddr;
use std::net::TcpStream as StdTcpStream;
use tokio::net::TcpStream;

#[cfg(feature = "user")]
impl PreparedQueuePair {
    /// The method `handshake` serves for RC control path, establishing communication between client
    /// and server. After the handshake phase, client side RC qp's `comm` field will be filled, which
    /// is for `Drop` to use.
    ///
    /// Param:
    /// - `addr` : ReliableConnectionServer's listening address.
    ///
    /// The reason why this function looks so ugly is that we cannot call a `block_on`
    /// in an asynchronous context, neither create a new tokio runtime. So once we detect that this
    /// function is called in an asynchronous context, we must create a new thread and wait until
    /// the newly created thread finished.
    ///
    /// We provide you an **async version of handshake method**, which avoids double scheduling
    /// and thread creation/destruction
    pub fn handshake(self, addr: SocketAddr) -> Result<Arc<QueuePair>, ControlpathError> {
        // check qp type
        // must be rc and never initialized
        if self.inner.mode != QPType::RC {
            log::error!("bring up type check error!");
            return Err(ControlpathError::CreationError(
                "QP mode that is not RC does not need handshake to bring it up!",
                Error::from_kernel_errno(0),
            ));
        }

        let mut stream = std::net::TcpStream::connect(addr).map_err(|_| {
            ControlpathError::CreationError("Failed to connect server", Error::from_kernel_errno(0))
        })?;

        let mut rc_qp = self.inner;

        let lid = rc_qp
            .ctx()
            .get_port_attr(self.port_num)
            .map_err(|_| {
                ControlpathError::CreationError("Failed to query lid", Error::from_kernel_errno(0))
            })?
            .lid as u32;

        let gid = rc_qp
            .ctx()
            .query_gid(self.port_num, 0) // FIXME: what if gid_index != 0?
            .map_err(|_| {
                ControlpathError::CreationError("Failed to query gid", Error::from_kernel_errno(0))
            })?;

        let data = RCConnectionData {
            lid,
            gid: ibv_gid_wrapper::from(gid),
            qpn: rc_qp.qp_num(),
            starting_psn: rc_qp.qp_num(),
            rnr_retry_count: self.retry_count,
            rc_key: 0,
        };

        let serialized = serde_json::to_string(&data).map_err(|_| {
            ControlpathError::CreationError(
                "Failed to do serialization",
                Error::from_kernel_errno(0),
            )
        })?;

        let req = CMMessage {
            message_type: CMMessageType::RegRCReq,
            serialized,
        };

        let _ = then_send_sync(&mut stream, req).map_err(|_| {
            log::error!("IO error");
            ControlpathError::CreationError(
                "Failed to write to server",
                Error::from_kernel_errno(0),
            )
        })?;

        let mut buffer = [0; 1024];
        let bytes_read = stream.read(&mut buffer).map_err(|_| {
            log::error!("IO error");
            ControlpathError::CreationError(
                "Failed to read from client",
                Error::from_kernel_errno(0),
            )
        })?;
        let message: CMMessage = serde_json::from_slice(&buffer[..bytes_read]).map_err(|_| {
            ControlpathError::CreationError(
                "Failed to deserialize CM Message",
                Error::from_kernel_errno(0),
            )
        })?;

        if message.message_type != CMMessageType::RegRCRes {
            log::error!("Response type error");
            return Err(ControlpathError::CreationError(
                "Failed to pass response type check",
                Error::from_kernel_errno(0),
            ));
        };

        let data: RCConnectionData =
            serde_json::from_str(message.serialized.as_str()).map_err(|_| {
                ControlpathError::CreationError(
                    "Failed to deserialize RCConnection Data",
                    Error::from_kernel_errno(0),
                )
            })?;

        rc_qp
            .bring_up_rc_inner(data.lid, data.gid.into(), data.qpn, data.starting_psn)
            .map_err(|_| {
                ControlpathError::CreationError(
                    "Failed to bring up qp",
                    Error::from_kernel_errno(0),
                )
            })?;

        rc_qp.comm = Some(CommStruct {
            qp_type: QPType::RC,
            addr,
            key: data.rc_key,
        });
        Ok(Arc::new(rc_qp))
    }
}

#[cfg(feature = "user")]
impl QueuePair {
    pub(crate) fn dereg(&self) {
        let comm = if self.comm.is_none() {
            return;
        } else {
            self.comm.unwrap()
        };

        match comm.qp_type {
            QPType::RC => {
                let res = StdTcpStream::connect(comm.addr);
                let mut stream = if res.is_ok() { res.unwrap() } else { return };
                let serialized = serde_json::to_string(&comm.key).unwrap();
                let dereg = CMMessage {
                    message_type: CMMessageType::DeregRCReq,
                    serialized,
                };
                let serialized = serde_json::to_vec(&dereg);
                let serialized = if serialized.is_ok() {
                    serialized.unwrap()
                } else {
                    return;
                };
                let _ = stream.write(serialized.as_slice());
            }
            QPType::UD => unimplemented!(),
            QPType::WC => unimplemented!(),
            QPType::DC => unimplemented!(),
        }
    }

    /// The reason why this function is so ugly is the same as `handshake`. See the doc of `handshake`
    /// for more details.
    pub fn query_mr_info(&self) -> Result<MRInfos, ControlpathError> {
        let addr = match self.comm {
            None => {
                return Err(ControlpathError::CreationError(
                    "None value on `comm`",
                    Error::from_kernel_errno(0),
                ))
            }
            Some(comm) => comm.addr,
        };
        let mut stream = std::net::TcpStream::connect(addr).map_err(|_| {
            ControlpathError::CreationError("Failed to connect server", Error::from_kernel_errno(0))
        })?;

        let req = CMMessage {
            message_type: CMMessageType::QueryMRReq,
            serialized: String::new(),
        };

        let _ = then_send_sync(&mut stream, req).map_err(|_| {
            ControlpathError::CreationError("Failed to send message", Error::from_kernel_errno(0))
        })?;

        let mut buffer = [0; 1024];
        let bytes_read = stream.read(&mut buffer).map_err(|_| {
            log::error!("IO error");
            ControlpathError::CreationError(
                "Failed to do read from client",
                Error::from_kernel_errno(0),
            )
        })?;

        let message: CMMessage = serde_json::from_slice(&buffer[..bytes_read]).map_err(|_| {
            ControlpathError::CreationError(
                "Failed to do deserialization",
                Error::from_kernel_errno(0),
            )
        })?;

        if message.message_type != CMMessageType::QueryMRRes {
            log::error!("Response type error");
            return Err(ControlpathError::CreationError(
                "Failed to pass response type check",
                Error::from_kernel_errno(0),
            ));
        }

        let data = serde_json::from_str(message.serialized.as_str()).map_err(|_| {
            ControlpathError::CreationError(
                "Failed to do deserialization",
                Error::from_kernel_errno(0),
            )
        })?;
        Ok(data)
    }
}
