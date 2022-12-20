use crate::runtime::error::RuntimeError;
use crate::runtime::wake_fn::waker_fn;
use crate::runtime::worker::{is_running, set_running, JoinHandle, Worker};
use std::future::Future;
use std::num::NonZeroU8;
use std::task::{Context, Poll};

/// # Introduction
/// A `WorkerGroup` is a group of [`crate::runtime::worker::Worker`]'s.
/// It controls the workers' lifetime and can spawn tasks to workers.
///
/// It is required to make the WorkerGroup a static variable.
///
/// # Example
/// ```rust
/// use std::num::NonZeroU8;
/// use KRdmaKit::runtime::worker_group::WorkerGroup;
/// use KRdmaKit::runtime::yield_now;
///
/// static mut WORKER_GROUP: WorkerGroup = WorkerGroup::new();
///
/// unsafe {
/// WORKER_GROUP.add_worker(NonZeroU8::new(1).unwrap());
/// let handle = WORKER_GROUP.spawn_unchecked(0, async move { 1 });
/// WORKER_GROUP.start();
/// let ret = WORKER_GROUP.block_on(handle);
/// WORKER_GROUP.stop();
/// }
/// ```
pub struct WorkerGroup {
    worker: Vec<Worker>,
}

impl WorkerGroup {
    /// Create a new empty `WorkerGroup`, usually used to initialize a static mut WorkerGroup
    pub const fn new() -> Self {
        Self { worker: Vec::new() }
    }

    pub fn start(&'static mut self) {
        set_running(true);
        for worker in &self.worker {
            worker.start()
        }
    }

    pub fn stop(&'static mut self) {
        set_running(false);
        for worker in &self.worker {
            worker.wait_stop()
        }
    }

    /// Add one `Worker` to this `WorkerGroup`.
    ///
    /// If the `WorkerGroup` is already running, this worker will be started.
    pub fn add_worker(&'static mut self, task_per_round: NonZeroU8) {
        let worker = Worker::new(task_per_round);
        if is_running() {
            worker.start();
        }
        self.worker.push(worker)
    }

    /// Spawn a new task to a worker with its index.
    ///
    /// Return `JoinHandle` if success and error if the index if out of bound.
    pub fn spawn<F>(
        &'static self,
        worker_idx: usize,
        future: F,
    ) -> Result<JoinHandle<F::Output>, RuntimeError>
    where
        F: Future + Send + 'static,
        F::Output: Send + 'static,
    {
        self.worker
            .get(worker_idx)
            .map(|worker| worker.spawn(future))
            .ok_or(RuntimeError::Error("worker index out of bound"))
    }

    ///
    /// # Description
    /// Spawn a new task to a worker with its index. Return the task's `JoinHandle`
    ///
    /// # Safety
    /// It is unsafe to `spawn_unchecked`
    ///
    /// # Panic
    /// Panic the index if out of bound.
    pub unsafe fn spawn_unchecked<F>(
        &'static self,
        worker_idx: usize,
        future: F,
    ) -> JoinHandle<F::Output>
    where
        F: Future + Send + 'static,
        F::Output: Send + 'static,
    {
        self.worker
            .get(worker_idx)
            .map(|worker| worker.spawn(future))
            .expect("Worker index out of bound")
    }

    /// Block until the future is finished and return its output.
    pub fn block_on<F: Future>(&'static self, future: F) -> F::Output {
        pin_utils::pin_mut!(future);
        let this_thread = std::thread::current();
        let waker = waker_fn(move || this_thread.unpark());
        let cx = &mut Context::from_waker(&waker);
        loop {
            match future.as_mut().poll(cx) {
                Poll::Ready(output) => return output,
                Poll::Pending => std::thread::park(),
            }
        }
    }
}
