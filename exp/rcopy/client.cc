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
DEFINE_int64(payload_sz, 7340032, "Payload size (bytes)");   // set default payload size to 64 bytes
DEFINE_int64(rdma_op, 0, "RDMA operation");
DEFINE_int64(run_sec, 10, "Running seconds");
// For RDMA
DEFINE_string(addr, "localhost:8888", "Server address to connect to."); // let val13 to be the server
DEFINE_int64(use_nic_idx, 0, "Which NIC to create QP");
DEFINE_int64(reg_nic_name, 73, "The name to register an opened NIC at rctrl.");
DEFINE_int64(reg_mem_name, 73, "The name to register an MR at rctrl.");
DEFINE_int64(mem_sz, 17340032, "Mr size");       // 4M
DEFINE_int64(port, 8888, "Server listener (UDP) port.");
DEFINE_uint32(nic_mod, 2, "NIC use mod");
DEFINE_int64(or_sz, 12, "Serve for the outstanding request. We use batch size to annote the step");

// TODO: add required params if necessilary

usize worker_fn(const usize &worker_id, Statics *s);

bool volatile running = true;

int main(int argc, char **argv) {
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    int nic_idx = FLAGS_use_nic_idx, worker_id = 0;
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

    auto qp_res = cm.cc_rc(name, qp, FLAGS_reg_nic_name + remote_nic_idx, QPConfig());
    RDMA_ASSERT(qp_res == IOCode::Ok) << std::get<0>(qp_res.desc);
    auto key = std::get<1>(qp_res.desc);

    // 3. create the local MR for usage, and create the remote MR for usage
    auto local_mem = Arc<RMem>(new RMem(FLAGS_mem_sz));
    auto local_mr = RegHandler::create(local_mem, nic).value();

    auto fetch_res = cm.fetch_remote_mr(FLAGS_reg_mem_name + remote_nic_idx);
    RDMA_ASSERT(fetch_res == IOCode::Ok) << std::get<0>(fetch_res.desc);
    rmem::RegAttr remote_attr = std::get<1>(fetch_res.desc);

    qp->bind_remote_mr(remote_attr);
    qp->bind_local_mr(local_mr->get_reg_attr().value());

    // start
    u64 *test_buf = (u64 *) (local_mem->raw_ptr);
    u64 offset;
    SROp op;

    if (FLAGS_rdma_op == 0) {
        op.set_read();
    } else {
        op.set_write();
    }

    op.set_payload(test_buf, FLAGS_payload_sz);
    RDMA_ASSERT(op.execute_my(qp, IBV_SEND_SIGNALED) == IOCode::Ok);
    RDMA_ASSERT(qp->wait_one_comp() == IOCode::Ok);
    op.set_payload(test_buf, 8);
    RDMA_ASSERT(op.execute_my(qp, IBV_SEND_SIGNALED) == IOCode::Ok);
    RDMA_ASSERT(qp->wait_one_comp() == IOCode::Ok);

    for (int i = 0; i < 1; ++i) {
        r2::compile_fence();
        int fd = open("/home/lfm/rcopy.txt", O_RDWR);
        char *ptr1 = (char *) mmap(NULL, FLAGS_payload_sz, PROT_WRITE, MAP_SHARED, fd, 0);
        memcpy(ptr1, test_buf, FLAGS_payload_sz);
        close(fd);
    }
    auto del_res = cm.delete_remote_rc(name, key);
    return 0;
}
