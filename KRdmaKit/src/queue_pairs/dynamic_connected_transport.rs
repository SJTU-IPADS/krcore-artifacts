use alloc::boxed::Box;
use alloc::sync::Arc;
use core::ptr::NonNull;

use rdma_shim::Error;
use rdma_shim::bindings::*;

use crate::comm_manager::CMError;
use crate::context::Context;
use crate::{CompletionQueue, ControlpathError, SharedReceiveQueue};

pub type DynamicConnectedTargetBuilder = super::builder::QueuePairBuilder;

/// Abstract the DCT target to simplify lifecycle management
/// For more, check:  https://www.openfabrics.org/images/eventpresos/workshops2014/DevWorkshop/presos/Monday/pdf/05_DC_Verbs.pdf
#[allow(dead_code)]
pub struct DynamicConnectedTarget {
    _ctx: Arc<Context>,

    inner_target: NonNull<ib_dct>,
    send_cq: Box<CompletionQueue>,
    recv_cq: Arc<CompletionQueue>,
    shared_receive_queue: Box<SharedReceiveQueue>,

    key: u64,

    /// The following is carried from the builder
    port_num: u8,
}

impl DynamicConnectedTarget {
    #[inline]
    pub fn ctx(&self) -> &Arc<Context> {
        &self._ctx
    }

    pub fn dct_num(&self) -> u32 {
        unsafe { self.inner_target.as_ref().dct_num }
    }

    pub fn dc_key(&self) -> u64 {
        self.key
    }

    pub fn port_num(&self) -> u8 { 
        self.port_num
    }

    pub fn get_datagram_meta(&self) -> Result<crate::services::DatagramMeta, CMError> {
        let port_attr = self
            .ctx()
            .get_dev_ref()
            .get_port_attr(self.port_num)
            .map_err(|err| CMError::Creation(err.to_kernel_errno()))?;
        let gid = self
            .ctx()
            .get_dev_ref()
            // FIXME: what if gid_index != 0?
            .query_gid(self.port_num, 0)
            .map_err(|err| CMError::Creation(err.to_kernel_errno()))?;

        let lid = port_attr.lid as u16;
        Ok(crate::services::DatagramMeta { lid, gid })
    }
    
}

impl DynamicConnectedTargetBuilder {
    /// Build a DCT target based on the configuration parameters of QueuePairBuilder.
    ///
    /// We re-use its parameters since the parameters for DCT target
    /// is a subset of the parameters of the QueuePairBuilder.
    ///
    /// Parameters:
    /// - `dc_key`: a user-passed 64-bit key to identify the target
    pub fn build_dynamic_connected_target(
        self,
        dc_key: u64,
    ) -> Result<DynamicConnectedTarget, ControlpathError> {
        let send = Box::new(CompletionQueue::create(&self.ctx, self.max_cq_entries)?);
        let recv = Arc::new(CompletionQueue::create(&self.ctx, self.max_cq_entries)?);
        let shared_receive_queue = Box::new(SharedReceiveQueue::create(
            &self.ctx,
            self.max_recv_wr,
            self.max_recv_sge,
        )?);

        let mut dc_attr = ib_dct_init_attr {
            pd: self.ctx.get_pd().as_ptr(),
            cq: send.raw_ptr().as_ptr(),
            srq: shared_receive_queue.raw_ptr().as_ptr(),
            dc_key: dc_key,
            port: self.port_num,
            access_flags: self.access,
            min_rnr_timer: self.min_rnr_timer,
            inline_size: self.max_inline_data,
            hop_limit: 1,
            mtu: self.path_mtu,
            ..Default::default()
        };

        let dct = unsafe { safe_ib_create_dct(self.ctx.get_pd().as_ptr(), &mut dc_attr as _) };
        if dct.is_null() {
            return Err(ControlpathError::CreationError("DCT", Error::EAGAIN));
        }

        Ok(DynamicConnectedTarget {
            _ctx: self.ctx,
            inner_target: unsafe { NonNull::new_unchecked(dct) },
            send_cq: send,
            recv_cq: recv,
            shared_receive_queue,
            key: dc_key,
            port_num: self.port_num,
        })
    }
    
}

impl super::QueuePairBuilder {
    /// Build an dynamic connected queue pair with the set parameters.
    ///
    /// The built queue pair needs to be brought up to be used.
    ///
    /// # Errors:
    /// - `CreationError` : error creating send completion queue
    /// or recv completion queue or ud queue pair
    ///
    pub fn build_dc(self) -> Result<super::PreparedQueuePair, ControlpathError> {
        let send = Box::new(CompletionQueue::create(&self.ctx, self.max_cq_entries)?);
        let recv = Arc::new(CompletionQueue::create(&self.ctx, self.max_cq_entries)?);
        let shared_receive_queue = Box::new(SharedReceiveQueue::create(
            &self.ctx,
            self.max_recv_wr,
            self.max_recv_sge,
        )?);

        let mut qp_attr = ib_exp_qp_init_attr {
            cap: ib_qp_cap {
                max_send_wr: self.max_send_wr,
                max_recv_wr: 0, // we don't support recv wr for DC now
                max_recv_sge: self.max_recv_sge,
                max_send_sge: self.max_send_sge,
                max_inline_data: self.max_inline_data,
                ..Default::default()
            },
            sq_sig_type: ib_sig_type::IB_SIGNAL_REQ_WR,
            qp_type: ib_qp_type::IB_EXP_QPT_DC_INI as _,
            send_cq: send.raw_ptr().as_ptr(),
            recv_cq: recv.raw_ptr().as_ptr(),
            srq: shared_receive_queue.raw_ptr().as_ptr(),
            ..Default::default()
        };

        let qp = unsafe { ib_create_qp_dct(self.ctx.get_pd().as_ptr(), &mut qp_attr as _) };
        self.build_inner(
            qp,
            send,
            recv,
            super::QPType::DC,
            Some(shared_receive_queue),
        )
    }
}

impl Drop for DynamicConnectedTarget {
    fn drop(&mut self) {
        unsafe { ib_exp_destroy_dct(self.inner_target.as_ptr(), core::ptr::null_mut()) };
    }
}
