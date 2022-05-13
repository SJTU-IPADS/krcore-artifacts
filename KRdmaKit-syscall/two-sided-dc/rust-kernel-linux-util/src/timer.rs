/// A kernel timer based on RDTSC
/// may need to further check the conversion from

use crate::bindings::{do_gettimeofday, timeval};

#[allow(dead_code)]
#[derive(Debug)]
pub struct RTimer {
    start: u64,
}

impl RTimer {
    pub fn new() -> Self {
        Self {
            start: Self::get_cur_ts(),
        }
    }

    pub fn reset(&mut self) {
        self.start = Self::get_cur_ts();
    }

    fn get_cur_ts() -> u64 {
        #[cfg(target_arch = "x86_64")]
            unsafe {
            core::arch::x86_64::_rdtsc()
        }
    }

    pub fn passed(&self) -> u64 {
        if Self::get_cur_ts() < self.start {
            0
        } else {
            Self::get_cur_ts() - self.start
        }
    }

    pub fn passed_as_ns(&self) -> f64 {
        self.passed() as f64 * Self::tick_to_ns()
    }

    pub fn passed_as_msec(&self) -> f64 {
        self.passed_as_ns() / 1000 as f64
    }

    pub fn passed_as_sec(&self) -> f64 {
        self.passed_as_msec() / 1000 as f64
    }

    pub fn tick_to_ns() -> f64 {
        1 as f64 / unsafe { crate::bindings::get_hz() as f64 }
    }
}


/// A kernel timer based on `do_ do_gettimeofday`
///
pub struct KTimer {
    cur: timeval,
}

impl KTimer {
    pub fn new() -> Self {
        Self {
            cur: Self::get_cur_timeval(),
        }
    }

    pub fn get_cur_timeval() -> timeval {
        let mut cur: timeval = Default::default();
        unsafe { do_gettimeofday(&mut cur as *mut _) };
        cur
    }

    pub fn get_passed_usec(&self) -> i64 {
        let passed = Self::get_cur_timeval() - self.cur;
        passed.tv_sec * 1000000 + passed.tv_usec
    }

    pub fn reset(&mut self) {
        self.cur = Self::get_cur_timeval();
    }
}

use core::ops::Sub;

impl Sub for timeval {
    type Output = Self;

    // Credits: https://www.gnu.org/software/libc/manual/html_node/Calculating-Elapsed-Time.html
    fn sub(self, other: Self) -> Self::Output {
        let mut other = other;
        /* Perform the carry for the later subtraction by updating y. */
        if self.tv_usec < other.tv_usec {
            let nsec = (other.tv_usec - self.tv_usec) / 1000000 + 1;
            other.tv_usec -= 1000000 * nsec;
            other.tv_sec += nsec;
        }

        if self.tv_usec - other.tv_usec > 1000000 {
            let nsec = (self.tv_usec - other.tv_usec) / 1000000;
            other.tv_usec += 1000000 * nsec;
            other.tv_sec -= nsec;
        }
        let mut result: timeval = Default::default();
        /* Compute the time remaining to wait.
        tv_usec is certainly positive. */
        result.tv_sec = self.tv_sec - other.tv_sec;
        result.tv_usec = self.tv_usec - other.tv_usec;
        result
    }
}
