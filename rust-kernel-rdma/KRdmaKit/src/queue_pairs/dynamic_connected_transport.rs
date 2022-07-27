use alloc::boxed::Box;
use alloc::sync::Arc;
use core::ptr::NonNull;

use linux_kernel_module::Error;

use crate::context::Context;
use crate::rust_kernel_rdma_base::*;
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

    key : u64,
}

impl DynamicConnectedTarget { 

    pub fn dct_num(&self) -> u32 { 
        unsafe { self.inner_target.as_ref().dct_num }
    }

    pub fn dc_key(&self) -> u64 { 
        self.key
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
            mtu: ib_mtu::IB_MTU_4096,
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
            key : dc_key
        })
    }
}

impl Drop for DynamicConnectedTarget {
    fn drop(&mut self) {
        unsafe { ib_exp_destroy_dct(self.inner_target.as_ptr(), core::ptr::null_mut()) };
    }
}
