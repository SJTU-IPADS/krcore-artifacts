use super::{GenerateConnectionReply, GetQueuePairInfo};

#[allow(dead_code)]
#[repr(C, align(8))]
#[derive(Copy, Clone, Debug)]
pub struct DynamicConnectedMeta {
    pub datagram_addr: super::DatagramMeta,
    pub dct_num: u32,
    pub dc_key: u64,
}

impl GenerateConnectionReply<DynamicConnectedMeta> for crate::queue_pairs::DynamicConnectedTarget {
    fn generate_connection_reply(
        &self,
    ) -> Result<DynamicConnectedMeta, crate::comm_manager::CMError> {
        Ok(DynamicConnectedMeta {
            datagram_addr: self.get_datagram_meta()?,
            dct_num: self.dct_num(),
            dc_key: self.dc_key(),
        })
    }
}

/// A dummy implementation of the trait
/// DCT metadata doesn't really need this
impl GetQueuePairInfo for crate::queue_pairs::DynamicConnectedTarget {
    fn get_qkey(&self) -> u32 {
         0
    }

    fn get_qp_num(&self) -> u32 {
        0
    }
}

pub type DCTargetService =
    super::ConnectionService<DynamicConnectedMeta, crate::queue_pairs::DynamicConnectedTarget>;
