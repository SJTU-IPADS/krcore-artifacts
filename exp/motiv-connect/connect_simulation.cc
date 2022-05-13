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
#include <random>

#define VAL

#ifdef VAL // the name of our cluster is VAL

#include "../val/cpu.hh"

using namespace xstore::platforms;
#endif
using namespace rdmaio;
using namespace rdmaio::qp;
using namespace rdmaio::rmem;
using namespace r2;
using namespace r2::rdma;

using Thread_t = bench::Thread<usize>;

DEFINE_string(addr, "localhost:8888", "Server address to connect to.");
DEFINE_int64(payload_sz, 64, "Payload size (bytes)");   // set default payload size to 64 bytes
DEFINE_string(gid, "fe80:0000:0000:0000:ec0d:9a03:0078:645e", "Gid target");
DEFINE_int64(threads, 1, "#Threads used.");
DEFINE_int64(rdma_op, 0, "RDMA operation");
DEFINE_int64(coroutines, 10, "#Coroutines used.");
DEFINE_string(client_name, "localhost", "Unique name to identify machine.");
DEFINE_int64(use_nic_idx, 0, "Which NIC to create QP");
DEFINE_int64(reg_nic_name, 73, "The name to register an opened NIC at rctrl.");
DEFINE_int64(reg_mem_name, 73, "The name to register an MR at rctrl.");
DEFINE_int64(op_count, 10, "The name to register an MR at rctrl.");
DEFINE_int64(or_sz, 1, "Serve for the outstanding request. We use batch size to annote the step");
DEFINE_int64(run_sec, 10, "Running seconds");
DEFINE_string(gids, "fe80:0000:0000:0000:ec0d:9a03:00ca:31d8 fe80:0000:0000:0000:ec0d:9a03:0078:649a",
              "gid list"); // split by one blank
usize
worker_fn(const usize &worker_id, Statics *s);

bool volatile running = true;

inline std::vector<std::string>
split(const std::string &str, const std::string &delim) {
    std::vector<std::string> res;
    if ("" == str)
        return res;
    char *strs = new char[str.length() + 1];
    strcpy(strs, str.c_str());

    char *d = new char[delim.length() + 1];
    strcpy(d, delim.c_str());

    char *p = strtok(strs, d);
    while (p) {
        std::string s = p;
        res.push_back(s);
        p = strtok(NULL, d);
    }

    return res;
}

int
main(int argc, char **argv) {
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    std::vector<Thread_t *> workers;
    std::vector<Statics> worker_statics(FLAGS_threads);

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

    r2::compile_fence();
    // gProfile.report();
    LOG(4) << "done";
}

usize
worker_fn(const usize &worker_id, Statics *s) {
    int nic_idx = FLAGS_use_nic_idx;
#ifdef VAL
    nic_idx = (worker_id) % 2;
    VALBinder::bind(nic_idx, worker_id % 24);
#endif
    r2::util::FastRandom r(0xdeafbeaf + worker_id);
    Statics &ss = *s;
#if 1
    const char *gid_str = FLAGS_gid.c_str();
#else
    std::vector<std::string> gids = split(FLAGS_gids, " ");
    assert(gids.size() > 0);
    const char *gid_str = gids[nic_idx % gids.size()].c_str();
#endif
    int qd = queue();
    assert(qd >= 0);
    int port = (worker_id) % 24;
    int conn_res = qconnect(qd, gid_str, strlen(gid_str), port);
    assert(conn_res == 1);
    const int req_len = 1;

    core_req_t req_list[FLAGS_or_sz];
    int mod = 1024 * 1024 * 4 / FLAGS_payload_sz;

    for (int i = 0; i < FLAGS_or_sz; ++i) {
        req_list[i] = {
                .addr = 0,
                .length = 8,
                .lkey = 32,
                .remote_addr= 0,
                .rkey = 32,
                .send_flags = 1,
                .vid = worker_id,
                .type = Read,
        };
    }

    push_core_req_t req = {.req_len=1, .req_list=req_list};
    pop_reply_t reply;

    while (running) {
        r2::compile_fence();
        req_list[0].remote_addr = 0;
        req_list[0].length = 8;
        assert(qpush(qd, &req) == 1);
        while (qpop(qd, &reply) != 1) {}

        req_list[0].remote_addr = 8;
        req_list[0].length = 58 * 3;
        assert(qpush(qd, &req) == 1);
        while (qpop(qd, &reply) != 1) {}

        ss.increment(1);

    }
    return 0;
}
