#![feature(thread_id_value)]

pub mod random;

use std::borrow::Borrow;
use std::cell::RefCell;
use std::collections::BTreeSet;
use std::net::SocketAddr;
use std::net::ToSocketAddrs;
use std::num::NonZeroU8;
use std::sync::Arc;
use std::thread;
use std::thread::JoinHandle;
use std::time::{Duration, Instant};
use KRdmaKit::runtime::waitable::{wait_on, WaitStatus, Waitable};
use KRdmaKit::runtime::work_group::WorkerGroup;
use KRdmaKit::{MemoryRegion, QueuePair, QueuePairBuilder, QueuePairStatus, UDriver};

use clap::Parser;

#[derive(Parser, Debug)]
#[command(name = "Throughput Benchmark")]
#[command(author = "Haotian Wang <WangHaotian013@outlook.com>")]
#[command(version = "1.0")]
#[command(about = "Set throughput bench arguments", long_about = None)]
struct Args {
    /// Lasting time of the reporter thread (second)
    #[arg(long, default_value_t = 15)]
    lasting_time: u64,

    /// Number of worker threads you want to start
    #[arg(long, default_value_t = 10)]
    thread_num: usize,

    /// Number of tasks you commit to each worker
    #[arg(long, default_value_t = 10)]
    task_per_thread: usize,

    /// RDMA read/write payload length (byte)
    #[arg(long, default_value_t = 8)]
    payload: u64,

    /// Server address to connect to
    #[arg(long, default_value_t = ("127.0.0.1:8888".to_socket_addrs().unwrap().next().unwrap()))]
    addr: SocketAddr,

    /// Number of tasks you poll one round, between each round
    /// the worker waits to see if the waitable is ready
    #[arg(long, default_value_t = NonZeroU8::new(5).unwrap())]
    task_per_round: NonZeroU8,
}

#[derive(Copy, Clone)]
#[repr(align(128))]
struct Counter {
    inner: u64,
}

impl Counter {
    const fn new() -> Self {
        Self { inner: 0 }
    }
}

static mut WORKER_GROUP: WorkerGroup = WorkerGroup::new();
static mut RUNNING: bool = true;

thread_local! {
    static POLLED: RefCell<BTreeSet<u64>> = RefCell::new(BTreeSet::new());
}

struct RDMAWaitable {
    qp: *const QueuePair,
    wr_id: u64,
}

unsafe impl Send for RDMAWaitable {}
unsafe impl Sync for RDMAWaitable {}

impl Waitable for RDMAWaitable {
    fn wait_one_round(&self) -> WaitStatus {
        POLLED.with(|polled| {
            let mut polled = polled.borrow_mut();
            if polled.remove(&self.wr_id) {
                return WaitStatus::Ready;
            }
            let mut completions = [Default::default(); 16];
            let completions = unsafe {
                (*(self.qp))
                    .poll_send_cq(&mut completions)
                    .expect("Poll CQ error")
            };

            let mut ok = false;
            for wc in completions {
                if wc.wr_id == self.wr_id {
                    ok = true;
                } else {
                    polled.insert(wc.wr_id);
                }
            }
            return if ok {
                WaitStatus::Ready
            } else {
                WaitStatus::Waiting
            };
        })
    }
}

fn report_throughput(start: Instant, end: Instant, counters: Vec<u64>) -> JoinHandle<()> {
    thread::spawn(move || {
        while !after(start) {
            thread::yield_now();
        }

        let mut thpt_snapshot = Vec::new();

        let thread_num = counters.len();

        for _ in 0..thread_num {
            thpt_snapshot.push(0);
        }

        let mut tick = Instant::now();
        while !after(end) {
            thread::sleep(Duration::from_secs(1));

            let mut sum = 0;
            for i in 0..thread_num {
                let tmp = unsafe { (*(counters[i] as *const Counter)).inner };
                sum += tmp - thpt_snapshot[i];
                thpt_snapshot[i] = tmp;
            }
            let elapsed = tick.elapsed();
            tick = Instant::now();

            let throughput = sum as f64 / elapsed.as_micros() as f64;
            println!("Throughput {:>2.2} Mops", throughput);
        }
        unsafe { RUNNING = false };
    })
}

