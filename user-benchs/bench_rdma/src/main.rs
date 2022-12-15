use clap::{arg, App, AppSettings};
use lazy_static::lazy_static;
use rand::RngCore;
use rand_chacha::rand_core::SeedableRng;
use rand_chacha::ChaCha8Rng;

use std::net::SocketAddr;
use std::sync::Mutex;
use std::thread::{spawn, JoinHandle};
use std::time::Instant;

use KRdmaKit::{MemoryRegion, QueuePairBuilder, QueuePairStatus, UDriver};

lazy_static! {
    static ref LISTEN_ADDR: Mutex<String> = Mutex::new("127.0.0.1:10001".to_string());
}

static mut NIC_NUM: usize = 1;
static mut NIC_IDX: usize = 1;
static mut RANDOM_SPACE_K: u64 = 10; // 10KB
static mut THREADS: u64 = 1;
static mut FACTOR: u64 = 50;
static mut RUNNING: bool = true;
static mut PAYLOAD: u64 = 32;
static mut LOCAL_MR: u64 = 2048;
static mut READ: bool = true;
static mut RANDOM: bool = true;
static mut CLI_ID: u64 = 0;
static mut THREAD_GAP: u64 = 1024;
static mut LATENCY_TEST: u8 = 0;

#[derive(Clone, Copy)]
#[repr(align(128))]
struct Counter {
    inner: u64,
}

impl Counter {
    fn new() -> Self {
        Self { inner: 0 }
    }
}

fn main() {
    let matches = App::new("Performance Test")
        .version("0.1")
        .about("RDMA Performance")
        .setting(AppSettings::AllArgsOverrideSelf)
        .args(&[
            arg!(--addr [LISTEN_ADDR] "server listening address and port (using TCP)"),
            arg!(--num_nic [NIC_NUM] "Number of NIC used"), 
            arg!(--start_nic_idx [NIC_IDX] "Start NIC idx"),
            arg!(--threads [THREADS] "Threads number"),
            arg!(--factor [FACTOR] "one reply among <num> requests"),
            arg!(--payload [PAYLOAD] "one rdma request payload"),
            arg!(--local_mr [LOCAL_MR] "local mr capacity"),
            arg!(--read [READ] "whether to test read or not"),
            arg!(--random_space [RANDOM_SPACE_K] "The random space of the remote server"),
            arg!(--random_arg [RANDOM] "Whether generate the request andonmly"),
            arg!(--client_id [CLI_ID] "ID of the benchmarking client"),
            arg!(--gap [THREAD_GAP] "Address gap between threads, only effective when random_arg is not set"),
            arg!(--latency [latency_test] "Whether to test latency or thpt"),
        ])
        .get_matches();

    if let Some(f) = matches.value_of("addr") {
        *LISTEN_ADDR.lock().unwrap() = f.to_string();
    }
    if let Some(f) = matches.value_of("num_nic") {
        unsafe { NIC_NUM = f.parse::<usize>().unwrap() }
    }

    if let Some(f) = matches.value_of("start_nic_idx") {
        unsafe { NIC_IDX = f.parse::<usize>().unwrap() }
    }

    if let Some(f) = matches.value_of("threads") {
        unsafe { THREADS = f.parse::<u64>().unwrap() }
    }
    if let Some(f) = matches.value_of("factor") {
        unsafe { FACTOR = f.parse::<u64>().unwrap() }
    }
    if let Some(f) = matches.value_of("payload") {
        unsafe { PAYLOAD = f.parse::<u64>().unwrap() }
    }
    if let Some(f) = matches.value_of("local_mr") {
        unsafe { LOCAL_MR = f.parse::<u64>().unwrap() }
    }

    if let Some(f) = matches.value_of("random_space") {
        unsafe { RANDOM_SPACE_K = f.parse::<u64>().unwrap() * 1024 }
    } else {
        unsafe { RANDOM_SPACE_K = RANDOM_SPACE_K * 1024 }
    }

    if let Some(f) = matches.value_of("read") {
        unsafe { READ = f.parse::<bool>().unwrap() }
    }
    if let Some(f) = matches.value_of("random_arg") {
        unsafe { RANDOM = f.parse::<bool>().unwrap() }
    }

    if let Some(f) = matches.value_of("client_id") {
        unsafe { CLI_ID = f.parse::<u64>().unwrap() }
    }

    if let Some(f) = matches.value_of("gap") {
        unsafe { THREAD_GAP = f.parse::<u64>().unwrap() }
    }

    if let Some(f) = matches.value_of("latency") {
        unsafe { LATENCY_TEST = f.parse::<u8>().unwrap() }
    }

    unsafe {
        println!(
            "Sanity check parameters: payload {}, nthreads {}, use READ {}",
            PAYLOAD, THREADS, READ
        )
    };

    unsafe { main_inner() };
}

