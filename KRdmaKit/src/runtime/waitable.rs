use crate::runtime::task::get_current_task_id;
use crate::runtime::worker::add_waitable;
use std::future::Future;
use std::pin::Pin;
use std::task::{Context, Poll};

/// User can design his own implementation on how to
/// make a sync task to a async one by implementing the `Waitable` trait.
pub trait Waitable: Send + Sync {
    fn wait_one_round(&mut self) -> WaitStatus;
}

pub(super) struct WaitableWrapper {
    pub(super) inner: Box<dyn Waitable + 'static>,
    pub(super) flag: u64,
}

impl WaitableWrapper {
    #[inline(always)]
    pub(super) fn set_flag(&self, flag: bool) {
        unsafe { *(self.flag as *mut bool) = flag }
    }
}

#[derive(Copy, Clone, Debug)]
pub enum WaitStatus {
    Ready,
    Waiting,
    Error,
}

impl WaitStatus {
    #[inline(always)]
    pub fn is_ready(&self) -> bool {
        match self {
            WaitStatus::Ready => true,
            _ => false,
        }
    }

    #[inline(always)]
    pub fn is_waiting(&self) -> bool {
        match self {
            WaitStatus::Waiting => true,
            _ => false,
        }
    }

    #[inline(always)]
    pub fn is_error(&self) -> bool {
        match self {
            WaitStatus::Error => true,
            _ => false,
        }
    }
}

/// Wait on an [`Waitable`] object to finish. Various kinds of object can be waited at
/// the same time as long as you implement `Waitable` for them.
///
/// For example, you can implement `Waitable` for TCP, RPC and RDMA and wait them in just one worker thread.
pub async fn wait_on(waitable: impl Waitable + 'static) -> Result<(), ()> {
    struct Wait {
        flag: bool,
    }

    impl Future for Wait {
        type Output = ();
        fn poll(mut self: Pin<&mut Self>, _ctx: &mut Context<'_>) -> Poll<Self::Output> {
            if self.flag {
                Poll::Ready(())
            } else {
                self.flag = true;
                Poll::Pending
            }
        }
    }

    let flag = Box::into_raw(Box::new(false)) as u64;
    let wrapper = WaitableWrapper {
        inner: Box::new(waitable),
        flag,
    };

    add_waitable(get_current_task_id(), wrapper);
    Wait { flag: false }.await;

    let flag = unsafe { Box::from_raw(flag as *mut bool) };
    return if *flag { Ok(()) } else { Err(()) };
}
