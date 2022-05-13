#include <gflags/gflags.h>

#include <assert.h>
#include <vector>

#include "../../include/syscall.h"
#include "../src/random.hh"
#include "../src/rdma/async_op.hh"
#include "../reporter.hh"
#include "rlib/benchs/thread.hh"
#include "rlib/core/lib.hh"
#include "rlib/core/qps/mod.hh"
#include "rlib/core/qps/recv_iter.hh"
#include "ud_msg.hh"

#define VAL

#ifdef VAL // the name of our cluster is VAL

#include "./val/cpu.hh"

using namespace xstore::platforms;
#endif
using namespace rdmaio;
using namespace rdmaio::qp;
using namespace rdmaio::rmem;
using namespace r2;
using namespace r2::rdma;

using SimpleAllocator = r2::UDAdapter::SimpleAllocator;
using Thread_t = bench::Thread<usize>;

DEFINE_string(addr, "localhost:8888", "Server address to connect to.");
DEFINE_string(gid, "fe80:0000:0000:0000:ec0d:9a03:0078:645e", "Gid target");
DEFINE_int64(threads, 1, "#Threads used.");
DEFINE_int64(coroutines, 10, "#Coroutines used.");
DEFINE_string(client_name, "localhost", "Unique name to identify machine.");
DEFINE_int64(use_nic_idx, 0, "Which NIC to create QP");
DEFINE_int64(reg_nic_name, 73, "The name to register an opened NIC at rctrl.");
DEFINE_int64(reg_mem_name, 73, "The name to register an MR at rctrl.");
DEFINE_int64(op_count, 10, "The name to register an MR at rctrl.");

usize
worker_fn(const usize& worker_id, Statics* s);

bool volatile running = true;

int
main(int argc, char** argv)
{
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    std::vector<Thread_t*> workers;
    std::vector<Statics> worker_statics(FLAGS_threads);

    for (uint i = 0; i < FLAGS_threads; ++i) {
        workers.push_back(
                new Thread_t(std::bind(worker_fn, i, &(worker_statics[i]))));
    }

    // start the workers
    for (auto w : workers) {
        w->start();
    }
    Reporter::report_thpt(worker_statics, 20); // report for 10 seconds
    running = false;                           // stop workers

    // wait for workers to join
    for (auto w : workers) {
        w->join();
    }

    r2::compile_fence();
    // gProfile.report();
    LOG(4) << "done";
}

usize
worker_fn(const usize& worker_id, Statics* s)
{
    int nic_idx = FLAGS_use_nic_idx;
#ifdef VAL
    nic_idx = (worker_id) % 2;
    VALBinder::bind(nic_idx, worker_id % 24);
#endif
    Statics& ss = *s;
    const char* gid_str = FLAGS_gid.c_str();

    int qd = queue();
    assert(qd >= 0);
    qconnect(qd, gid_str, strlen(gid_str), worker_id);
    const int req_len = 1;

    core_req_t req_list[req_len];
    for (int i = 0; i < req_len; ++i) {
        req_list[i] = {
                .addr = 1024,
                .length = 64,
                .lkey = 32,
                .remote_addr=2048,
                .rkey = 32,
                .send_flags = 1,
                .vid = 0,
                .type = Read
        };
    }
    push_core_req_t req = {.req_len=1, .req_list=req_list};
    pop_reply_t reply;

    while (running) {
        qpush(qd, &req);
        // pop till comp finish
        while ( qpop(qd, &reply) != 1) {  }
        ss.increment();
    }
    return 0;
}