unsafe fn main_inner() {
    let threads = THREADS as usize;
    let mut vec = Vec::new();
    let mut counters = Vec::new();
    let addr: SocketAddr = LISTEN_ADDR.lock().unwrap().parse().unwrap();

    for thread_id in 0..threads {
        let a = Box::into_raw(Box::new(Counter::new())) as u64;
        counters.push(a);

        #[cfg(not(feature = "signaled"))]
        {
            vec.push(client_thread(thread_id, NIC_NUM, addr, FACTOR, a, READ));
        }

        #[cfg(feature = "signaled")]
        {
            vec.push(client_thread_signaled(
                thread_id, NIC_NUM, addr, FACTOR, a, READ,
            ));
        }
    }

    let mut prev = 0;
    let mut tick = Instant::now();

    for epoch in 0..15 {
        let mut counter = 0;
        std::thread::sleep(std::time::Duration::from_secs(1));
        for i in &counters {
            let addr = (*i) as *mut Counter;
            counter += (*addr).inner;
        }

        let elapsed = tick.elapsed();
        tick = Instant::now();

        let thpt = (counter - prev) as f64 / elapsed.as_micros() as f64;

        if LATENCY_TEST != 0 {
            // latency test on
            println!(
                "epoch @ {:<3} thpt {:>3.2} Mreqs/sec  Latency : {:>2.2} us",
                epoch,
                thpt,
                1.0 * (THREADS as f64) / thpt,
            )
        } else {
            println!("epoch @ {:<3} thpt {:>3.2} Mreqs/sec", epoch, thpt,);
        }

        prev = counter;
    }

    RUNNING = false;
    for handle in vec.into_iter() {
        let _ = handle.join();
    }
    for c in counters {
        let _ = Box::from_raw(c as *mut Counter);
    }
}

#[allow(dead_code)]
unsafe fn client_thread(
    thread_idx: usize,
    num_nic: usize,
    addr: SocketAddr,
    factor: u64,
    counter_ptr: u64,
    read: bool,
) -> JoinHandle<()> {
    spawn(move || {
        let batch_or_not = if LATENCY_TEST != 0 { 1 } else { 32 };
        let counter_ptr = counter_ptr as *mut Counter;
        let client_port: u8 = 1;
        let ctx = UDriver::create()
            .expect("failed to query device")
            .devices()
            .get(thread_idx % num_nic + NIC_IDX)
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
            QueuePairStatus::ReadyToSend => {}
            _ => eprintln!("Error : Bring up failed"),
        }

        let mr_infos = qp.query_mr_info().expect("Failed to query MR info");
        let mr_metadata = mr_infos.inner().get("MR").expect("Unregistered MR");
        let client_mr =
            MemoryRegion::new(ctx.clone(), LOCAL_MR as _).expect("Failed to allocate MR");
        let mr_buf = client_mr.get_virt_addr() as *mut u64;
        unsafe { *mr_buf = 0 };

        let mut rand =
            ChaCha8Rng::seed_from_u64((0xdeadbeaf + 73 * thread_idx) as u64 + CLI_ID * 37);
        let mut completions = [Default::default()];

        let mut pending: usize = 0;
        let start = 0;

        while RUNNING {
            std::sync::atomic::compiler_fence(std::sync::atomic::Ordering::Release);
            for i in 0..factor {
                let mut r = rand.next_u64();

                if unsafe { !RANDOM } {
                    if THREAD_GAP != 0 {
                        // r = (thread_idx * 64) as _;
                        assert!(THREAD_GAP >= PAYLOAD);
                        assert!(THREADS * THREAD_GAP <= RANDOM_SPACE_K);

                        r = r % THREAD_GAP + (thread_idx as u64 * THREAD_GAP);
                    } else {
                        r = 0;
                    }
                }

                // align
                let align = 64;
                if r % align != 0 {
                    r = r + align - r % align;
                }
                assert!(r % align == 0);

                let index = (r % (RANDOM_SPACE_K - PAYLOAD)) as u64;
                // start = (start + std::cmp::max(PAYLOAD, 64)) % ((LOCAL_MR - PAYLOAD) as u64);
                let signal = pending == 0;

                if read {
                    qp.post_send_read(
                        &client_mr,
                        start..(start + PAYLOAD),
                        signal,
                        mr_metadata.addr + index,
                        mr_metadata.rkey,
                        i,
                    )
                    .expect("read should succeeed");
                } else {
                    qp.post_send_write(
                        &client_mr,
                        start..(start + PAYLOAD),
                        signal,
                        mr_metadata.addr + index,
                        mr_metadata.rkey,
                        i,
                    )
                    .expect("read should succeeed");
                }

                pending += 1;
                if pending >= batch_or_not {
                    let mut ok = false;
                    while !ok {
                        let ret = qp
                            .poll_send_cq(&mut completions)
                            .expect("Failed to poll cq");
                        if ret.len() > 0 {
                            if ret[0].status != 0 {
                                println!("read remote addr: {:?}", index);
                            }
                            assert_eq!(ret[0].status, 0);
                            ok = true;
                        }
                    }
                    pending = 0;
                }
            }
            unsafe { (*counter_ptr).inner += factor };
        }
        // end of main benchmark loop
    })
}

