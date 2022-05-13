use crate::rust_kernel_rdma_base::*;
use rust_kernel_linux_util::timer::RTimer;

#[derive(Debug)]
pub struct QPProfile {
    timer: RTimer,
    post_cycles: u64,
    poll_cycles: u64,
}

impl QPProfile {
    pub fn new() -> Self {
        Self {
            timer: RTimer::new(),
            post_cycles: 0,
            poll_cycles: 0,
        }
    }

    pub fn reset_timer(&mut self) {
        self.timer.reset();
    }

    pub fn reset_post_cycles(&mut self) {
        self.post_cycles = 0;
    }

    pub fn reset_poll_cycles(&mut self) {
        self.poll_cycles = 0;
    }

    pub fn add_cur_timer_to_post(&mut self) {
        self.post_cycles += self.timer.passed();
    }

    pub fn add_cur_timer_to_poll(&mut self) {
        self.poll_cycles += self.timer.passed();
    }

    pub fn get_post_poll_cycles(&self) -> (u64, u64) {
        (self.post_cycles, self.poll_cycles)
    }
}