#[inline]
fn after(end_instant: Instant) -> bool {
    Instant::now() > end_instant
}

fn connect_qp(num: usize, addr: SocketAddr) -> Vec<Arc<QueuePair>> {
    let client_port: u8 = 1;
    let mut vec = Vec::new();

    for _ in 0..num {
        let ctx = UDriver::create()
            .expect("failed to query device")
            .devices()
            .get(0)
            .expect("no rdma device available")
            .open_context()
            .expect("failed to create RDMA context");
        let mut builder = QueuePairBuilder::new(&ctx);
        builder
            .allow_remote_rw()
            .allow_remote_atomic()
            .set_port_num(client_port);
        let qp = builder.build_rc().expect("failed to create the client QP");
        let qp = qp.handshake(addr).expect("Handshake failed!");
        let a = qp.status().expect("Query status failed!");
        match a {
            QueuePairStatus::ReadyToSend => println!("Bring up succeeded"),
            _ => eprintln!("Error : Bring up failed"),
        };
        vec.push(qp);
    }
    vec
}

fn main() {
    let args = Args::parse();

    let thread_num: usize = args.thread_num;
    let task_per_thread: usize = args.task_per_thread;
    let lasting_time: u64 = args.lasting_time;
    let payload: u64 = args.payload;
    let addr: SocketAddr = args.addr;
    let task_per_round: NonZeroU8 = args.task_per_round;

    let qps = connect_qp(thread_num, addr);
    let mut mrs = Vec::new();
    for qp in &qps {
        mrs.push(Arc::new(
            MemoryRegion::new(qp.ctx().clone(), 4096).expect("Failed to allocate MR"),
        ));
    }

    let mr_infos = qps[0].query_mr_info().expect("Failed to query MR info");
    println!("{:?}", mr_infos);
    let mr_metadata = mr_infos.inner().get("MR1").expect("Unregistered MR");
    let addr = mr_metadata.addr;
    let rkey = mr_metadata.rkey;

    println!(
        "Thread Num [{}]  Task Per Thread [{}]",
        thread_num, task_per_thread
    );

    let start = Instant::now();
    let end = start + Duration::from_secs(lasting_time);

    let mut counters = Vec::new();
    for _ in 0..thread_num {
        counters.push(Box::into_raw(Box::new(Counter::new())) as u64);
    }

    let handle = report_throughput(start, end, counters.clone());

    unsafe {
        for _ in 0..thread_num {
            WORKER_GROUP.add_worker(task_per_round);
        }
        let mut handles = Vec::new();
        for i in 0..(thread_num * task_per_thread) {
            let id = i;
            let thread_id = (id % thread_num) as u64;
            let task_id = (id / thread_num) as u64;
            let qp = qps[thread_id as usize].clone();
            let client_mr = mrs[thread_id as usize].clone();
            let counter_addr = counters[thread_id as usize];
            let handle = WORKER_GROUP.spawn_unchecked(thread_id as usize, async move {
                let mut rand =
                    random::FastRandom::new(0xdeadbeaf + task_id * 64 + thread_id * 1024);
                while RUNNING {
                    let r = rand.get_next();
                    let start = r % 4000;
                    let off = r % 50000;
                    let _ = qp.post_send_read(
                        client_mr.borrow(),
                        start..(start + payload),
                        true,
                        addr + off as u64,
                        rkey,
                        task_id,
                    );
                    // TODO : implement Waitable
                    let waitable = RDMAWaitable {
                        qp: Arc::as_ptr(&qp),
                        wr_id: task_id,
                    };
                    wait_on(waitable).await;
                    (*(counter_addr as *mut Counter)).inner += 1;
                }
            });
            handles.push(handle);
        }
        WORKER_GROUP.start();
        WORKER_GROUP.block_on(async move {
            for handle in handles.into_iter() {
                handle.await
            }
        });
        WORKER_GROUP.stop();
    }

    let _ = handle.join();
    for i in counters.into_iter() {
        unsafe { Box::from_raw(i as *mut Counter) };
    }
}
