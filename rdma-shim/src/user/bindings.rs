pub use rust_user_rdma::*;

/// Warn!!!!: these bindings currently work with Mellanox OFED 4_9
/// will include other bindings later
///
/// In the future, we will avoid adding these bindings
/// by re-bind the interfaces from the kernel space to the user-space ones
///
#[allow(non_camel_case_types)]
mod wrapper_types {
    use super::*;
    #[allow(unused_imports)]
    use crate::ffi::c_types::*;

    /// We follow the convention of the kernel to rename the user-space verbs types
    /// types:
    pub type ib_device = ibv_device;
    pub type ib_device_attr = ibv_device_attr;
    pub type ib_port_attr = ibv_port_attr;

    pub type ib_pd = ibv_pd;
    pub type ib_ah = ibv_ah;
    pub type ib_ah_attr = ibv_ah_attr;
    pub type ib_gid = ibv_gid;

    pub type rdma_ah_attr = ibv_ah_attr;

    // CQ related types
    pub type ib_cq = ibv_cq;
    pub type ib_srq = ibv_srq;
    pub type ib_wc = ibv_wc;
    pub type ib_srq_init_attr = ibv_srq_init_attr;

    // MR related types
    pub mod ib_access_flags {
        use super::ibv_access_flags;

        // FIXME: hard-coded support is ok?
        pub type Type = u32;

        pub const IB_ACCESS_LOCAL_WRITE: Type = ibv_access_flags::IBV_ACCESS_LOCAL_WRITE.0;
        pub const IB_ACCESS_REMOTE_WRITE: Type = ibv_access_flags::IBV_ACCESS_REMOTE_WRITE.0;
        pub const IB_ACCESS_REMOTE_READ: Type = ibv_access_flags::IBV_ACCESS_REMOTE_READ.0;
        pub const IB_ACCESS_REMOTE_ATOMIC: Type = ibv_access_flags::IBV_ACCESS_REMOTE_ATOMIC.0;
        pub const IB_ACCESS_MW_BIND: Type = ibv_access_flags::IBV_ACCESS_MW_BIND.0;
        pub const IB_ACCESS_ZERO_BASED: Type = ibv_access_flags::IBV_ACCESS_ZERO_BASED.0;
        pub const IB_ACCESS_ON_DEMAND: Type = ibv_access_flags::IBV_ACCESS_ON_DEMAND.0;
    }

    // QP related types
    pub type ib_qp = ibv_qp;
    pub mod ib_qp_state {
        pub use super::ibv_qp_state::*;

        pub const IB_QPS_RESET: Type = IBV_QPS_RESET;
        pub const IB_QPS_INIT: Type = IBV_QPS_INIT;
        pub const IB_QPS_RTR: Type = IBV_QPS_RTR;
        pub const IB_QPS_RTS: Type = IBV_QPS_RTS;
        pub const IB_QPS_SQD: Type = IBV_QPS_SQD;
        pub const IB_QPS_SQE: Type = IBV_QPS_SQE;
        pub const IB_QPS_ERR: Type = IBV_QPS_ERR;
        pub const IB_QPS_UNKNOWN: Type = IBV_QPS_UNKNOWN;
    }

    pub mod ib_mtu {
        pub use super::ibv_mtu::*;
        pub const IB_MTU_256: Type = IBV_MTU_256;
        pub const IB_MTU_512: Type = IBV_MTU_512;
        pub const IB_MTU_1024: Type = IBV_MTU_1024;
        pub const IB_MTU_2048: Type = IBV_MTU_2048;
        pub const IB_MTU_4096: Type = IBV_MTU_4096;
    }

    pub mod ib_qp_type {
        pub use super::ibv_qp_type::*;

        pub const IB_QPT_RC: Type = IBV_QPT_RC;
        pub const IB_QPT_UC: Type = IBV_QPT_UC;
        pub const IB_QPT_UD: Type = IBV_QPT_UD;

        #[cfg(feature = "dct")]
        pub const IB_QPT_XRC: Type = IBV_QPT_XRC;

        #[cfg(feature = "dct")]
        pub const IB_QPT_RAW_PACKET: Type = IBV_QPT_RAW_PACKET;

        #[cfg(feature = "dct")]
        pub const IB_QPT_RAW_ETH: Type = IBV_QPT_RAW_ETH;
        pub const IB_QPT_XRC_SEND: Type = IBV_QPT_XRC_SEND;
        pub const IB_QPT_XRC_RECV: Type = IBV_QPT_XRC_RECV;

        #[cfg(feature = "dct")]
        pub const IB_EXP_QP_TYPE_START: Type = IBV_EXP_QP_TYPE_START;
        
        #[cfg(feature = "dct")]        
        pub const IB_EXP_QPT_DC_INI: Type = IBV_EXP_QPT_DC_INI;
    }

    pub mod ib_qp_attr_mask {
        pub use super::ibv_qp_attr_mask::*;

