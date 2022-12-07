use std::sync::atomic::{AtomicUsize, Ordering};

pub(super) type TaskId = usize;
pub(super) struct TaskIdGenerator {
    next: AtomicUsize,
}

impl TaskIdGenerator {
    pub(super) fn new() -> Self {
        Self {
            next: AtomicUsize::new(0),
        }
    }

    #[inline]
    pub(super) fn next(&self) -> TaskId {
        self.next.fetch_add(1, Ordering::SeqCst)
    }
}

unsafe impl Send for TaskIdGenerator {}
unsafe impl Sync for TaskIdGenerator {}
