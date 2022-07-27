use alloc::boxed::Box;
use alloc::sync::Arc;
use core::ptr::NonNull;

use crate::rust_kernel_rdma_base::*;
use crate::{CompletionQueue, SharedReceiveQueue};

/// Abstract the DCT target to simplify lifecycle management
/// For more, check:  https://www.openfabrics.org/images/eventpresos/workshops2014/DevWorkshop/presos/Monday/pdf/05_DC_Verbs.pdf
pub struct DynamicConnectedTarget {
    _ctx: Arc<Context>,

    inner_target: NonNull<ib_dct>,
    send_cq: Box<CompletionQueue>,
    recv_cq: Box<CompletionQueue>,
    shared_receive_queue: Box<SharedReceiveQueue>,
}

impl super::builder::QueuePairBuilder {
    pub fn build_dynamic_connected_target(
        self,
    ) -> Result<DynamicConnectedTarget, ControlpathError> {
        let send = Box::new(CompletionQueue::create(&self.ctx, self.max_cq_entries)?);
        let recv = Arc::new(CompletionQueue::create(&self.ctx, self.max_cq_entries)?);
        let shared_receive_queue = Box::new(SharedReceiveQueue::create(
            &self.ctx,
            self.max_recv_wr,
            self.max_recv_sge,
        )?);

        unimplemented!();
    }
}
