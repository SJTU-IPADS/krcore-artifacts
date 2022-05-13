//! Util libs support for gid transformation et al.

use rust_kernel_rdma_base::*;
use alloc::string::String;
use no_std_net::Guid;

/// Transform net address of type `String` into `ib_gid`
#[inline]
pub fn str_to_gid(addr: &String) -> ib_gid {
    let addr: Guid = (addr as &str).parse().unwrap();
    let mut res: ib_gid = Default::default();
    res.raw = addr.get_raw();
    res
}

/// Transform net address of type `ib_gid` into `String`
#[inline]
pub fn gid_to_str(gid: ib_gid) -> String {
    let gid: Guid = Guid::new_u8(unsafe { &gid.raw });
    alloc::format!("{}", gid)
}
