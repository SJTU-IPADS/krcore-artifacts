use alloc::sync::Arc;
use core::fmt::{Debug, Formatter};
use core::ptr::NonNull;

use rust_kernel_rdma_base::rust_kernel_linux_util::bindings::completion;
use rust_kernel_rdma_base::*;

use crate::comm_manager::{CMCallbacker, CMError, CMReplyer, CMSender};
use crate::context::{AddressHandler, ContextRef};
use crate::linux_kernel_module::Error;
use crate::services::UnreliableDatagramMeta;
use crate::{log, ControlpathError};

/// The unreliable datagram endpoint
/// that a client QP can use to communicate with a server
///
/// For the meaning of these fields, check the documentations in builder.rs
///
pub struct UnreliableDatagramEndpoint {
    address_handler: AddressHandler,
    qpn: u32,
    qkey: u32,
    lid: u32,
    gid: ib_gid,
}

impl Debug for UnreliableDatagramEndpoint {
    fn fmt(&self, f: &mut Formatter<'_>) -> core::fmt::Result {
        f.debug_struct("EndPoint")
            .field("qpn", &self.qpn)
            .field("qkey", &self.qkey)
            .field("lid", &self.lid)
            .field("gid", &self.gid)
            .field("address handler", &self.address_handler)
            .finish()
    }
}

impl UnreliableDatagramEndpoint {
    pub fn new(
        ctx: &ContextRef,
        local_port_num: u8,
        lid: u32,
        gid: ib_gid,
        qpn: u32,
        qkey: u32,
    ) -> Result<Self, ControlpathError> {
        let ah = ctx.create_address_handler(local_port_num, lid, gid)?;
        Ok(Self {
            address_handler: ah,
            qpn,
            qkey,
            lid,
            gid,
        })
    }

    pub fn raw_address_handler_ptr(&self) -> &NonNull<ib_ah> {
        self.address_handler.raw_ptr()
    }

    pub fn address_handler(&self) -> &AddressHandler {
        &self.address_handler
    }

    pub fn qpn(&self) -> u32 {
        self.qpn
    }

    pub fn qkey(&self) -> u32 {
        self.qkey
    }

    pub fn lid(&self) -> u32 {
        self.lid
    }

    pub fn gid(&self) -> ib_gid {
        self.gid
    }
}

/// This querier serves you a quick way to get the endpoint information from
/// the server side (defined in `UnreliableDatagramServer`).
pub struct UnreliableDatagramEndpointQuerier {
    inner: CMSender<UnreliableDatagramQuerierInner>,
}

impl UnreliableDatagramEndpointQuerier {
    /// Create an UnreliableDatagramEndpointQuerier.
    ///
    /// The `port_num` should be consistent with local queue pair's port_num you are going to build
    pub fn create(ctx: &ContextRef, local_port_num: u8) -> Result<Self, ControlpathError> {
        let mut querier_inner = UnreliableDatagramQuerierInner::create(ctx, local_port_num);
        let sender = CMSender::new(&querier_inner, ctx.get_dev_ref()).map_err(|err| match err {
            CMError::Creation(i) => {
                ControlpathError::CreationError("UD Querier", Error::from_kernel_errno(i))
            }
            _ => ControlpathError::CreationError("UD Querier unknown error", Error::EAGAIN),
        })?;
        let querier_inner_ref = unsafe { Arc::get_mut_unchecked(&mut querier_inner) };
        querier_inner_ref.completion.init();
        Ok(Self { inner: sender })
    }

    /// Core method of the querier. Return an Endpoint if success and ControlPathError otherwise.
    ///
    /// Input:
    /// - `remote_service_id` : Predefined service id that you want to query
    /// - `sa_path_rec` : path resolved by `Explorer`, attention that the resolved path must be consistent with the remote_service_id
    pub fn query(
        mut self,
        remote_service_id: u64,
        path: sa_path_rec,
    ) -> Result<UnreliableDatagramEndpoint, ControlpathError> {
        let mut querier_inner = self.inner.callbacker().clone();
        let querier_inner_ref = unsafe { Arc::get_mut_unchecked(&mut querier_inner) };
        let req = ib_cm_sidr_req_param {
            path: &path as *const _ as _,
            service_id: remote_service_id,
            timeout_ms: 20,
            max_cm_retries: 3,
            ..Default::default()
        };
        let _ = self
            .inner
            .send_sidr(req, remote_service_id as usize)
            .map_err(|err| match err {
                CMError::Creation(i) => {
                    ControlpathError::QueryError("", Error::from_kernel_errno(i))
                }
                _ => ControlpathError::QueryError("", Error::EAGAIN),
            })?;
        querier_inner_ref
            .completion
            .wait(1000)
            .map_err(|err: Error| ControlpathError::QueryError("Unreliable Datagram", err))?;
        querier_inner_ref
            .take_endpoint()
            .ok_or(ControlpathError::QueryError(
                "Failed to get the endpoint",
                Error::EAGAIN,
            ))
    }
}

/// Wrap it in CMSender which already implements callback function to establish UD sidr connection
pub struct UnreliableDatagramQuerierInner {
    completion: completion,
    ctx: ContextRef,
    endpoint: Option<UnreliableDatagramEndpoint>,
    port_num: u8,
}

impl UnreliableDatagramQuerierInner {
    fn create(ctx: &ContextRef, port_num: u8) -> Arc<UnreliableDatagramQuerierInner> {
        let mut querier_inner = Arc::new(UnreliableDatagramQuerierInner {
            completion: Default::default(),
            ctx: ctx.clone(),
            endpoint: None,
            port_num,
        });
        let ud_ref = unsafe { Arc::get_mut_unchecked(&mut querier_inner) };
        ud_ref.completion.init();
        querier_inner
    }

    fn take_endpoint(&mut self) -> Option<UnreliableDatagramEndpoint> {
        self.endpoint.take()
    }
}

impl CMCallbacker for UnreliableDatagramQuerierInner {
    /// Called on receiving SIDR_REP from server side
    fn handle_sidr_rep(
        self: &mut Self,
        mut _reply_cm: CMReplyer,
        event: &ib_cm_event,
    ) -> Result<(), CMError> {
        let rep_param = unsafe { event.param.sidr_rep_rcvd };
        if rep_param.status != ib_cm_sidr_status::IB_SIDR_SUCCESS {
            log::error!(
                "Failed to send SIDR connection with status {:?}",
                rep_param.status
            );
        } else {
            let reply = unsafe { *(rep_param.info as *mut UnreliableDatagramMeta) };
            self.endpoint = UnreliableDatagramEndpoint::new(
                &self.ctx,
                self.port_num,
                reply.lid as u32,
                reply.gid,
                rep_param.qpn,
                rep_param.qkey,
            )
            .ok();
        }
        self.completion.done();
        Ok(())
    }
}
