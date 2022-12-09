use crate::context::Context;
use crate::services_user::{CMMessage, CMMessageType, ConnectionManagerHandler};
use crate::{CMError, MemoryRegion, QueuePair, QueuePairBuilder};
use core::fmt::Debug;
use rdma_shim::bindings::*;
use rdma_shim::log;
use serde_derive::{Deserialize, Serialize};
use std::collections::HashMap;
use std::sync::{Arc, Mutex};

/// `RCConnectionData` is used for remote QP to connect with it
#[derive(Copy, Clone, Serialize, Deserialize, Debug)]
pub struct RCConnectionData {
    pub lid: u32,
    pub gid: ibv_gid_wrapper,
    pub qpn: u32,
    pub starting_psn: u32,
    pub rnr_retry_count: u8,
    pub rc_key: u64, // for server-side indexing related qp
}

/// `ibv_gid_wrapper` is a wrapper of `ib_gid`(which is generated by `bindgen`), to derive `serde::Serialize` trait automatically.
///
/// Both `From<ib_gid>` and `Into<ib_gid>` are implemented for `ibv_gid_wrapper`.
///
/// Only for user-mode to use.
#[repr(C)]
#[allow(non_camel_case_types)]
#[derive(Copy, Clone, Serialize, Deserialize, Debug)]
pub struct ibv_gid_wrapper {
    raw: [u64; 2usize],
}

impl From<ib_gid> for ibv_gid_wrapper {
    fn from(gid: ib_gid) -> Self {
        Self {
            raw: gid.bindgen_union_field,
        }
    }
}

impl Into<ib_gid> for ibv_gid_wrapper {
    fn into(self) -> ib_gid {
        ib_gid {
            bindgen_union_field: self.raw,
            ..Default::default()
        }
    }
}

/// The DefaultHandler basically handles connection-requests and stores the incoming RC-QPs.
///
/// After receiving an RC-QP connection request,
/// it will automatically create a new one and store it in its `registered_rc`.
pub struct DefaultConnectionManagerHandler {
    pub registered_rc: Arc<Mutex<HashMap<u64, Arc<QueuePair>>>>,
    pub registered_mr: MRWrapper,
    pub port_num: u8,
    pub ctx: Arc<Context>,
}

unsafe impl Send for DefaultConnectionManagerHandler {}
unsafe impl Sync for DefaultConnectionManagerHandler {}

impl DefaultConnectionManagerHandler {
    /// Create an DefaultConnectionManagerHandler that listens to connection request and handles registration
    /// requests and de-registration request.
    ///
    /// Param `port_num` is the  RC-QP's port number to be created.
    ///
    /// Param `ctx` is corresponding to NIC you want to use.
    /// Pass different `ctx` created by `UDriver` to select a specific NIC to use or simultaneously use multiple-NICs
    ///
    /// Return an `Arc<DefaultConnectionManagerHandler>` for communicating usage.
    pub fn new(ctx: &Arc<Context>, port_num: u8) -> Self {
        Self {
            registered_rc: Arc::new(Default::default()),
            registered_mr: Default::default(),
            port_num,
            ctx: ctx.clone(),
        }
    }

    #[inline]
    pub fn register_mr(&mut self, mrs: Vec<(String, MemoryRegion)>) {
        let _ = self.registered_mr.insert(mrs);
    }

    #[inline]
    pub fn exp_get_qps(&self) -> Vec<Arc<QueuePair>> {
        self.registered_rc
            .lock()
            .unwrap()
            .iter()
            .map(|(_, qp)| qp.clone())
            .collect()
    }

    #[inline]
    pub fn exp_get_mrs(&self) -> Vec<&MemoryRegion> {
        self.registered_mr.inner.iter().map(|(_, mr)| mr).collect()
    }

    #[inline]
    pub fn ctx(&self) -> &Arc<Context> {
        &self.ctx
    }
}

