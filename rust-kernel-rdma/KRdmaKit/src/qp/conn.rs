//! Module `conn` for the request / reply in cm handshakes
#[repr(C, align(8))]
#[derive(Copy, Clone)]
pub struct Request {
    pub qd: u64,
}

impl Request {
    pub fn new(qd: u64) -> Self {
        Self { qd }
    }
}

#[repr(C, align(8))]
#[derive(Copy, Clone)]
pub struct DeRequest {
    pub qd: u64,
}

impl DeRequest {
    pub fn new(qd: u64) -> Self {
        Self { qd }
    }
}

pub mod reply {
    use crate::device::RContext;
    use rust_kernel_rdma_base::ib_gid;
    use crate::mem::TempMR;
    use crate::qp::DCTargetMeta;

    #[allow(dead_code)]
    #[repr(C, align(8))]
    #[derive(Copy, Clone, Debug)]
    pub struct Payload {
        pub qd: u64,
        pub mr: TempMR,
        // TODO: modify this struct after reconstruct RCOp
        pub status: Status,
    }

    #[repr(u16)]
    #[derive(Debug, Copy, Clone)]
    pub enum Status {
        Ok = 1,
        Err = 2,
        Nil = 3,
        // an QP with the same QD has already exists
        Exist = 4,
        // an QP not exist
        NotExist = 5,
    }

    impl Payload {
        pub fn new(qd: u64) -> Option<Self> {
            Some(Self {
                qd,
                mr: Default::default(),
                status: Status::Ok,
            })
        }

        pub fn new_from_null(qd: u64) -> Self {
            Self {
                qd,
                mr: Default::default(),
                status: Status::Nil,
            }
        }
    }


    #[allow(dead_code)]
    #[repr(C, align(8))]
    #[derive(Copy, Clone, Debug)]
    pub struct SidrPayload {
        pub qd: u64,
        pub lid: u16,
        pub gid: ib_gid,
        pub dct_num: u32,
        pub mr: TempMR,
        pub status: Status,
    }

    impl SidrPayload {
        pub fn new(qd: u64, ctx: &RContext, dct: &DCTargetMeta, status: Status) -> Option<Self> {
            Some(Self {
                qd,
                status,
                lid: ctx.get_lid(),
                gid: ctx.get_gid(),
                dct_num: dct.dct_num,
                mr: dct.mr,
            })
        }

        pub fn new_from_null(qd: u64) -> Self {
            Self {
                qd,
                status: Status::Nil,
                lid: 0,
                gid: Default::default(),
                dct_num: 0,
                mr: Default::default()
            }
        }
    }
}