#[allow(dead_code)]
unsafe fn client_thread_signaled(
    thread_idx: usize,
    num_nic: usize,
    addr: SocketAddr,
    factor: u64,
    counter_ptr: u64,
    read: bool,
) -> JoinHandle<()> {
    spawn(move || {
        println!("start signaled client {}", thread_idx);
        let counter_ptr = counter_ptr as *mut Counter;
        let client_port: u8 = 1;
        let ctx = UDriver::create()
            .expect("failed to query device")
            .devices()
            .get(thread_idx % num_nic + NIC_IDX)
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
            QueuePairStatus::ReadyToSend => {}
            _ => eprintln!("Error : Bring up failed"),
        }

        let mr_infos = qp.query_mr_info().expect("Failed to query MR info");
        let mr_metadata = mr_infos.inner().get("MR").expect("Unregistered MR");
        let client_mr =
            MemoryRegion::new(ctx.clone(), LOCAL_MR as _).expect("Failed to allocate MR");
        let mr_buf = client_mr.get_virt_addr() as *mut u64;
        unsafe { *mr_buf = 0 };

        let mut rand =
            ChaCha8Rng::seed_from_u64((0xdeadbeaf + 73 * thread_idx) as u64 + CLI_ID * 37);
        let mut completions = [Default::default()];

        let start = 0;

        while RUNNING {
            std::sync::atomic::compiler_fence(std::sync::atomic::Ordering::Release);
            for i in 0..factor {
                let mut r = rand.next_u64();

                if unsafe { !RANDOM } {
                    if THREAD_GAP != 0 {
                        // r = (thread_idx * 64) as _;
                        assert!(THREAD_GAP >= PAYLOAD);
                        assert!(THREADS * THREAD_GAP <= RANDOM_SPACE_K);

                        r = r % THREAD_GAP + (thread_idx as u64 * THREAD_GAP);
                    } else {
                        r = 0;
                    }
                }

                // align
                let align = 64;
                if r % align != 0 {
                    r = r + align - r % align;
                }
                assert!(r % align == 0);

                let index = (r % (RANDOM_SPACE_K - PAYLOAD)) as u64;
                // start = (start + std::cmp::max(PAYLOAD, 64)) % ((LOCAL_MR - PAYLOAD) as u64);
                let signal = true;

                if read {
                    qp.post_send_read(
                        &client_mr,
                        start..(start + PAYLOAD),
                        signal,
                        mr_metadata.addr + index,
                        mr_metadata.rkey,
                        i,
                    )
                    .expect("read should succeeed");
                } else {
                    qp.post_send_write(
                        &client_mr,
                        start..(start + PAYLOAD),
                        signal,
                        mr_metadata.addr + index,
                        mr_metadata.rkey,
                        i,
                    )
                    .expect("read should succeeed");
                }
            }

            for _ in 0..factor {
                let mut ok = false;
                while !ok {
                    let ret = qp
                        .poll_send_cq(&mut completions)
                        .expect("Failed to poll cq");
                    if ret.len() > 0 {
                        if ret[0].status != 0 {}
                        assert_eq!(ret[0].status, 0);
                        ok = true;
                    }
                }
            }
            unsafe { (*counter_ptr).inner += factor };
        }
        // end of main benchmark loop
    })
}