impl ConnectionManagerHandler for DefaultConnectionManagerHandler {
    fn handle_reg_rc_req(&self, raw: String) -> Result<CMMessage, CMError> {
        let data: RCConnectionData = serde_json::from_str(raw.as_str())
            .map_err(|_| CMError::InvalidArg("Failed to do deserialization", "".to_string()))?;
        let mut builder = QueuePairBuilder::new(&self.ctx);
        builder
            .set_rnr_retry(data.rnr_retry_count)
            .set_port_num(self.port_num)
            .set_max_send_wr(64) // not necessary to use a large WR: server QP is passive
            .allow_remote_rw()
            .allow_remote_atomic();
        let rc_qp = builder.build_rc().map_err(|_| {
            log::error!("Build rc error");
            CMError::Creation(0)
        })?;

        // directly bring this QP up
        let rc_qp = rc_qp
            .bring_up_rc(data.lid, data.gid.into(), data.qpn, data.starting_psn)
            .map_err(|_| {
                log::error!("Bring up rc error");
                CMError::Creation(0)
            })?;

        let rc_key = rc_qp.as_ref() as *const QueuePair as u64;

        let lid = rc_qp
            .ctx()
            .get_port_attr(self.port_num)
            .map_err(|err| CMError::Creation(err.to_kernel_errno()))?
            .lid as u32;

        let gid = rc_qp
            .ctx()
            .query_gid(self.port_num, 0) // FIXME: what if gid_index != 0?
            .map_err(|err| CMError::Creation(err.to_kernel_errno()))?;

        // send back
        let data = RCConnectionData {
            lid,
            gid: ibv_gid_wrapper::from(gid),
            qpn: rc_qp.qp_num(),
            starting_psn: rc_qp.qp_num(),
            rnr_retry_count: data.rnr_retry_count,
            rc_key,
        };
        self.registered_rc.lock().unwrap().insert(rc_key, rc_qp);
        let serialized = serde_json::to_string(&data).map_err(|_| CMError::Creation(0))?;
        Ok(CMMessage {
            message_type: CMMessageType::RegRCRes,
            serialized,
        })
    }

    fn handle_dereg_rc_req(&self, raw: String) -> Result<CMMessage, CMError> {
        let rc_key: u64 = serde_json::from_str(raw.as_str())
            .map_err(|_| CMError::InvalidArg("Failed to do deserialization", "".to_string()))?;
        self.registered_rc.lock().unwrap().remove(&rc_key);
        Ok(CMMessage {
            message_type: CMMessageType::NeverSend,
            serialized: Default::default(),
        })
    }

    fn handle_query_mr_req(&self, _raw: String) -> Result<CMMessage, CMError> {
        let mrs = self.registered_mr.to_mrinfos();
        let serialized = serde_json::to_string(&mrs).map_err(|_| CMError::Creation(0))?;
        Ok(CMMessage {
            message_type: CMMessageType::QueryMRRes,
            serialized,
        })
    }

    fn handle_error(&self, _raw: String) -> Result<CMMessage, CMError> {
        return Ok(CMMessage {
            message_type: CMMessageType::NeverSend,
            serialized: "Remote Side Error".to_string(),
        });
    }
}

#[derive(Copy, Clone, Serialize, Deserialize, Debug)]
pub struct MRInfo {
    pub addr: u64,
    pub capacity: usize,
    pub rkey: u32,
}

#[derive(Clone, Serialize, Deserialize, Debug, Default)]
pub struct MRInfos {
    inner: HashMap<String, MRInfo>,
}

unsafe impl Send for MRInfos {}
unsafe impl Sync for MRInfos {}

impl MRInfos {
    #[inline]
    pub fn inner(&self) -> &HashMap<String, MRInfo> {
        &self.inner
    }
}

#[derive(Default)]
pub struct MRWrapper {
    pub inner: HashMap<String, MemoryRegion>,
}

unsafe impl Send for MRWrapper {}
unsafe impl Sync for MRWrapper {}

impl MRWrapper {
    #[inline]
    pub fn insert(&mut self, mrs: Vec<(String, MemoryRegion)>) {
        for mr in mrs {
            self.inner.insert(mr.0, mr.1);
        }
    }

    #[inline]
    pub fn to_mrinfos(&self) -> MRInfos {
        let mut infos = HashMap::default();
        for (k, mr) in &self.inner {
            infos.insert(k.clone(), MRInfo::from(mr));
        }
        MRInfos { inner: infos }
    }
}

impl From<&MemoryRegion> for MRInfo {
    fn from(mr: &MemoryRegion) -> Self {
        Self {
            addr: mr.get_virt_addr(),
            capacity: mr.capacity(),
            rkey: mr.rkey().0,
        }
    }
}
