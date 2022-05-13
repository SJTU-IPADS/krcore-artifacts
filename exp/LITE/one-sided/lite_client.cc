#include <gflags/gflags.h>

#include <assert.h>
#include <vector>

#include "../src/random.hh"
#include "../src/rdma/sop.hh"
#include "../../reporter.hh"
#include "rlib/benchs/thread.hh"
#include "rlib/core/lib.hh"
#include <random>

#include "../common.h"
#include "../lib/lite-lib.h"

#define VAL

#ifdef VAL // the name of our cluster is VAL

#include "../../val/cpu.hh"

using namespace xstore::platforms;
#endif
#define MAX_BUF_SIZE    (1024 * 1024 * 32) // 32M
#define NR_TESTS_PER_SIZE    (1000 * 1000)
#define NSEC_PER_SEC    (1000*1000*1000)

using namespace rdmaio; // warning: should not use it in a global space often
using namespace rdmaio::rmem;
using namespace rdmaio::qp;
using namespace r2;
using namespace r2::rdma;
using Thread_t = bench::Thread<usize>;

static inline unsigned long get_random() {
    static std::random_device rd;
    static std::mt19937_64 e(rd());
    return e();
}

DEFINE_int64(threads, 1, "#Threads used.");
DEFINE_int64(payload_sz, 8, "Payload size (bytes)");   // set default payload size to 64 bytes
DEFINE_int64(rdma_op, 0, "RDMA operation");
DEFINE_int64(run_sec, 10, "Running seconds");
// For RDMA
DEFINE_string(addr, "localhost:8888", "Server address to connect to."); // let val13 to be the server
DEFINE_int64(use_nic_idx, 0, "Which NIC to create QP");
DEFINE_int64(reg_nic_name, 73, "The name to register an opened NIC at rctrl.");
DEFINE_int64(reg_mem_name, 73, "The name to register an MR at rctrl.");
DEFINE_int64(mem_sz, 4194304, "Mr size");       // 4M
DEFINE_int64(port, 8888, "Server listener (UDP) port.");
DEFINE_uint32(nic_mod, 2, "NIC use mod");
DEFINE_int64(or_sz, 12, "Serve for the outstanding request. We use batch size to annote the step");
DEFINE_int64(remote_node, 1, "LITE remote node");

static pthread_barrier_t thread_barrier;
static unsigned int pg_size;
static uint64_t test_key[12];
static int password;

usize worker_fn(const usize &worker_id, Statics *s);

bool volatile running = true;

int main(int argc, char **argv) {
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    std::vector<Thread_t *> workers;
    std::vector<Statics> worker_statics(FLAGS_threads); // setup workers

    test_key[FLAGS_remote_node] = userspace_liteapi_alloc_remote_mem(FLAGS_remote_node,
                                                  MAX_BUF_SIZE, 0, password);
    RDMA_LOG(2) << "Finish remote mem alloc. Key:" << test_key;
    RDMA_LOG(2) << "Test RDMA (avg of " << NR_TESTS_PER_SIZE << " run)";

    for (uint i = 0; i < FLAGS_threads; ++i) {
        workers.push_back(
                new Thread_t(std::bind(worker_fn, i, &(worker_statics[i]))));
    }
    // start the workers
    for (auto w: workers) {
        w->start();
    }

    Reporter::report_thpt(worker_statics, FLAGS_run_sec); // report for 10 seconds
    running = false;                           // stop workers

    // wait for workers to join
    for (auto w: workers) {
        w->join();
    }
    RDMA_LOG(4) << "done";
}

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

// main func for every single worker
usize worker_fn(const usize &worker_id, Statics *s) {
    int nic_idx = FLAGS_use_nic_idx;
#ifdef VAL
    nic_idx = (worker_id) % 2;
    VALBinder::bind(nic_idx, worker_id % 24);
#endif
    r2::util::FastRandom r(0xdeafbeaf + worker_id);
    Statics &ss = *s;

    char *buf;
    int *poll;

    buf = (char *) aligned_alloc(4096, MAX_BUF_SIZE);
    poll = (int *) malloc(4 * NR_TESTS_PER_SIZE);
    memset(poll, 0, 4 * NR_TESTS_PER_SIZE);
    memset(buf, 'A', 1024 * 64);

    while (running) {
        r2::compile_fence();
        int t = FLAGS_remote_node;
// READ
        userspace_liteapi_rdma_read(test_key[t], buf, FLAGS_payload_sz,
                                    0, password);

// Write
//        userspace_liteapi_rdma_write(test_key[t], buf, FLAGS_payload_sz,
//                                     offset, password);

// Async Read
//        async_rdma_read(test_key[t], buf,
//                        FLAGS_payload_sz, 0,
//                        &poll[0]);
        ss.increment(1);
    }

    return 0;
}
