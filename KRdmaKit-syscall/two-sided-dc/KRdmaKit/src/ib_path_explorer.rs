//! Path explorer helps the cm to fetch the `sa_path_rec`

use rust_kernel_rdma_base::*;
use rust_kernel_linux_util::bindings::{completion};

use crate::device::RContext;
use core::option::Option;
use crate::alloc::string::String;
use linux_kernel_module::{c_types, Error, KernelResult};
use linux_kernel_module::bindings::mutex;
use crate::consts::CONNECT_TIME_OUT_MS;

type ExplorerMutex = mutex;

pub struct IBExplorer {
    path_result: Option<sa_path_rec>,
    done: completion,
    mutex: ExplorerMutex,
}

impl IBExplorer {
    pub fn new() -> Self {
        let ret = Self {
            path_result: None,
            done: Default::default(),
            mutex: Default::default(),
        };
        ret.mutex.init();
        ret
    }

    #[inline]
    pub fn get_path_result(&self) -> Option<sa_path_rec> {
        self.path_result
    }

    // why it is unsafe? because only ib has lids
    #[inline]
    pub unsafe fn get_source_target_lid_pairs(&self) -> Option<(u32, u32)> { 
        self.path_result.map(|r| 
            (r.__bindgen_anon_1.ib.slid as _, r.__bindgen_anon_1.ib.dlid as _)
        )
    }
}


impl IBExplorer {
    #[inline]
    pub fn resolve(&mut self, service_id: u64,
                   ctx: &RContext,
                   dst_gid: &String,
                   sa_client: *mut ib_sa_client,
    ) -> KernelResult<()> {
        let dst_gid = crate::net_util::str_to_gid(dst_gid);
        self.resolve_inner(service_id, ctx, dst_gid, sa_client)
    }

    #[inline]
    fn resolve_inner(&mut self, service_id: u64,
                     ctx: &RContext,
                     dst_gid: ib_gid,
                     sa_client: *mut ib_sa_client,
    ) -> KernelResult<()> {
        self.done.init();

        let mut path_rec: sa_path_rec = Default::default();
        path_rec.dgid = dst_gid;
        path_rec.sgid = ctx.get_gid();
        path_rec.numb_path = 1;
        path_rec.service_id = service_id;

        let mut sa_query: *mut ib_sa_query = core::ptr::null_mut();
        self.mutex.lock();
        let ret = unsafe {
            // fixme: unable to handle kernel paging request when thread number is large
            ib_sa_path_rec_get(
                sa_client,
                ctx.get_raw_dev(),
                ctx.get_port(),
                &mut path_rec as *mut sa_path_rec,
                path_rec_service_id() | path_rec_dgid() | path_rec_sgid() | path_rec_numb_path(),
                CONNECT_TIME_OUT_MS as _,
                0,
                linux_kernel_module::bindings::GFP_KERNEL,
                Some(path_rec_completion),
                (self as *mut Self).cast::<c_types::c_void>(),
                &mut sa_query as *mut *mut ib_sa_query,
            )
        };
        self.mutex.unlock();

        if ret < 0 {
            linux_kernel_module::println!("call ib_sa_path_rec_get error! {:?}", Error::from_kernel_errno(ret));
            return Err(Error::from_kernel_errno(ret));
        }

        self.done.wait(crate::consts::CONNECT_TIME_OUT_MS)
    }
}

#[inline]
pub unsafe extern "C" fn path_rec_completion(
    status: linux_kernel_module::c_types::c_int,
    resp: *mut sa_path_rec,
    context: *mut linux_kernel_module::c_types::c_void,
) {
    if status != 0 {
        panic!("Failed to query the path with error {}", status);
    }

    let e = &mut *(context as *mut IBExplorer);
    e.path_result = Some(*resp);
    e.done.done();
}
