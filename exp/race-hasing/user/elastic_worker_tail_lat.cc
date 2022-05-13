#include "../../../include/syscall.h"
#include "../../huge_region.hh"
#include "../src/random.hh"
#include "../src/rdma/sop.hh"
#include "../../reporter.hh"
#include "rlib/benchs/thread.hh"
#include "rlib/core/lib.hh"
#include "rlib/core/qps/rc_recv_manager.hh"
#include "rlib/core/qps/recv_iter.hh"
#include <random>

using namespace nvm;

#include <gflags/gflags.h>
#include <vector>

// int64
DEFINE_int64(threads, 1, "#Threads used.");
DEFINE_int64(port, 8888, "Server listener (UDP) port.");
DEFINE_int64(host_len, 1, "Client host len");
DEFINE_int64(use_nic_idx, 0, "Which NIC to create QP");
DEFINE_int64(payload_sz, 64, "Payload size (bytes)");
DEFINE_int64(reg_nic_name, 73, "The name to register an opened NIC at rctrl.");
DEFINE_int64(reg_mem_name, 73, "The name to register an MR at rctrl.");
DEFINE_int64(seq_limit, 4096, "seq size for overflow test");
DEFINE_int64(silent_sec, 2, "silent time used to overflow");
DEFINE_int64(write_imm, 0, "write imm");
DEFINE_int64(id, 0, "node id");
DEFINE_int64(run_sec, 10, "running seconds");
DEFINE_int64(worker_num, 2, "Worker_num");
DEFINE_int64(
        or_sz,
1,
"Serve for the outstanding request. We use batch size to annote the step");

// string
DEFINE_string(name_recv, "server-qp", "Server address to connect to.");
DEFINE_string(name_client, "client-qp", "Server address to connect to.");
DEFINE_uint64(magic_num, 0xdeadbeaf, "The magic number read by the client");
DEFINE_string(addr, "localhost", "Host of the machine");
DEFINE_string(analyser_addr, "localhost:8888", "Host of the machine");
DEFINE_string(server_gid,
"fe80:0000:0000:0000:248a:0703:009c:7c94",
"server gid to connect and send");
DEFINE_string(server_addr, "localhost:8888", "Server address to connect to.");
DEFINE_int64(rdma_op, 0, "RDMA operation");
DEFINE_int64(mem_sz, 4194304, "Mr size"); // 4M
DEFINE_int64(tick_interval_us, 10, "Tick interval");
DEFINE_int64(host, 1, "Server address to connect to.");
#define MR_SIZE 1024 * 1024 * 4

using namespace rdmaio;
using namespace rdmaio::rmem;
using namespace rdmaio::qp;
using namespace r2;
using namespace r2::rdma;
using Thread_t = bench::Thread<usize>;

bool volatile running = true;
bool volatile triggered = false;

static inline unsigned long
get_random()
{
    static std::random_device rd;
    static std::mt19937_64 e(rd());
    return e();
}

class SimpleAllocator : public AbsRecvAllocator
{
    RMem::raw_ptr_t buf = nullptr;
    usize total_mem = 0;
    mr_key_t key;

public:
    SimpleAllocator(Arc<RMem> mem, mr_key_t key)
            : buf(mem->raw_ptr)
            , total_mem(mem->sz)
            , key(key)
    {
        // RDMA_LOG(4) << "simple allocator use key: " << key;
    }

    r2::Option<std::pair<rmem::RMem::raw_ptr_t, rmem::mr_key_t>> alloc_one(
            const usize& sz) override
    {
        if (total_mem < sz)
            return {};
        auto ret = buf;
        buf = static_cast<char*>(buf) + sz;
        total_mem -= sz;
        return std::make_pair(ret, key);
    }

    r2::Option<std::pair<rmem::RMem::raw_ptr_t, rmem::RegAttr>>
    alloc_one_for_remote(const usize& sz) override
    {
        return {};
    }
};

usize
main_worker_fn(const usize& worker_id, Statics* s);

usize
analyser_fn(const usize& worker_id, Statics* s);
static std::vector<Thread_t*> workers;
static std::vector<Thread_t*> analysers;
static std::vector<Statics> worker_statics(FLAGS_threads); // setup workers
static volatile uint64_t total_operations = 0;

int
main(int argc, char** argv)
{

    gflags::ParseCommandLineFlags(&argc, &argv, true);

    for (uint i = 0; i < FLAGS_threads; ++i) {
        workers.push_back(
                new Thread_t(std::bind(main_worker_fn, i, &(worker_statics[i]))));
    }
    for (uint i = 0; i < FLAGS_threads; ++i) {
        analysers.push_back(
                new Thread_t(std::bind(analyser_fn, i, &(worker_statics[i]))));
    }

    triggered = false;
    analysers[0]->start(); // analyser thread
    workers[0]->start(); // start first one

    sleep(FLAGS_run_sec);
    running = false;                       // stop workers

    // wait for workers to join
    for (auto w : workers) {
        w->join();
    }

    for (auto w : analysers) {
        w->join();
    }
}

