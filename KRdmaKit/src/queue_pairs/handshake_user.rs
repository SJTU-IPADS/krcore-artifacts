use crate::services_user::{ibv_gid_wrapper, CMReqType, RCConnectionData};
use std::io::Write;
use std::net::SocketAddr;
use std::net::TcpStream as StdTcpStream;
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::TcpStream;

#[cfg(feature = "user")]
impl PreparedQueuePair {
    /// The method `handshake` serves for RC control path, establishing communication between client
    /// and server. After the handshake phase, client side RC qp's `addr` field will be filled, which
    /// is for `Drop` to use.
    ///
    /// Param:
    /// - `addr` : ReliableConnectionServer's listening address.
    ///
    pub fn handshake(self, addr: SocketAddr) -> Result<Arc<QueuePair>, ControlpathError> {
        tokio::runtime::Builder::new_current_thread()
            .enable_all()
            .build()
            .map_err(|_| {
                ControlpathError::CreationError(
                    "Failed to build tokio Runtime",
                    Error::from_kernel_errno(0),
                )
            })?
            .block_on(self.handshake_inner(addr))
    }

    async fn handshake_inner(self, addr: SocketAddr) -> Result<Arc<QueuePair>, ControlpathError> {
        // check qp type
        // must be rc and never initialized
        if self.inner.mode != QPType::RC {
            log::error!("bring up type check error!");
            return Err(ControlpathError::CreationError(
                "QP mode that is not RC does not need handshake to bring it up!",
                Error::from_kernel_errno(0),
            ));
        }

        let mut stream = TcpStream::connect(addr).await.map_err(|_| {
            ControlpathError::CreationError("Failed to connect server", Error::from_kernel_errno(0))
        })?;
        let (mut read, mut write) = stream.split();

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

        let req = CMReqType::RegReq(data);

        let serialized = serde_json::to_string(&req).map_err(|_| {
            ControlpathError::CreationError(
                "Failed to do serialization",
                Error::from_kernel_errno(0),
            )
        })?;
        let _ = write.write(serialized.as_bytes()).await;

        let mut buffer = [0; 1024];
        let bytes_read = read.read(&mut buffer).await.map_err(|_| {
            log::error!("IO error");
            ControlpathError::CreationError(
                "Failed to do read from client",
                Error::from_kernel_errno(0),
            )
        })?;

        let req_type: CMReqType = serde_json::from_slice(&buffer[..bytes_read]).map_err(|_| {
            ControlpathError::CreationError("Failed to bring up qp", Error::from_kernel_errno(0))
        })?;

        let data = match req_type {
            CMReqType::RegRes(data) => data,
            _ => {
                log::error!("Response type error");
                return Err(ControlpathError::CreationError(
                    "Failed to pass response type check",
                    Error::from_kernel_errno(0),
                ));
            }
        };

        rc_qp
            .bring_up_rc_inner(data.lid, data.gid.into(), data.qpn, data.starting_psn)
            .map_err(|_| {
                ControlpathError::CreationError(
                    "Failed to bring up qp",
                    Error::from_kernel_errno(0),
                )
            })?;

        rc_qp.rc_key = data.rc_key;
        rc_qp.addr = Some(addr);
        Ok(Arc::new(rc_qp))
    }
}

#[cfg(feature = "user")]
impl QueuePair {
    pub(crate) fn dereg(&self) {
        let addr = if self.addr.is_none() {
            return;
        } else {
            self.addr.unwrap()
        };
        let res = StdTcpStream::connect(addr);
        let mut stream = if res.is_ok() { res.unwrap() } else { return };
        let dereg = CMReqType::DeregReq(self.rc_key);
        let serialized = serde_json::to_string(&dereg);
        let serialized = if serialized.is_ok() {
            serialized.unwrap()
        } else {
            return;
        };
        let _ = stream.write(serialized.as_bytes());
    }
}
