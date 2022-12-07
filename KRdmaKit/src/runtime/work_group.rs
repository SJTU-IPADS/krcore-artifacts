use crate::runtime::error::RuntimeError;
use crate::runtime::wake_fn::waker_fn;
use crate::runtime::worker::{is_running, set_running, JoinHandle, Worker};
use std::future::Future;
use std::num::NonZeroU8;
use std::task::{Context, Poll};

pub struct WorkerGroup {
    worker: Vec<Worker>,
}

impl WorkerGroup {
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

    pub fn add_worker(&'static mut self, task_per_round: NonZeroU8) {
        let worker = Worker::new(task_per_round);
        if is_running() {
            worker.start();
        }
        self.worker.push(worker)
    }

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