usize
worker(const usize& worker_id, Statics* s)
{
    int nic_idx = FLAGS_use_nic_idx;
#ifdef VAL
    nic_idx = (worker_id) % 2;
  VALBinder::bind(nic_idx, worker_id % 24);
#endif
    // remote server nic
    int remote_nic_idx = nic_idx;
    auto nic = RNic::create(RNicInfo::query_dev_names().at(nic_idx)).value();

    // 1. create a local QP to use
    r2::util::FastRandom r(0xdeafbeaf + worker_id);
    std::string name = "client-qp-" + std::to_string(worker_id) + "-" +
                       std::to_string(get_random());

    auto qp = RC::create(nic, QPConfig()).value();

    // 2. create the pair QP at server using CM
    ConnectManager cm(FLAGS_server_addr);
    if (cm.wait_ready(1000000, 50) ==
        IOCode::Timeout) // wait 1 second for server to ready, retry 2 times
        RDMA_ASSERT(false) << "cm connect to server timeout";

    int key = 0;

    while (running) {
        auto qp_res =
                cm.cc_rc(name, qp, FLAGS_reg_nic_name + remote_nic_idx, QPConfig());
        if (qp_res == IOCode::Ok) {
            key = std::get<1>(qp_res.desc);
            break;
        }
    }

    // 3. create the local MR for usage, and create the remote MR for usage
    auto local_mem = Arc<RMem>(new RMem(FLAGS_mem_sz));
    auto local_mr = RegHandler::create(local_mem, nic).value();

    rmem::RegAttr remote_attr;
    while (running) {
        auto fetch_res = cm.fetch_remote_mr(FLAGS_reg_mem_name + remote_nic_idx);
        if (fetch_res == IOCode::Ok) {
            remote_attr = std::get<1>(fetch_res.desc);
            break;
        }
    }

    qp->bind_remote_mr(remote_attr);
    qp->bind_local_mr(local_mr->get_reg_attr().value());

    // start
    u64* test_buf = (u64*)(local_mem->raw_ptr);
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
    if (FLAGS_rdma_op && FLAGS_payload_sz < 64)
        send_flags |= IBV_SEND_INLINE;

    Timer t;
    if (triggered)
        RDMA_LOG(2) << "start sending";
    while (running) {
        r2::compile_fence();
        // if FLAGS_or_sz == 1, then same with sync
        for(int i = 0 ; i < 3 ;++i) {
            for(int j = 0 ; j < FLAGS_or_sz; ++j) {
                int offset = r.next() % mod;
                if (i == 3) {
                    op.set_write();
                } else {
                    op.set_read();
                }
                op.set_payload(test_buf, FLAGS_payload_sz)
                        .set_remote_addr(offset * FLAGS_payload_sz);
                RDMA_ASSERT(op.execute_my(qp, j == 0 ? IBV_SEND_SIGNALED : 0) == IOCode::Ok);
            }
            RDMA_ASSERT(qp->wait_one_comp() == IOCode::Ok);
        }

        total_operations += FLAGS_or_sz;
    }
    s->data.lat = t.passed_msec(); // latency record
    auto del_res = cm.delete_remote_rc(name, key);
    RDMA_ASSERT(del_res == IOCode::Ok)
            << "delete remote QP error: " << del_res.desc;
    return 0;
}

usize
main_worker_fn(const usize& worker_id, Statics* s)
{
    if (triggered) {
        worker(worker_id, s);
    } else {
        const usize entry_cnt = 2048;
        Statics& ss = *s;
        r2::util::FastRandom r(0xdeafbeaf + worker_id);

        // start a controller, so that others may access it using UDP based channel
        int port = worker_id + FLAGS_port;
        RCtrl ctrl(port);
        RecvManager<entry_cnt, 4096> manager(ctrl); // create manager

        // 1. first we open the NIC
        //    and reg it to ctrl
        auto nic =
                RNic::create(RNicInfo::query_dev_names().at(FLAGS_use_nic_idx)).value();
        RDMA_ASSERT(ctrl.opened_nics.reg(FLAGS_reg_nic_name, nic));

        // 2. prepare recv cq
        auto recv_cq_res =
                ::rdmaio::qp::Impl::create_cq(nic, entry_cnt); // buffer with [entry_cnt]
        RDMA_ASSERT(recv_cq_res == IOCode::Ok);
        auto recv_cq = std::get<0>(recv_cq_res.desc);

        // 3. mem region
        /*
        auto mem =
          Arc<RMem>(new RMem(16 * 1024 * 1024)); // allocate a memory with 4M bytes
        */
        auto region = HugeRegion::create(16 * 1024 * 1024).value();
        auto mem = region->convert_to_rmem().value();

        RDMA_ASSERT(mem->valid());

        auto reg_handler = RegHandler::create(mem, nic).value();
        char* buf_0 = (char*)(reg_handler->get_reg_attr().value().buf);

        // reg mr to ctrl
        RDMA_ASSERT(ctrl.registered_mrs.reg(FLAGS_reg_mem_name, reg_handler));

        // 4. reg recv cq at server side
        auto alloc = std::make_shared<SimpleAllocator>(
                mem, reg_handler->get_reg_attr().value().key);
        RDMA_ASSERT(
                manager.reg_recv_cqs.create_then_reg(FLAGS_name_recv, recv_cq, alloc));

        ibv_wc wcs[entry_cnt];
        ctrl.start_daemon();

        std::vector<Thread_t*> subworker;
        subworker.push_back(new Thread_t(std::bind(worker, worker_id, s)));

        subworker[0]->start();

        while (running) {

            for (RecvIter<RC, entry_cnt> iter(recv_cq, wcs); iter.has_msgs();
                 iter.next()) {
                // Do connect work
                triggered = true;
                RDMA_LOG(2) << "Trigger elastic";
                {
                    // fork all of other clients
                    for (uint i = 1; i < FLAGS_threads; ++i) {
                        workers[i]->start();
                    }
                }
                break;
            }
        }
    }
}

