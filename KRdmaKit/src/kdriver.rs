// This module must be only compiled in the kernel

use alloc::sync::Arc;
use alloc::vec::Vec;
use core::option::Option;

use rdma_shim::bindings::*;
use rdma_shim::ffi::c_types;
use rdma_shim::log;

pub struct KDriver {
    client: ib_client,
    rnics: Vec<crate::device::DeviceRef>,
}

pub type KDriverRef = Arc<KDriver>;

impl KDriver {
    pub fn devices(&self) -> &Vec<crate::device::DeviceRef> {
        &self.rnics
    }

    /// ! warning: this function is **not** thread safe
    pub unsafe fn create() -> Option<Arc<Self>> {
        let mut temp = Arc::new(KDriver {
            client: core::mem::MaybeUninit::zeroed().assume_init(),
            rnics: Vec::new(),
        });

        // First, we query all the ib_devices
        {
            let temp_inner = Arc::get_mut_unchecked(&mut temp);

            _NICS = Some(Vec::new());

            temp_inner.client.name = b"kRdmaKit\0".as_ptr() as *mut c_types::c_char;
            temp_inner.client.add = Some(KDriver_add_one);
            temp_inner.client.remove = Some(_KRdiver_remove_one);

            let err = ib_register_client((&mut temp_inner.client) as _);
            if err != 0 {
                return None;
            }
        }

        // next, weconstruct the nics
        // we need to do this to avoid move out temp upon constructing the devices
        let rnics = get_temp_rnics()
            .into_iter()
            .map(|dev| {
                crate::device::Device::new(*dev, &temp)
                    .expect("Query ib_device pointers should never fail")
            })
            .collect();

        // modify the temp again
        {
            let temp_inner = Arc::get_mut_unchecked(&mut temp);
            temp_inner.rnics = rnics;
            _NICS.take();
        }

        log::debug!("KRdmaKit driver initialization done. ");

        Some(temp)
    }
}

impl Drop for KDriver {
    fn drop(&mut self) {
        unsafe { ib_unregister_client(&mut self.client) };
    }
}

/* helper functions & parameters for bootstrap */
static mut _NICS: Option<Vec<*mut ib_device>> = None;

unsafe fn get_temp_rnics() -> &'static mut Vec<*mut ib_device> {
    match _NICS {
        Some(ref mut x) => &mut *x,
        None => panic!(),
    }
}

#[allow(non_snake_case)]
unsafe extern "C" fn _KRdiver_add_one(dev: *mut ib_device) {
    //    let nic = crate::device::RNIC::create(dev, 1);
    //    get_temp_rnics().push(nic.ok().unwrap());
    get_temp_rnics().push(dev);
}
rdma_shim::gen_add_dev_func!(_KRdiver_add_one, KDriver_add_one);

#[allow(non_snake_case)]
unsafe extern "C" fn _KRdiver_remove_one(dev: *mut ib_device, _client_data: *mut c_types::c_void) {
    log::info!("remove one dev {:?}", dev);
}

impl core::fmt::Debug for KDriver {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        f.debug_struct("KDriver")
            .field("num_device", &self.rnics.len())
            .finish()
    }
}