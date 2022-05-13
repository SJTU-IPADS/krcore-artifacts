use rust_kernel_rdma_base::*;
use rust_kernel_linux_util::bindings::bd_cpu_to_be64;
use linux_kernel_module::bindings::*;
use linux_kernel_module::{Error, println};

use core::option::Option;
use crate::qp::conn::reply;
use crate::rust_kernel_rdma_base::linux_kernel_module::KernelResult;


/// Client CM, the abstraction of `cm_id`
#[derive(Debug)]
pub struct ClientCM {
    inner_cm: *mut ib_cm_id,
}

fn create_cm_id(
    hca: *mut ib_device,
    cm_handler: ib_cm_handler,
    context: *mut linux_kernel_module::c_types::c_void,
) -> Option<*mut ib_cm_id> {
    let cm = unsafe { ib_create_cm_id(hca, cm_handler, context) };
    if cm.is_null() {
        println!("create cm_id error!");
        return None;
    }
    Some(cm)
}


#[allow(dead_code)]
impl ClientCM {
    pub fn new(
        hca: *mut ib_device,
        cm_handler: ib_cm_handler,
        context: *mut linux_kernel_module::c_types::c_void,
    ) -> Option<Self> {
        create_cm_id(hca, cm_handler, context).map(|cm| Self { inner_cm: cm })
    }

    #[inline]
    pub fn get_cm(&self) -> *mut ib_cm_id {
        self.inner_cm
    }

    #[inline]
    pub fn status(&self) -> ib_cm_state {
        unsafe { *self.get_cm() }.state
    }

    /// when in server side, one could listen to the `id`
    #[inline]
    pub fn listen(&mut self, id: u64) -> linux_kernel_module::KernelResult<()> {
        let err = unsafe { ib_cm_listen(self.inner_cm, bd_cpu_to_be64(id), 0) };
        if err != 0 {
            return Err(Error::from_kernel_errno(err));
        }
        Ok(())
    }
}

impl ClientCM {
    #[inline]
    pub fn new_from_raw(cm: *mut ib_cm_id) -> Self {
        Self { inner_cm: cm }
    }

    #[inline]
    pub fn set_inner_cm(&mut self, cm_id: *mut ib_cm_id) {
        self.inner_cm = cm_id;
    }

    #[inline]
    pub fn reset(&mut self) {
        if !self.inner_cm.is_null() {
            unsafe { ib_destroy_cm_id(self.inner_cm) };
        }
        self.inner_cm = core::ptr::null_mut();
    }

    #[inline]
    pub fn set_context(&mut self, val: u64) {
        unsafe { (*self.inner_cm).context = val as *mut linux_kernel_module::c_types::c_void };
    }

    /// Move the CM out of it. Use this function when delete register connections in `RCtrl`
    /// Also will reset the CM, this is important
    #[inline]
    pub fn handoff_raw_cm(&mut self) -> *mut ib_cm_id {
        let ret = self.get_cm();
        self.inner_cm = core::ptr::null_mut(); // avoid being freed if the QP is freed
        ret
    }
}


/// Managed communication methods. The wrapper for
/// `ib_send_cm_rep`,  `ib_send_cm_req`, `ib_send_cm_dreq`, `send_drep`,
/// `send_rtu`
impl ClientCM {
    pub fn send_reply<T: Sized>(
        &self,
        mut rep: ib_cm_rep_param,
        mut pri: T,
    ) -> linux_kernel_module::KernelResult<()> {
        rep.private_data = ((&mut pri) as *mut T).cast::<linux_kernel_module::c_types::c_void>();
        rep.private_data_len = core::mem::size_of::<T>() as u8;

        let err = unsafe { ib_send_cm_rep(self.get_cm(), &mut rep as *mut _) };
        if err != 0 {
            println!(
                "CM send reply error: {:?}",
                linux_kernel_module::Error::from_kernel_errno(err)
            );
            return Err(linux_kernel_module::Error::from_kernel_errno(err));
        }
        Ok(())
    }

    pub fn send_req<T: Sized>(
        &mut self,
        mut req: ib_cm_req_param,
        mut pri: T,
    ) -> linux_kernel_module::KernelResult<()> {
        req.private_data = ((&mut pri) as *mut T).cast::<linux_kernel_module::c_types::c_void>();
        req.private_data_len = core::mem::size_of::<T>() as u8;

        let err = unsafe { ib_send_cm_req(self.get_cm(), &mut req as *mut _) };
        if err != 0 {
            println!(
                "CM send request error: {:?}",
                linux_kernel_module::Error::from_kernel_errno(err)
            );
            return Err(linux_kernel_module::Error::from_kernel_errno(err));
        }
        Ok(())
    }

