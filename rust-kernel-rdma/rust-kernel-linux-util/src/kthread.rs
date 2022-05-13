extern crate alloc;

use alloc::string::{String, ToString};

use crate::bindings::{task_struct, bd_kthread_run, bd_kthread_stop, bd_kthread_should_stop, bd_schedule, bd_ssleep, bd_kthread_bind, bd_kthread_create, bd_get_cpu_id, bd_wake_up_process};
use crate::linux_kernel_module::{KernelResult, Error};
use crate::linux_kernel_module::c_types::c_void;

/// Yield the cpu
/// 
/// ## Usage
/// In a kernel loop, `yield_now` should be used to avoid CPU soft lockup
/// 
pub fn yield_now() {
    unsafe {
        bd_schedule()
    };
}

/// Return true when someone calls `kthread_stop` on the thread
/// 
/// ## Usage
/// Any joinable thread must ensure that `should_stop` returns true
/// before exiting
/// 
pub fn should_stop() -> bool {
    unsafe {
        bd_kthread_should_stop()
    }
}

/// Return the id of the cpu where the current kthread is running on
///
pub fn get_cpu_id() -> u32 {
    unsafe {
        bd_get_cpu_id()
    }
}

/// Sleep for some time (in seconds)
/// 
/// ## Usage
/// Sleep for assigned time
/// 
pub fn sleep(seconds: u32) {
    unsafe {
        bd_ssleep(seconds)
    }
}

pub struct JoinHandler {
    task: *mut task_struct,
}

/// Join the corresponding kthread
/// 
/// ## Usage
/// Wait the corresponding kthread to exit,
/// returns the returned value of the kthread
/// 
/// ```
/// let handler = thread::Builder::new()
///                 .set_xxxx()
///                 .spawn(func)
/// let ret = handler.join()
/// ```
/// 
/// `join` will cause kernel bug if the corresponding
/// kthread has already exited.
/// 
impl JoinHandler {
    pub fn new(task: *mut task_struct) -> Self {
        Self {
            task: task
        }
    }

    pub fn join(self) -> i32 {
        unsafe {
            bd_kthread_stop(self.task)
        }
    }
}

pub struct Builder {
    name: String,
    parameter: *mut c_void,
    cpu_binding: Option<u32>,
}

impl Default for Builder {
    fn default() -> Self {
        Self {
            name: "\0".to_string(),
            parameter: 0 as *mut c_void,
            cpu_binding: None,
        }
    }
}

/// A builder for kthread
/// 
/// ## Usage
/// 
/// A builder for spawn kthreads
/// 
/// ```
/// extern "C" fn sample_func(_param: *mut c_void) -> i32 {
///     println!("hello world from kthread!");
///     0
/// }
/// 
/// let handler = thread::Builder::new()
///                 .set_name("hello".to_string())
///                 .set_parameter(my_ptr)
///                 .spawn(sample_func);
/// 
/// assert!(handler.is_ok());
/// ```
/// 
/// `bind` can be used to bind the spawned kthread to
/// a specific cpu
/// 
/// ```
/// let target_cpu_id: u32 = 0;
/// let handler = thread::Builder::new()
///                 .set_name("hello".to_string())
///                 .set_parameter(my_ptr)
///                 .bind(target_cpu_id)
///                 .spawn(sample_func);
/// 
/// assert!(handler.is_ok());
/// ```
/// 
impl Builder {
    pub fn new() -> Self {
        Default::default()
    }

    pub fn set_name(mut self, name: String) -> Self {
        self.name = name;
        self.name.push('\0');
        self
    }

    pub fn set_parameter(mut self, parameter: *mut c_void) -> Self {
        self.parameter = parameter;
        self
    }

    pub fn bind(mut self, cpu_id: u32) -> Self {
        self.cpu_binding = Some(cpu_id);
        self
    }

    pub fn spawn(self, body: extern "C" fn(*mut c_void) -> i32) -> KernelResult<JoinHandler> {
        if self.cpu_binding.is_none() {
            return self.spawn_wo_bind(body);
        } else {
            let cpu_id = *self.cpu_binding.as_ref().unwrap();
            return self.spawn_and_bind(body, cpu_id);
        }
    }

    fn spawn_wo_bind(self, body: extern "C" fn(*mut c_void) -> i32) -> KernelResult<JoinHandler> {
        let task = unsafe {
            bd_kthread_run(
                Some(body),
                self.parameter as *mut c_void,
                self.name.as_bytes().as_ptr() as *const i8)
        };
        if task.is_null() {
            return Err(Error::from_kernel_errno(0));
        }
        Ok(JoinHandler::new(task))
    }

    fn spawn_and_bind(self, body: extern "C" fn(*mut c_void) -> i32, cpu: u32) -> KernelResult<JoinHandler> {
        let task = unsafe {
            bd_kthread_create(
                Some(body),
                self.parameter as *mut c_void,
                self.name.as_bytes().as_ptr() as *const i8)
        };

        if task.is_null() {
            return Err(Error::from_kernel_errno(0));
        }

        unsafe {
            bd_kthread_bind(task, cpu);
            bd_wake_up_process(task);
        }
        Ok(JoinHandler::new(task))
    }
}