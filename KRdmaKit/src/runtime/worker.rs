use crate::runtime::task::{set_current_task_id, Task, TaskWaker};
use crate::runtime::tid::*;
use crate::runtime::waitable::WaitableWrapper;
use crossbeam::queue::ArrayQueue;
use futures::channel::oneshot::channel;
use std::cell::RefCell;
use std::collections::BTreeMap;
use std::future::Future;
use std::num::NonZeroU8;
use std::pin::Pin;
use std::sync::Arc;
use std::task::{Context, Waker};

static mut RUNNING: bool = false;

thread_local! {
    static WAITING: RefCell<BTreeMap<TaskId, WaitableWrapper>> = RefCell::new(BTreeMap::new());
}

#[inline]
pub(super) fn add_waitable(task_id: TaskId, waitable_wrapper: WaitableWrapper) {
    WAITING.with(|waiting| {
        waiting.borrow_mut().insert(task_id, waitable_wrapper);
    })
}

#[inline(always)]
pub(super) fn set_running(running: bool) {
    unsafe { RUNNING = running }
}

#[inline(always)]
pub(super) fn is_running() -> bool {
    unsafe { RUNNING }
}

pub type JoinHandle<R> = Pin<Box<dyn Future<Output = R> + Send>>;

/// This is the core struct of this runtime. A `Worker` will start after the [`Worker::start`] method is called.
///
/// One worker will poll the tasks spawned to it in a main loop. Between one or several rounds
/// of loop, the worker tries to wait one round of the `Waitable` object to see if the task is ready
/// to move to the next stage or not. Ready tasks will be re-added to the scheduling queue.
pub struct Worker {
    join_handle: RefCell<Option<std::thread::JoinHandle<()>>>,
    spawner: Arc<ArrayQueue<Task>>,
    task_id_gen: TaskIdGenerator,
    task_per_round: NonZeroU8,
}

impl Worker {
    /// Create a new worker with is maximum capacity of scheduling queue to be 1024
    pub fn new(task_per_round: NonZeroU8) -> Self {
        Self {
            join_handle: RefCell::new(None),
            spawner: Arc::new(ArrayQueue::new(1024)),
            task_id_gen: TaskIdGenerator::new(),
            task_per_round,
        }
    }

    /// Start this worker by starting another thread. In that thread, this worker will
    /// be busy trying to pop a ready task from its scheduling queue.
    ///
    /// # Panic
    /// Panic if the worker is already running. A Worker cannot be started for multiple times
    pub fn start(&self) {
        let mut opt_handle = self.join_handle.borrow_mut();
        if opt_handle.is_some() {
            panic!("The worker is running!")
        }
        let task_per_round = self.task_per_round.get();
        let spawner = self.spawner.clone();
        let handle = std::thread::spawn(move || {
            let spawner = spawner;
            let queue = Arc::new(ArrayQueue::<TaskId>::new(1024));
            let tasks = RefCell::new(BTreeMap::<TaskId, (Task, Waker)>::new());

            loop {
                if !is_running() {
                    WAITING.with(|waiting| waiting.borrow_mut().clear());
                    break;
                }
                for _ in 0..1_000_000 {
                    for _ in 0..task_per_round {
                        Self::find_task(&spawner, &queue, &tasks).map(|task_id| {
                            let mut tasks_ref = tasks.borrow_mut();
                            match tasks_ref.get_mut(&task_id) {
                                Some((task, waker)) => {
                                    set_current_task_id(task_id);
                                    let mut ctx = Context::from_waker(waker);
                                    if task.poll(&mut ctx).is_ready() {
                                        tasks_ref.remove(&task_id);
                                    }
                                }
                                _ => {}
                            }
                        });
                    }

                    WAITING.with(|waiting| {
                        let mut waiting_ref = waiting.borrow_mut();
                        let mut tasks_ref = tasks.borrow_mut();
                        let mut vec = Vec::new();
                        for (task_id, waitable) in waiting_ref.iter_mut() {
                            let status = waitable.inner.wait_one_round();
                            if status.is_ready() {
                                waitable.set_flag(true);
                                vec.push(*task_id);
                            } else if status.is_error() {
                                waitable.set_flag(false);
                                vec.push(*task_id);
                            }
                        }
                        for task_id in vec {
                            waiting_ref.remove(&task_id);
                            match tasks_ref.get_mut(&task_id) {
                                Some((_, waker)) => waker.wake_by_ref(),
                                _ => {}
                            }
                        }
                    })
                }
            }
        });
        let _ = opt_handle.insert(handle);
    }

    /// Spawn a task this worker and return it join-handle. To get the join-handle's return value,
    /// call [`crate::runtime::WorkerGroup::block_on`] on this join-handle.
    pub fn spawn<F>(&self, future: F) -> JoinHandle<F::Output>
    where
        F: Future + Send + 'static,
        F::Output: Send + 'static,
    {
        let task_id = self.task_id_gen.next();
        let (sender, receiver) = channel();
        let task = Task::new(
            async move {
                let _ = sender.send(future.await);
            },
            task_id,
        );
        self.spawner.push(task).expect("Work queue full!");
        Box::pin(async { receiver.await.unwrap() })
    }

    pub(super) fn wait_stop(&self) {
        let handle = self.join_handle.borrow_mut().take();
        let _ = handle.unwrap().join();
    }

    /// Find next task in the scheduling queue
    #[inline(always)]
    fn find_task(
        spawner: &Arc<ArrayQueue<Task>>,
        queue: &Arc<ArrayQueue<TaskId>>,
        tasks: &RefCell<BTreeMap<TaskId, (Task, Waker)>>,
    ) -> Option<TaskId> {
        while let Some(task) = spawner.pop() {
            let task_id = task.tid;
            let waker = TaskWaker::new(task_id, queue);
            tasks.borrow_mut().insert(task_id, (task, waker));
            queue.push(task_id).unwrap();
        }
        queue.pop()
    }
}
