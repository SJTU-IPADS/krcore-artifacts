use clap::{arg, App, AppSettings};
use lazy_static::lazy_static;

use std::net::SocketAddr;
use std::sync::Mutex;

use KRdmaKit::services_user::{ConnectionManagerServer, DefaultConnectionManagerHandler};
use KRdmaKit::{MemoryRegion, UDriver};

lazy_static! {
    static ref LISTEN_ADDR: Mutex<String> = Mutex::new("127.0.0.1:10001".to_string());
}

static mut NIC_IDX: usize = 0;
static mut RANDOM_SPACE_K: usize = 10; // 10KB
static mut HUGEPAGE: bool = true;

static mut RUNNING: bool = true;

fn main() {
    let matches = App::new("Performance Test")
        .version("0.1")
        .about("RDMA Performance Benchmark")
        .setting(AppSettings::AllArgsOverrideSelf)
        .args(&[
            arg!(--addr [LISTEN_ADDR] "server listening address and port (using TCP)"),
            arg!(--nic [NIC_IDX] "select which RNIC to use"),
            arg!(--random_space [RANDOM_SPACE_K] "The random space of the remote server"),
            arg!(--huge_page [HUGEPAGE] "Whether to use the huge page"),
            // !!! FIXME: below flags are dummy ones to keep the same with clients for ease of benchmark scripting
            arg!(--read [READ] "whether to test read or not"),
            arg!(--num_nic [NIC_NUM] "Number of NIC used"),
            arg!(--factor [FACTOR] "one reply among <num> requests"),
            arg!(--payload [PAYLOAD] "one rdma request payload"),
            arg!(--threads [THREAD_NUM] "Threads number"),
            arg!(--random_arg [RANDOM] "Whether generate the request andonmly"),
            arg!(--gap [THREAD_GAP] "Address gap between threads, only effective when random_arg is not set"),
        ])
        .get_matches();

    if let Some(f) = matches.value_of("addr") {
        *LISTEN_ADDR.lock().unwrap() = f.to_string();
    }
    if let Some(f) = matches.value_of("nic") {
        unsafe { NIC_IDX = f.parse::<usize>().unwrap() }
    }
    if let Some(f) = matches.value_of("random_space") {
        unsafe { RANDOM_SPACE_K = f.parse::<usize>().unwrap() * 1024 }
    } else {
        unsafe { RANDOM_SPACE_K = RANDOM_SPACE_K * 1024 }
    }

    if let Some(f) = matches.value_of("huge_page") {
        unsafe { HUGEPAGE = f.parse::<bool>().unwrap() }
    }

    let addr: SocketAddr = LISTEN_ADDR.lock().unwrap().parse().unwrap();

    unsafe { main_inner(NIC_IDX, addr) };
}

pub fn main_inner(idx: usize, addr: SocketAddr) {
    println!("server uses RNIC {}", idx);
    let ctx = UDriver::create()
        .expect("failed to query device")
        .devices()
        .get(idx)
        .expect("no rdma device available")
        .open_context()
        .expect("failed to create RDMA context");

    println!("Check registered huge page sz: {}KB", unsafe {
        RANDOM_SPACE_K / 1_000
    });

    let mut handler = DefaultConnectionManagerHandler::new(&ctx, 1);
    let server_mr = if unsafe { HUGEPAGE } {
        MemoryRegion::new_huge_page(ctx.clone(), unsafe { RANDOM_SPACE_K })
            .expect("Failed to allocate huge page MR")
    } else {
        MemoryRegion::new(ctx.clone(), unsafe { RANDOM_SPACE_K }).expect("Failed to allocate MR")
    };

    handler.register_mr(vec![("MR".to_string(), server_mr)]);
    let server = ConnectionManagerServer::new(handler);

    ctrlc::set_handler(move || {
        unsafe { RUNNING = false };
    })
    .expect("Error setting Ctrl-C handler");
    let running = unsafe { &mut RUNNING as *mut bool };
    let _ = server.blocking_listener(addr, running);
    println!("Exit");
}
