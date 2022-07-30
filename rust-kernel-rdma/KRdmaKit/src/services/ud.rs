use super::GenerateConnectionReply;
use rdma_shim::bindings::*;

#[cfg(feature = "dct")]
use super::dc::DynamicConnectedMeta;

#[allow(dead_code)]
#[repr(C, align(8))]
#[derive(Copy, Clone, Debug)]
pub struct DatagramMeta {
    // The payload's related data
    pub lid: u16,
    pub gid: ib_gid,
}

#[cfg(not(feature = "dct"))]
impl GenerateConnectionReply<DatagramMeta> for crate::queue_pairs::QueuePair {
    fn generate_connection_reply(&self) -> Result<DatagramMeta, crate::comm_manager::CMError> {
        Ok(self.get_datagram_meta()?)
    }
}

#[cfg(feature = "dct")]
impl GenerateConnectionReply<super::dc::DynamicConnectedMeta> for crate::queue_pairs::QueuePair {
    fn generate_connection_reply(
        &self,
    ) -> Result<DynamicConnectedMeta, crate::comm_manager::CMError> {
        Ok(DynamicConnectedMeta {
            datagram_addr: self.get_datagram_meta()?,
            dct_num: 0,
            dc_key: 0,
        })
    }
}

#[cfg(feature = "dct")]
pub type UnreliableDatagramAddressService =
    super::ConnectionService<DynamicConnectedMeta, crate::queue_pairs::QueuePair>;

#[cfg(not(feature = "dct"))]
pub type UnreliableDatagramAddressService =
    super::ConnectionService<DatagramMeta, crate::queue_pairs::QueuePair>;