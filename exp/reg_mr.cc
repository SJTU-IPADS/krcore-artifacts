#include <gflags/gflags.h>

#include "../src/random.hh"
#include "../src/rdma/sop.hh"
#include "profile.hh"
#include "../reporter.hh"
#include "rlib/benchs/thread.hh"
#include "rlib/core/lib.hh"
#include <assert.h>
#include <random>
#include <vector>

#define VAL

#ifdef VAL // the name of our cluster is VAL

#include "../val/cpu.hh"

using namespace xstore::platforms;
#endif

using namespace rdmaio; // warning: should not use it in a global space often
using namespace rdmaio::rmem;
using namespace rdmaio::qp;
using namespace r2;
using namespace r2::rdma;
using Thread_t = bench::Thread<usize>;

static inline unsigned long
get_random() {
    static std::random_device rd;
    static std::mt19937_64 e(rd());
    return e();
}

DEFINE_int64(threads, 1, "#Threads used.");
DEFINE_int64(payload_sz,
             64,
             "Payload size (bytes)"); // set default payload size to 64 bytes
DEFINE_int64(rdma_op, 0, "RDMA operation");
DEFINE_int64(run_sec, 10, "Running seconds");
// For RDMA
DEFINE_string(addr,
              "localhost:8888",
              "Server address to connect to."); // let val13 to be the server
DEFINE_int64(use_nic_idx, 0, "Which NIC to create QP");
DEFINE_int64(reg_nic_name, 73, "The name to register an opened NIC at rctrl.");
DEFINE_int64(reg_mem_name, 73, "The name to register an MR at rctrl.");
DEFINE_int64(mem_sz, 4194304, "Mr size"); // 4M
DEFINE_int64(port, 8888, "Server listener (UDP) port.");
DEFINE_uint32(nic_mod, 2, "NIC use mod");
DEFINE_int64(
        or_sz,
        12,
        "Serve for the outstanding request. We use batch size to annote the step");

// TODO: add required params if necessilary

usize
worker_fn(const usize &worker_id, Statics *s);

bool volatile running = true;

int
main(int argc, char **argv) {
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    std::vector<Thread_t *> workers;
    std::vector<Statics> worker_statics(FLAGS_threads); // setup workers

    for (uint i = 0; i < FLAGS_threads; ++i) {
        workers.push_back(
                new Thread_t(std::bind(worker_fn, i, &(worker_statics[i]))));
    }

    // start the workers
    for (auto w: workers) {
        w->start();
    }

    Reporter::report_thpt(worker_statics, FLAGS_run_sec); // report for 10 seconds
    running = false;                                      // stop workers

    // wait for workers to join
    for (auto w: workers) {
        w->join();
    }
    RDMA_LOG(4) << "done";
}

#define K 1024
#define M 1024 * K


// main func for every single worker
usize
worker_fn(const usize &worker_id, Statics *s) {
    int nic_idx = FLAGS_use_nic_idx;
#ifdef VAL
    nic_idx = (worker_id) % 2;
    VALBinder::bind(nic_idx, worker_id % 24);
#endif
    std::vector<long> vec = {128, 512, 1 * K, 4 * K, 16 * K, 256 * K, 512 * K,
                             1 * M, 4 * M, 16 * M, 64 * M, 128 * M, 512 * M};
    // remote server nic
    int remote_nic_idx = nic_idx;
    auto nic = RNic::create(RNicInfo::query_dev_names().at(nic_idx)).value();

    // 1. create a local QP to use
    r2::util::FastRandom r(0xdeafbeaf + worker_id);
    std::string name = "client-qp-" + std::to_string(worker_id) + "-" +
                       std::to_string(get_random());
    int protection_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ |
                           IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_ATOMIC;
    int cnt = 1;

    for (auto &sz: vec) {
        auto local_mem = Arc<RMem>(new RMem(sz));
        printf("here\n");
        Profile profile = Profile(1);

        for (int i = 0; i < cnt; ++i) {
#if 0
            int cur_sz = 0;
            while (cur_sz < sz) {
                void *raw_ptr = local_mem->raw_ptr;
                auto raw_sz = local_mem->sz;
                profile.start();
                auto mr = ibv_reg_mr(nic->get_pd(),
                                     (void *) ((uint64_t) (raw_ptr) + cur_sz),
                                     4096,
                                     protection_flags
                );
                profile.tick_record(0);
                cur_sz += 4096;
                ibv_dereg_mr(mr);
            }
#else
            void *raw_ptr = local_mem->raw_ptr;
            auto raw_sz = local_mem->sz;
            auto mr = ibv_reg_mr(nic->get_pd(),
                                 raw_ptr,
                                 sz,
                                 protection_flags
            );
            profile.start();
            ibv_dereg_mr(mr);
            profile.tick_record(0);
#endif
        }
        profile.increase(cnt);
        profile.report(std::to_string(sz));
    }
    return 0;
}