    pub fn send_dreq<T: Sized>(&mut self, mut pri: T) -> linux_kernel_module::KernelResult<()> {
        let err = unsafe {
            ib_send_cm_dreq(
                self.get_cm(),
                (&mut pri as *mut _) as *mut linux_kernel_module::c_types::c_void,
                core::mem::size_of::<T>() as u8,
            )
        };
        if err != 0 {
            println!(
                "CM send dereq error: {:?}",
                linux_kernel_module::Error::from_kernel_errno(err)
            );
            return Err(linux_kernel_module::Error::from_kernel_errno(err));
        }
        Ok(())
    }

    pub fn send_drep(&mut self) -> linux_kernel_module::KernelResult<()> {
        let err = unsafe { ib_send_cm_drep(self.get_cm(), core::ptr::null_mut(), 0) };
        if err != 0 {
            println!(
                "CM send derep error: {:?}",
                linux_kernel_module::Error::from_kernel_errno(err)
            );
            return Err(linux_kernel_module::Error::from_kernel_errno(err));
        }
        Ok(())
    }

    pub fn send_rtu<T: Sized>(&mut self, mut pri: T) -> linux_kernel_module::KernelResult<()> {
        // println!("[client cm] start send cm rtu req");
        let err = unsafe {
            ib_send_cm_rtu(
                self.get_cm(),
                (&mut pri as *mut T).cast::<linux_kernel_module::c_types::c_void>(),
                core::mem::size_of::<T>() as u8,
            )
        };
        if err != 0 {
            println!(
                "CM send RTU error: {:?}",
                linux_kernel_module::Error::from_kernel_errno(err)
            );
            return Err(linux_kernel_module::Error::from_kernel_errno(err));
        }
        Ok(())
    }
}

/// Sidr req and rep, for UD communications.
/// Wrapper for `ib_send_cm_sidr_req`, `ib_send_cm_sidr_rep`
impl ClientCM {
    pub fn send_sidr<T: Sized>(
        &mut self,
        mut req: ib_cm_sidr_req_param,
        mut pri: T,
    ) -> linux_kernel_module::KernelResult<()> {
        req.private_data = ((&mut pri) as *mut T).cast::<linux_kernel_module::c_types::c_void>();
        req.private_data_len = core::mem::size_of::<T>() as u8;

        let err = unsafe { ib_send_cm_sidr_req(self.get_cm(), &mut req as *mut _) };
        if err != 0 {
            println!(
                "CM send sidr request error: {:?}",
                linux_kernel_module::Error::from_kernel_errno(err)
            );
            return Err(linux_kernel_module::Error::from_kernel_errno(err));
        }
        Ok(())
    }

    /// Static method for sidr reply
    pub fn ud_conn_reply(ib_cm: *mut ib_cm_id, qpn: u32, qkey: u32,
                         reply: reply::SidrPayload) -> KernelResult<()> {
        let mut rep: ib_cm_sidr_rep_param = Default::default();
        let mut client_cm = ClientCM::new_from_raw(ib_cm);
        rep.qp_num = qpn;
        rep.qkey = qkey as u32; // should let qkey of two side the same
        rep.status = ib_cm_sidr_status::IB_SIDR_SUCCESS;
        client_cm.send_sidr_reply(rep, reply)
    }

    fn send_sidr_reply<T: Sized>(
        &mut self,
        mut rep: ib_cm_sidr_rep_param,
        mut info: T,
    ) -> linux_kernel_module::KernelResult<()> {
        println!("[client cm] start send sidr reply");
        rep.info = ((&mut info) as *mut T).cast::<linux_kernel_module::c_types::c_void>();
        rep.info_length = core::mem::size_of::<T>() as u8;

        let err = unsafe { ib_send_cm_sidr_rep(self.get_cm(), &mut rep as *mut _) };
        if err != 0 {
            println!(
                "CM send sidr reply error: {:?}",
                linux_kernel_module::Error::from_kernel_errno(err)
            );
            return Err(linux_kernel_module::Error::from_kernel_errno(err));
        }
        Ok(())
    }
}


impl Default for ClientCM {
    fn default() -> Self {
        Self {
            inner_cm: core::ptr::null_mut(),
        }
    }
}