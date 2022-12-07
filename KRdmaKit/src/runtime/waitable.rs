use crate::runtime::task::get_current_task_id;
use crate::runtime::worker::add_waitable;
use std::future::Future;
use std::pin::Pin;
use std::task::{Context, Poll};

pub trait Waitable: Send + Sync {
    fn wait_one_round(&self) -> WaitStatus;
}

#[derive(Copy, Clone, Debug)]
pub enum WaitStatus {
    Ready,
    Waiting,
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
}

pub async fn wait_on(waitable: impl Waitable + 'static) {
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

    add_waitable(get_current_task_id(), Box::new(waitable));
    Wait { flag: false }.await
}
