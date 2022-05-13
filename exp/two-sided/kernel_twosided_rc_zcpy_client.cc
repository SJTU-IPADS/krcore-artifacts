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

//#define VAL

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
DEFINE_int64(payload_sz,
64,
"Payload size (bytes)"); // set default payload size to 64 bytes
DEFINE_int64(vid, 1, "Vid assigned to a machine");
DEFINE_string(gid, "fe80:0000:0000:0000:ec0d:9a03:0078:645e", "Gid target");
DEFINE_int64(threads, 1, "#Threads used.");
DEFINE_int64(port, 8888, "#Port base");
DEFINE_int64(coroutines, 10, "#Coroutines used.");
DEFINE_string(client_name, "localhost", "Unique name to identify machine.");
DEFINE_int64(use_nic_idx, 0, "Which NIC to create QP");
DEFINE_int64(reg_nic_name, 73, "The name to register an opened NIC at rctrl.");
DEFINE_int64(reg_mem_name, 73, "The name to register an MR at rctrl.");
DEFINE_int64(op_count, 10, "The name to register an MR at rctrl.");
DEFINE_int64(run_sec, 10, "Running seconds");
DEFINE_int64(
        or_sz,
1,
"Serve for the outstanding request. We use batch size to annote the step");
DEFINE_string(gids,
"fe80:0000:0000:0000:ec0d:9a03:00ca:31d8 "
"fe80:0000:0000:0000:ec0d:9a03:0078:649a",
"gid list"); // split by one blank
usize
worker_fn(const usize& worker_id, Statics* s);

bool volatile running = true;

inline std::vector<std::string>
split(const std::string& str, const std::string& delim)
{
    std::vector<std::string> res;
    if ("" == str)
        return res;
    char* strs = new char[str.length() + 1];
    strcpy(strs, str.c_str());

    char* d = new char[delim.length() + 1];
    strcpy(d, delim.c_str());

    char* p = strtok(strs, d);
    while (p) {
        std::string s = p;
        res.push_back(s);
        p = strtok(NULL, d);
    }

    return res;
}
int retry_pop_cnt = 0;
int total_batch_cnt = 0;
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
    Reporter::report_thpt(worker_statics, FLAGS_run_sec); // report for 10 seconds
    running = false;                                      // stop workers

    // wait for workers to join
    for (auto w : workers) {
        w->join();
    }

    r2::compile_fence();
    // gProfile.report();
    LOG(2) << "retry total cnt:" << retry_pop_cnt << ", retry op per batch:"
           << double(retry_pop_cnt) / double(total_batch_cnt);
    LOG(4) << "done";
}

usize
worker_fn(const usize& worker_id, Statics* s)
{
    int qd = queue();
    int res = 0;
    int cnt = 0;
    pop_reply_t reply;
    Statics& ss = *s;

    const char* gid_str = FLAGS_gid.c_str();
    const int vid = FLAGS_vid;
    const int port = FLAGS_port + worker_id;
    qconnect(qd, gid_str, strlen(gid_str), port, vid);
    core_req_t req_list[FLAGS_or_sz];
    bool large_msg = FLAGS_payload_sz >= 1024 * 64; // we name Large Message as the payload size larger than 64K

    for (int i = 0; i < FLAGS_or_sz; ++i) {
        req_list[i] = { .addr = 32,
                .length = large_msg ? 8 : FLAGS_payload_sz,
                .lkey = 32,
                .remote_addr = 2048,
                .rkey = 32,
                .send_flags = i == 0 ? 1 : 0,
                .vid = vid,
                .type = SendImm };
    }
    push_core_req_t req = { .req_len = FLAGS_or_sz, .req_list = req_list };

    while (running) {
        assert(1 == qpush(qd, &req, 1, FLAGS_or_sz));
#if 1
        {
            int polled_cnt = 0;
            while (polled_cnt < FLAGS_or_sz && running) {
                reply.pop_count = 0;
                int res = qpop_msgs(qd, &reply, FLAGS_or_sz, large_msg ? 0 : FLAGS_payload_sz);
                polled_cnt += reply.pop_count;
                if (polled_cnt < FLAGS_or_sz) // not polled at first time
                    retry_pop_cnt++;
            }
            ++total_batch_cnt;
            ss.increment(FLAGS_or_sz);
        }
#endif
        //        sleep(1);
    }

    return 0;
}