        pub const IB_QP_STATE: Type = IBV_QP_STATE;
        pub const IB_QP_CUR_STATE: Type = IBV_QP_CUR_STATE;
        pub const IB_QP_EN_SQD_ASYNC_NOTIFY: Type = IBV_QP_EN_SQD_ASYNC_NOTIFY;
        pub const IB_QP_ACCESS_FLAGS: Type = IBV_QP_ACCESS_FLAGS;
        pub const IB_QP_PKEY_INDEX: Type = IBV_QP_PKEY_INDEX;
        pub const IB_QP_PORT: Type = IBV_QP_PORT;
        pub const IB_QP_QKEY: Type = IBV_QP_QKEY;
        pub const IB_QP_AV: Type = IBV_QP_AV;
        pub const IB_QP_PATH_MTU: Type = IBV_QP_PATH_MTU;
        pub const IB_QP_TIMEOUT: Type = IBV_QP_TIMEOUT;
        pub const IB_QP_RETRY_CNT: Type = IBV_QP_RETRY_CNT;
        pub const IB_QP_RNR_RETRY: Type = IBV_QP_RNR_RETRY;
        pub const IB_QP_RQ_PSN: Type = IBV_QP_RQ_PSN;
        pub const IB_QP_MAX_QP_RD_ATOMIC: Type = IBV_QP_MAX_QP_RD_ATOMIC;
        pub const IB_QP_ALT_PATH: Type = IBV_QP_ALT_PATH;
        pub const IB_QP_MIN_RNR_TIMER: Type = IBV_QP_MIN_RNR_TIMER;
        pub const IB_QP_SQ_PSN: Type = IBV_QP_SQ_PSN;
        pub const IB_QP_MAX_DEST_RD_ATOMIC: Type = IBV_QP_MAX_DEST_RD_ATOMIC;
        pub const IB_QP_PATH_MIG_STATE: Type = IBV_QP_PATH_MIG_STATE;
        pub const IB_QP_CAP: Type = IBV_QP_CAP;
        pub const IB_QP_DEST_QPN: Type = IBV_QP_DEST_QPN;
    }

    pub type ib_send_wr = ibv_send_wr;
    pub type ib_rdma_wr = ibv_send_wr;
    pub type ib_ud_wr = ibv_send_wr;
    pub type ib_sge = ibv_sge;
    pub type ib_recv_wr = ibv_recv_wr;
    pub type ib_qp_attr = ibv_qp_attr;
    pub type ib_qp_init_attr = ibv_qp_init_attr;
    pub type ib_qp_cap = ibv_qp_cap;

    /// functions
    #[inline(always)]
    pub unsafe fn ib_dealloc_pd(pd: *mut ib_pd) {
        ibv_dealloc_pd(pd);
    }

    #[inline(always)]
    pub unsafe fn rdma_create_ah_wrapper(pd: *mut ib_pd, attr: *mut ib_ah_attr) -> *mut ib_ah {
        ibv_create_ah(pd, attr)
    }

    #[inline(always)]
    pub unsafe fn rdma_destroy_ah(ah: *mut ib_ah) {
        ibv_destroy_ah(ah);
    }

    #[inline(always)]
    pub unsafe fn ib_free_cq(cq: *mut ib_cq) {
        ibv_destroy_cq(cq);
    }

    #[inline(always)]
    pub unsafe fn ib_destroy_srq(srq: *mut ib_srq) {
        ibv_destroy_srq(srq);
    }

    #[inline(always)]
    pub unsafe fn ib_create_srq(pd: *mut ib_pd, attr: *mut ib_srq_init_attr) -> *mut ib_srq {
        ibv_create_srq(pd, attr)
    }

    #[inline(always)]
    pub unsafe fn ib_destroy_qp(qp: *mut ib_qp) -> c_int {
        ibv_destroy_qp(qp)
    }

    #[inline(always)]
    pub unsafe fn ib_query_qp(
        qp: *mut ib_qp,
        attr: *mut ib_qp_attr,
        attr_mask: c_int,
        init_attr: *mut ib_qp_init_attr,
    ) -> c_int {
        ibv_query_qp(qp, attr, attr_mask, init_attr)
    }

    #[inline(always)]
    pub unsafe fn ib_modify_qp(qp: *mut ib_qp, attr: *mut ib_qp_attr, attr_mask: c_int) -> c_int {
        ibv_modify_qp(qp, attr, attr_mask)
    }

    #[inline(always)]
    pub unsafe fn ib_create_qp(pd: *mut ib_pd, qp_init_attr: *mut ib_qp_init_attr) -> *mut ib_qp {
        ibv_create_qp(pd, qp_init_attr)
    }
}

pub use wrapper_types::*;

#[allow(unused_unsafe)]
pub unsafe fn ptr_is_err<T>(ptr: *mut T) -> super::ffi::c_types::c_int {
    if ptr.is_null() {
        1
    } else {
        0
    }
}
