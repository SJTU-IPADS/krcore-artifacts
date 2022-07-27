use core::ptr::NonNull;
use alloc::boxed::Box;
use alloc::sync::Arc;

use crate::rust_kernel_rdma_base::*;
use crate::CompletionQueue;

/// Abstract the DCT target to simplify lifecycle management
/// For more, check:  https://www.openfabrics.org/images/eventpresos/workshops2014/DevWorkshop/presos/Monday/pdf/05_DC_Verbs.pdf
pub struct DynamicConnectedTarget { 
    _ctx: Arc<Context>,

    inner_target : NonNull<ib_dct>,
    send_cq: Box<CompletionQueue>,
    recv_cq : Box<CompletionQueue>,

    // FIXME: this field is not used, so I just leave it here
    shared_receive_queue : NonNull<ib_srq>, 
}

impl super::builder::QueuePairBuilder { 
    pub fn build_dynamic_connected_target(self) -> bool { 
        unimplemented!();
    }
}