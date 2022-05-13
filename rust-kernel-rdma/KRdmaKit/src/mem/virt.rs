use crate::rust_kernel_rdma_base::*;
use crate::mem::{RegAttr, Memory};

use crate::device::RContext;

/// The rdma memory for large memory region register.
///
/// Usage as below:
///         let ptr = unsafe { virmalloc::<i8>(4096) };
///         let mem = RMem::create(ctx, ptr, 4096);
///         let mr = mem.get_inner_mr();
///         send_reg_mr(qp, mr);
///         unsafe { vfree(ptr)} ;
pub struct RMemVirt {
    // the start address (virtual)
    buf: *mut i8,
    // dma buf of physical address, used for RDMA operations.
    dma_buf: u64,
    // registered mr ref. Note that we have to call `send_reg_mr` after init `PBlock` to register this mr!
    mr: *mut ib_mr,
    // total block size. It should be `page_size` * `pgn`
    capacity: u64,
}

use linux_kernel_module::println;
use alloc::boxed::Box;
use crate::rust_kernel_rdma_base::linux_kernel_module::KernelResult;

impl RMemVirt {
    pub fn create(ctx: &RContext, base_addr: *mut i8, size: u64) -> Self {
        let pd = ctx.get_pd();
        let mr: *mut ib_mr;
        let mut sge_offsets: u32 = 0;

        // TODO:: check the page size, what if it is a huge page?
        let pg_sz = unsafe { page_size() } as u64;
        let entry_len = if size % pg_sz != 0 {
            1 + (size / pg_sz)
        } else {
            size / pg_sz
        };
        // alloc
        let mut sg_list: Box<[core::mem::MaybeUninit<scatterlist>], VmallocAllocator> =
            Box::new_zeroed_slice_in(entry_len as usize, VmallocAllocator);


        let sg_ptr = sg_list[0].as_mut_ptr();
        // first element address
        unsafe { sg_init_table(sg_ptr, entry_len as u32) };
        // params
        let sg_en_sz = pg_sz as u64;   // 4096B as default
        let offset = 0;
        for i in 0..entry_len {
            let sg = sg_list[i as usize].as_mut_ptr();
            let cur_addr = (base_addr as u64 + i * pg_sz) as *mut i8;

            // when `base_addr` is allocated by `vmalloc`, transform by `vmalloc_to_page`.
            // And if created by `kmalloc`, transform by `virt_to_page`.
            // vmalloc_to_page is slow, so don't put it in the critical path!
            let pages = unsafe { vmalloc_to_page(cur_addr.cast::<linux_kernel_module::c_types::c_void>()) };
            // set page value
            unsafe { bd_sg_set_page(sg, pages, pg_sz as u32, offset) };
        }

        // alloc one mr
        mr = unsafe { ib_alloc_mr(pd, ib_mr_type::IB_MR_TYPE_MEM_REG, entry_len as u32) };
        // map sg
        let n = unsafe { bd_ib_dma_map_sg((*pd).device, sg_ptr, entry_len as i32, dma_from_device()) };
        if n < 0 {
            println!("error when dma map sg!");
        }
        let n = unsafe {
            ib_map_mr_sg(mr, sg_ptr, entry_len as i32, &mut sge_offsets as *mut _,
                         sg_en_sz as u32)
        };
        if n < 0 {
            println!("error when map mr sg!");
        }

        let dma_buf = unsafe { *sg_ptr }.dma_address as u64;

        let entry_sz = sg_en_sz as u64;
        Self {
            buf: base_addr,
            mr,
            dma_buf,
            // since align to entry size, the capacity may large than the given `size`
            capacity: entry_sz * entry_len as u64,
        }
    }

    /**
     *  let mr = unsafe { ib_alloc_mr(pd, ib_mr_type::IB_MR_TYPE_MEM_REG, max_entry_len as u32) };
     */
    pub fn create_from_mr(ctx: &RContext, mr: *mut ib_mr, base_addr: *mut i8, size: u64) -> Self {
        let pd = ctx.get_pd();
        let mut sge_offsets: u32 = 0;

        let pg_sz = unsafe { page_size() } as u64;
        let entry_len = if size % pg_sz != 0 {
            1 + (size / pg_sz)
        } else {
            size / pg_sz
        };
        // alloc
        let mut sg_list: Box<[core::mem::MaybeUninit<scatterlist>], VmallocAllocator> =
            Box::new_zeroed_slice_in(entry_len as usize, VmallocAllocator);


        let sg_ptr = sg_list[0].as_mut_ptr();
        // first element address
        unsafe { sg_init_table(sg_ptr, entry_len as u32) };
        // params
        let sg_en_sz = pg_sz as u64;   // 4096B as default
        let offset = 0;
        for i in 0..entry_len {
            let sg = sg_list[i as usize].as_mut_ptr();
            let cur_addr = (base_addr as u64 + i * pg_sz) as *mut i8;

            // when `base_addr` is allocated by `vmalloc`, transform by `vmalloc_to_page`.
            // And if created by `kmalloc`, transform by `virt_to_page`.
            // vmalloc_to_page is slow, so don't put it in the critical path!
            let pages = unsafe { vmalloc_to_page(cur_addr.cast::<linux_kernel_module::c_types::c_void>()) };
            // set page value
            unsafe { bd_sg_set_page(sg, pages, pg_sz as u32, offset) };
        }

        // map sg
        let n = unsafe { bd_ib_dma_map_sg((*pd).device, sg_ptr, entry_len as i32, dma_from_device()) };
        if n < 0 {
            println!("error when dma map sg!");
        }
        let n = unsafe {
            ib_map_mr_sg(mr, sg_ptr, entry_len as i32, &mut sge_offsets as *mut _,
                         sg_en_sz as u32)
        };
        if n < 0 {
            println!("error when map mr sg!");
        }

        let dma_buf = unsafe { *sg_ptr }.dma_address as u64;

        let entry_sz = sg_en_sz as u64;
        Self {
            buf: base_addr,
            mr,
            dma_buf,
            // since align to entry size, the capacity may large than the given `size`
            capacity: entry_sz * entry_len as u64,
        }
    }
}

impl Memory for RMemVirt {
    fn get_ptr(&self) -> *mut i8 {
        self.buf
    }

    fn get_pa(&mut self, off: u64) -> u64 {
        self.get_dma_buf() + off
    }

    fn get_sz(&self) -> u64 {
        self.capacity
    }

    fn pin_for_rdma(&mut self, _ctx: &RContext) -> KernelResult<()> {
        unimplemented!();
    }
}

impl RMemVirt {
    /// Get the physical dma address (used for RDMA operations)
    #[inline]
    pub fn get_dma_buf(&self) -> u64 {
        self.dma_buf
    }

    #[inline]
    pub fn get_inner_mr(&self) -> *mut ib_mr {
        self.mr
    }

    #[inline]
    pub fn get_mr_pd(&self) -> *mut ib_pd { unsafe { (*self.mr).pd } }
    /// Get the virtual address
    #[inline]
    pub fn get_va_buf(&self) -> u64 {
        self.buf as u64
    }

    #[inline]
    pub fn extract_reg_attr(&self) -> RegAttr {
        RegAttr {
            buf: self.dma_buf,
            size: self.capacity,
            lkey: unsafe { (*self.mr).lkey },
            rkey: unsafe { (*self.mr).rkey },
        }
    }
}


impl Drop for RMemVirt {
    fn drop(&mut self) {
        if !self.mr.is_null() {
            unsafe { ib_dereg_mr(self.mr) };
        }
    }
}