usize
analyser_fn(const usize& worker_id, Statics* s)
{
    int nic_idx = FLAGS_use_nic_idx;
#ifdef VAL
    nic_idx = (worker_id) % 2;
  VALBinder::bind(nic_idx, worker_id % 24);
#endif
    // 1. create a local QP to use
    r2::util::FastRandom r(0xdeafbeaf + worker_id + 73 * FLAGS_id);
    std::string name =
            (std::to_string(worker_id) + "-" + std::to_string(get_random()))
                    .substr(0, 8);
    name = std::to_string(FLAGS_host);
    auto nic = RNic::create(RNicInfo::query_dev_names().at(nic_idx)).value();

    auto recv_cq_res =
            ::rdmaio::qp::Impl::create_cq(nic, 16); // buffer with [entry_cnt]
    RDMA_ASSERT(recv_cq_res == IOCode::Ok);
    auto recv_cq = std::get<0>(recv_cq_res.desc);
    auto qp = RC::create(nic, QPConfig(), recv_cq).value();

    // 2. create the pair QP at server using CM
    ConnectManager cm(FLAGS_analyser_addr);
    if (cm.wait_ready(1000000, 2) ==
        IOCode::Timeout) // wait 1 second for server to ready, retry 2 times
        RDMA_ASSERT(false) << "cm connect to server timeout";

    auto qp_res = cm.cc_rc_msg(
            name, FLAGS_name_recv, 1024, qp, FLAGS_reg_nic_name, QPConfig());
    RDMA_ASSERT(qp_res == IOCode::Ok) << std::get<0>(qp_res.desc);
    auto key = std::get<1>(qp_res.desc); // fetch remote auth_key, which could be
    // used when post RDMA requests
    // 3. create the local MR for usage, and create the remote MR for usage

    auto region = HugeRegion::create(1024 * 1024 * 16).value();
    auto local_mem = region->convert_to_rmem().value();
    auto local_mr = RegHandler::create(local_mem, nic).value();

    auto recv_mem = Arc<RMem>(new RMem(1024 * 1024 * 16));
    auto handler = RegHandler::create(recv_mem, nic).value();

    SimpleAllocator alloc(recv_mem, handler->get_reg_attr().value().key);

    auto recv_rs = RecvEntriesFactory<SimpleAllocator, 1024, 1024>::create(alloc);

    auto fetch_res =
            cm.fetch_remote_mr(FLAGS_reg_mem_name); // rpc call to get remote mr
    RDMA_ASSERT(fetch_res == IOCode::Ok) << std::get<0>(fetch_res.desc);
    rmem::RegAttr remote_attr = std::get<1>(fetch_res.desc);

    //  remote_attr.key = 0;
    qp->bind_remote_mr(remote_attr);
    qp->bind_local_mr(local_mr->get_reg_attr().value());

    /*This is the example code usage of the fully created RCQP */
    char* buf = (char*)(local_mr->get_reg_attr().value().buf);
    // LOG(4) << "Send me attr: " << local_mr->get_reg_attr().value().key << "@"
    // << worker_id; *buf = 'A';
    *((uint64_t *)buf) = 233;

    //  u64 offset;
    SROp op;

    op.set_imm(FLAGS_host)
            .set_payload(buf, 8)
            .set_remote_addr(0)
            .set_op(IBV_WR_SEND_WITH_IMM);

    while (running) {
        r2::compile_fence();
        // post recv at first
        *((uint64_t *)buf) = total_operations;
        RDMA_ASSERT(op.execute_sync(qp, IBV_SEND_SIGNALED) == IOCode::Ok);
        total_operations = 0;
        usleep(FLAGS_tick_interval_us);
    }
    // finally, some clean up, to delete my created QP at server
    auto del_res = cm.delete_remote_rc(name, key);
    RDMA_ASSERT(del_res == IOCode::Ok)
            << "delete remote QP error: " << del_res.desc;
    return 0;
}
