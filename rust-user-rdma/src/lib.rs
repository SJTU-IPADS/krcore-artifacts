#![allow(deref_nullptr)] // TODO(fxbug.dev/74605): Remove once bindgen is fixed.
#![allow(non_snake_case, non_camel_case_types, non_upper_case_globals)]

#[allow(unused_imports)]
use libc::*;

use no_std_net::Guid;

include!(concat!(env!("OUT_DIR"), "/bindings.rs"));

impl Default for ibv_ah_attr {
    fn default() -> Self {
        unsafe { core::mem::zeroed() }
    }
}

impl Default for ibv_device_attr { 
    fn default() -> Self {
        unsafe { core::mem::zeroed() }
    }
}

impl Default for ibv_port_attr { 
    fn default() -> Self {
        unsafe { core::mem::zeroed() }
    }
}

impl Default for ibv_gid { 
    fn default() -> Self {
        unsafe { core::mem::zeroed() }
    }
}

impl core::fmt::Debug for ibv_device_attr {
    /// print the device attr, the detailed fields can be found at:
    /// https://www.rdmamojo.com/2012/07/13/ibv_query_device/
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        f.debug_struct("ibv_device_attr")
            .field("max_qp_rd_atom", &self.max_qp_rd_atom)
            .finish() // TODO: more fields to be added
    }
}

impl core::fmt::Debug for ibv_port_attr {
    /// print the port attr, the detailed fields can be found at:
    /// https://www.rdmamojo.com/2012/07/21/ibv_query_port/
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        f.debug_struct("ibv_port_attr")
            .field("lid", &self.lid)
            .field("state", &self.state)
            .field("max_mtu", &self.max_mtu)
            .field("max_msg_sz", &self.max_msg_sz)
            .field("link_layer", &self.link_layer)
            .finish() // TODO: more fields to be added
    }
}

impl core::fmt::Debug for ibv_gid {
    /// print the gid, refer to 
    /// https://www.rdmamojo.com/2012/08/02/ibv_query_gid/
    fn fmt(&self, fmt: &mut ::core::fmt::Formatter) -> core::fmt::Result {
        let guid = Guid::new_u8(unsafe { &self.raw.as_ref() });
        write!(fmt, "{}", guid)
    }
}

#[cfg(test)]
mod tests {
    #[test]
    fn has_rdma() {
        let mut n = 0i32;
        let devices = unsafe { super::ibv_get_device_list(&mut n as *mut _) };

        // should have devices
        assert!(!devices.is_null());
    }
}
