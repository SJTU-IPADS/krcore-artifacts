#include <gflags/gflags.h>

#include <assert.h>
#include <vector>

#include "../src/random.hh"
#include "../src/rdma/sop.hh"
#include "../reporter.hh"
#include "rlib/benchs/thread.hh"
#include "rlib/core/lib.hh"
#include <random>


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

static inline unsigned long get_random() {
    static std::random_device rd;
    static std::mt19937_64 e(rd());
    return e();
}

DEFINE_int64(threads, 1, "#Threads used.");
DEFINE_int64(payload_sz, 64, "Payload size (bytes)");   // set default payload size to 64 bytes
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

// TODO: add required params if necessilary

usize worker_fn(const usize &worker_id, Statics *s);

bool volatile running = true;

int main(int argc, char **argv) {
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    std::vector<Thread_t *> workers;
    std::vector<Statics> worker_statics(FLAGS_threads); // setup workers

    for (uint i = 0; i < FLAGS_threads; ++i) {
        workers.push_back(
                new Thread_t(std::bind(worker_fn, i, &(worker_statics[i]))));
    }

    // start the workers
    for (auto w : workers) {
        w->start();
    }

    Reporter::report_thpt(worker_statics, FLAGS_run_sec, 1000000,FLAGS_or_sz); // report for 10 seconds
    running = false;                           // stop workers

    // wait for workers to join
    for (auto w : workers) {
        w->join();
    }
    RDMA_LOG(4) << "done";
}

// main func for every single worker
usize worker_fn(const usize &worker_id, Statics *s) {
    int nic_idx = FLAGS_use_nic_idx;
#ifdef VAL
    nic_idx = (worker_id) % 2;
    VALBinder::bind(nic_idx, worker_id % 24);
#endif
    // remote server nic
    int remote_nic_idx = nic_idx;
    auto nic =
            RNic::create(RNicInfo::query_dev_names().at(nic_idx)).value();

    // 1. create a local QP to use
    r2::util::FastRandom r(0xdeafbeaf + worker_id);
    std::string name = "client-qp-" + std::to_string(worker_id) + "-" + std::to_string(get_random());

    auto qp = RC::create(nic, QPConfig()).value();

    // 2. create the pair QP at server using CM
    ConnectManager cm(FLAGS_addr);
    if (cm.wait_ready(1000000, 2) ==
        IOCode::Timeout) // wait 1 second for server to ready, retry 2 times
        RDMA_ASSERT(false) << "cm connect to server timeout";

    RDMA_LOG(rdmaio::DEBUG) << "client worker_id = " << worker_id << ", rc_name = " << name;
    auto qp_res = cm.cc_rc(name, qp, FLAGS_reg_nic_name + remote_nic_idx, QPConfig());
    RDMA_ASSERT(qp_res == IOCode::Ok) << std::get<0>(qp_res.desc);
    auto key = std::get<1>(qp_res.desc);
    RDMA_LOG(rdmaio::DEBUG) << "client fetch QP authentical key: " << key;

    // 3. create the local MR for usage, and create the remote MR for usage
    auto local_mem = Arc<RMem>(new RMem(FLAGS_mem_sz));
    auto local_mr = RegHandler::create(local_mem, nic).value();

    auto fetch_res = cm.fetch_remote_mr(FLAGS_reg_mem_name + remote_nic_idx);
    RDMA_ASSERT(fetch_res == IOCode::Ok) << std::get<0>(fetch_res.desc);
    rmem::RegAttr remote_attr = std::get<1>(fetch_res.desc);

    qp->bind_remote_mr(remote_attr);
    qp->bind_local_mr(local_mr->get_reg_attr().value());

    RDMA_LOG(4) << "t-" << worker_id << " started";
    // start
    u64 *test_buf = (u64 *) (local_mem->raw_ptr);
    *test_buf = 0;
    u64 offset;
    SROp op;

    if (FLAGS_rdma_op == 0) {
        op.set_read();
    } else {
        op.set_write();
    }
    op.set_payload(test_buf, FLAGS_payload_sz);

    int mod = FLAGS_mem_sz / FLAGS_payload_sz;
    int send_flags = 0;
    if (FLAGS_rdma_op && FLAGS_payload_sz < 64) send_flags |= IBV_SEND_INLINE;

    Timer t;
    while (running) {
        r2::compile_fence();
        // if FLAGS_or_sz == 1, then same with sync
        int offset = r.next() % mod;
        op.set_payload(test_buf, FLAGS_payload_sz).set_remote_addr(offset * FLAGS_payload_sz);
        RDMA_ASSERT(op.execute_my(qp, IBV_SEND_SIGNALED | send_flags) == IOCode::Ok);
        for (uint i = 0; i < FLAGS_or_sz - 1; ++i) {
            int offset = r.next() % mod;
            op.set_payload(test_buf + i ,
                           FLAGS_payload_sz).set_remote_addr(offset * FLAGS_payload_sz);
            RDMA_ASSERT(op.execute_my(qp, send_flags) == IOCode::Ok);
        }
        RDMA_ASSERT(qp->wait_one_comp() == IOCode::Ok);

        s->increment(FLAGS_or_sz); // finish one batch
    }
    s->data.lat = t.passed_msec(); // latency record
    auto del_res = cm.delete_remote_rc(name, key);
    RDMA_ASSERT(del_res == IOCode::Ok)
            << "delete remote QP error: " << del_res.desc;
    RDMA_LOG(4) << "t-" << worker_id << " stoped";
    return 0;
}
