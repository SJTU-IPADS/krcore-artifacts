use crate::runtime::tid::TaskId;
use crossbeam::queue::ArrayQueue;
use std::cell::RefCell;
use std::fmt::{Debug, Formatter};
use std::future::Future;
use std::pin::Pin;
use std::sync::Arc;
use std::task::{Context, Poll, Wake, Waker};

thread_local! {
    static TASK_ID: RefCell<TaskId> = RefCell::new(0);
}

pub(super) fn get_current_task_id() -> TaskId {
    TASK_ID.with(|tid| *tid.borrow())
}

pub(super) fn set_current_task_id(task_id: TaskId) {
    TASK_ID.with(|tid| *tid.borrow_mut() = task_id)
}

pub(super) struct Task {
    future: Pin<Box<dyn Future<Output = ()>>>,
    pub tid: TaskId,
}

unsafe impl Send for Task {}
unsafe impl Sync for Task {}

impl Task {
    pub(super) fn new(future: impl Future<Output = ()> + 'static, task_id: TaskId) -> Self {
        Self {
            future: Box::pin(future),
            tid: task_id,
        }
    }

    pub fn poll(&mut self, context: &mut Context) -> Poll<()> {
        self.future.as_mut().poll(context)
    }
}

impl Debug for Task {
    fn fmt(&self, f: &mut Formatter<'_>) -> std::fmt::Result {
        write!(f, "Task [{:<8}]", self.tid)
    }
}

pub(super) struct TaskWaker {
    task_id: TaskId,
    queue: Arc<ArrayQueue<TaskId>>,
}

impl TaskWaker {
    pub(super) fn new(task_id: TaskId, queue: &Arc<ArrayQueue<TaskId>>) -> Waker {
        Waker::from(Arc::new(TaskWaker {
            task_id,
            queue: queue.clone(),
        }))
    }

    #[inline(always)]
    fn wake_task(&self) {
        self.queue.push(self.task_id).unwrap()
    }
}

impl Wake for TaskWaker {
    fn wake(self: Arc<Self>) {
        self.wake_task();
    }

    fn wake_by_ref(self: &Arc<Self>) {
        self.wake_task();
    }
}

pub async fn yield_now() {
    struct Yield {
        flag: bool,
    }

    impl Future for Yield {
        type Output = ();

        fn poll(mut self: Pin<&mut Self>, ctx: &mut Context<'_>) -> Poll<Self::Output> {
            if self.flag {
                Poll::Ready(())
            } else {
                self.flag = true;
                ctx.waker().wake_by_ref();
                Poll::Pending
            }
        }
    }

    Yield { flag: false }.await
}
