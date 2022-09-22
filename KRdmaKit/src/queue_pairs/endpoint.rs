#[allow(unused_imports)]
use alloc::sync::Arc;

use core::fmt::{Debug, Formatter};
use core::ptr::NonNull;

use rdma_shim::bindings::*;
use rdma_shim::{log, Error};

use crate::context::{AddressHandler, ContextRef};
use crate::ControlpathError;

/// The datagram endpoint
/// that a client QP can use to communicate with a server.
///
/// If feature=dct is enabled, it further contains the dct_num and dc_key fields.
///
/// Note:
/// - when dct is enabled, the qkey & qpn fields are meaningless if the remote end is DCT.
/// - when dct is enabled, the dct_num & dc_key fields are meaningless if the remote end is UD.
///
/// Warn:
/// - The behavior is **undefined** if you query a DCT endpoint, while remote is listening UD queries (and vice verse).
///
/// For the meaning of these fields, check the documentations in builder.rs
///
pub struct DatagramEndpoint {
    address_handler: AddressHandler,
    qpn: u32,
    qkey: u32,
    lid: u32,
    gid: ib_gid,
    #[cfg(feature = "dct")]
    dct_num: u32,
    #[cfg(feature = "dct")]
    dc_key: u64,
}

impl Debug for DatagramEndpoint {
    fn fmt(&self, f: &mut Formatter<'_>) -> core::fmt::Result {
        #[cfg(not(feature = "dct"))]
        {
            f.debug_struct("EndPoint")
                .field("remote qpn", &self.qpn)
                .field("remote qkey", &self.qkey)
                .field("remote lid", &self.lid)
                .field("remote gid", &self.gid)
                .field("address handler", &self.address_handler)
                .finish()
        }

        // FIXME: should refine later
        #[cfg(feature = "dct")]
        {
            f.debug_struct("EndPoint")
                .field("remote qpn", &self.qpn)
                .field("remote qkey", &self.qkey)
                .field("remote lid", &self.lid)
                .field("remote gid", &self.gid)
                .field("address handler", &self.address_handler)
                .field("dct_num", &self.dct_num)
                .field("dc_key", &self.dc_key)
                .finish()
        }
    }
}

impl DatagramEndpoint {

    /// Create a datagram endpoint given the remote information
    /// Note, the following parameters must be queried from the remote:
    /// - lid
    /// - gid
    /// - qpn
    /// - qkey
    /// 
    pub fn new(
        ctx: &ContextRef,
        local_port_num: u8,
        lid: u32,
        gid: ib_gid,
        qpn: u32,
        qkey: u32,

        #[cfg(feature = "dct")] dct_num: u32,
        #[cfg(feature = "dct")] dc_key: u64,
    ) -> Result<Self, ControlpathError> {
        // FIXME: what if gid_index != 0?
        let ah = ctx.create_address_handler(local_port_num, 0, lid, gid)?;

        #[cfg(not(feature = "dct"))]
        return Ok(Self {
            address_handler: ah,
            qpn,
            qkey,
            lid,
            gid,
        });

        #[cfg(feature = "dct")]
        Ok(Self {
            address_handler: ah,
            qpn,
            qkey,
            lid,
            gid,
            dct_num,
            dc_key,
        })
    }

    #[inline]
    pub fn raw_address_handler_ptr(&self) -> &NonNull<ib_ah> {
        self.address_handler.raw_ptr()
    }

    #[inline]
    pub fn address_handler(&self) -> &AddressHandler {
        &self.address_handler
    }

    #[inline]
    pub fn qpn(&self) -> u32 {
        self.qpn
    }

    #[inline]
    pub fn qkey(&self) -> u32 {
        self.qkey
    }

    #[inline]
    pub fn lid(&self) -> u32 {
        self.lid
    }

    #[inline]
    pub fn gid(&self) -> ib_gid {
        self.gid
    }

    #[cfg(feature = "dct")]
    #[inline]
    pub fn dc_key(&self) -> u64 {
        self.dc_key
    }

    #[cfg(feature = "dct")]
    #[inline]
    pub fn dct_num(&self) -> u32 {
        self.dct_num
    }
}

#[cfg(feature = "kernel")]
pub use kernel_querier::*;

#[cfg(feature = "kernel")]
mod kernel_querier {

    use super::*;
    use crate::comm_manager::{CMCallbacker, CMError, CMReplyer, CMSender};
    use rdma_shim::utils::completion;

    #[cfg(feature = "dct")]
    use crate::services::dc::DynamicConnectedMeta;

    #[cfg(not(feature = "dct"))]
    use crate::services::DatagramMeta;    

    /// This querier serves you a quick way to get the endpoint information from
    /// the server side (see `services/mod.rs`).
    pub struct DatagramEndpointQuerier {
        sender: CMSender<DatagramQuerierInner>,
        inner: Arc<DatagramQuerierInner>,
    }

    #[cfg(feature = "kernel")]
    impl DatagramEndpointQuerier {
        /// Create an DatagramEndpointQuerier.
        ///
        /// The `port_num` should be consistent with local queue pair's port_num you are going to build
        pub fn create(ctx: &ContextRef, local_port_num: u8) -> Result<Self, ControlpathError> {
            let mut querier_inner = DatagramQuerierInner::create(ctx, local_port_num);
            let sender =
                CMSender::new(&querier_inner, ctx.get_dev_ref()).map_err(|err| match err {
                    CMError::Creation(i) => {
                        ControlpathError::CreationError("UD Querier", Error::from_kernel_errno(i))
                    }
                    _ => ControlpathError::CreationError("UD Querier unknown error", Error::EAGAIN),
                })?;

            let querier_inner_ref = unsafe { Arc::get_mut_unchecked(&mut querier_inner) };
            querier_inner_ref.completion.init();
            Ok(Self {
                sender,
                inner: querier_inner,
            })
        }

        /// Core method of the querier. Return an Endpoint if success and ControlPathError otherwise.
        ///
        /// Input:
        /// - `remote_service_id` : Predefined service id that you want to query
        /// - `qd_hint`: Query remote UD information with qd_hint enables the server to have more than one UD qp server on one service id
        /// - `sa_path_rec` : path resolved by `Explorer`, attention that the resolved path must be consistent with the remote_service_id
        pub fn query(
            mut self,
            remote_service_id: u64,
            qd_hint: usize,
            path: sa_path_rec,
        ) -> Result<DatagramEndpoint, ControlpathError> {
            let mut querier_inner = self.inner;
            let querier_inner_ref = unsafe { Arc::get_mut_unchecked(&mut querier_inner) };
            let req = ib_cm_sidr_req_param {
                path: &path as *const _ as _,
                service_id: remote_service_id,
                timeout_ms: 20,
                max_cm_retries: 3,
                ..Default::default()
            };
            let _ = self
                .sender
                .send_sidr(req, qd_hint as usize)
                .map_err(|err| match err {
                    CMError::Creation(i) => {
                        ControlpathError::QueryError("", Error::from_kernel_errno(i))
                    }
                    _ => ControlpathError::QueryError("", Error::EAGAIN),
                })?;

            querier_inner_ref
                .completion
                .wait(1000)
                .map_err(|err: Error| ControlpathError::QueryError("Datagram Endpoint", err))?;
            querier_inner_ref
                .take_endpoint()
                .ok_or(ControlpathError::QueryError(
                    "Failed to get the endpoint",
                    Error::EAGAIN,
                ))
        }
    }

    /// Wrap it in CMSender which already implements callback function to establish UD sidr connection
    pub struct DatagramQuerierInner {
        completion: completion,
        ctx: ContextRef,
        endpoint: Option<DatagramEndpoint>,
        port_num: u8,
    }

    impl DatagramQuerierInner {
        fn create(ctx: &ContextRef, port_num: u8) -> Arc<DatagramQuerierInner> {
            let mut querier_inner = Arc::new(DatagramQuerierInner {
                completion: Default::default(),
                ctx: ctx.clone(),
                endpoint: None,
                port_num,
            });
            let ud_ref = unsafe { Arc::get_mut_unchecked(&mut querier_inner) };
            ud_ref.completion.init();
            querier_inner
        }

        fn take_endpoint(&mut self) -> Option<DatagramEndpoint> {
            self.endpoint.take()
        }
    }

    impl CMCallbacker for DatagramQuerierInner {
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
                #[cfg(not(feature = "dct"))]
                let reply = unsafe { *(rep_param.info as *mut DatagramMeta) };

                #[cfg(feature = "dct")]
                let reply = unsafe { *(rep_param.info as *mut DynamicConnectedMeta) };

                #[cfg(feature = "dct")]
                {
                    self.endpoint = DatagramEndpoint::new(
                        &self.ctx,
                        self.port_num,
                        reply.datagram_addr.lid as u32,
                        reply.datagram_addr.gid,
                        rep_param.qpn,
                        rep_param.qkey,
                        reply.dct_num,
                        reply.dc_key,
                    )
                    .ok();
                }

                #[cfg(not(feature = "dct"))]
                {
                    self.endpoint = DatagramEndpoint::new(
                        &self.ctx,
                        self.port_num,
                        reply.lid as u32,
                        reply.gid,
                        rep_param.qpn,
                        rep_param.qkey,
                    )
                    .ok();
                }
            }
            self.completion.done();
            Ok(())
        }
    }
}
