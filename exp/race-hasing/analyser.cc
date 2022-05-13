#include "../src/rdma/sop.hh"
#include "../reporter.hh"
#include "rlib/benchs/thread.hh"
#include "rlib/core/lib.hh"
#include "rlib/core/qps/rc_recv_manager.hh"
#include "rlib/core/qps/recv_iter.hh"
#include "../huge_region.hh"
using namespace nvm;

#include <gflags/gflags.h>
#include <vector>

// int64
DEFINE_int64(threads, 1, "#Threads used.");
DEFINE_int64(port, 8888, "Server listener (UDP) port.");
DEFINE_int64(host_len, 5, "Client host len");
DEFINE_int64(use_nic_idx, 0, "Which NIC to create QP");
DEFINE_int64(payload_sz, 64, "Payload size (bytes)");
DEFINE_int64(reg_nic_name, 73, "The name to register an opened NIC at rctrl.");
DEFINE_int64(reg_mem_name, 73, "The name to register an MR at rctrl.");
DEFINE_int64(seq_limit, 4096, "seq size for overflow test");
DEFINE_int64(silent_sec, 2, "silent time used to overflow");
DEFINE_int64(write_imm, 1, "write imm");
DEFINE_int64(id, 0, "node id");
DEFINE_int64(run_sec, 10, "running seconds");
DEFINE_int64(tick_interval_us, 10, "Tick interval");

// string
DEFINE_string(name_recv, "server-qp", "Server address to connect to.");
DEFINE_string(name_client, "client-qp", "Server address to connect to.");
DEFINE_uint64(magic_num, 0xdeadbeaf, "The magic number read by the client");

using namespace rdmaio;
using namespace rdmaio::rmem;
using namespace rdmaio::qp;
using namespace r2;
using namespace r2::rdma;
using Thread_t = bench::Thread<usize>;
bool volatile running = true;

const u64 kMsgArea = 1024 * 8;

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
worker_fn(const usize& worker_id, Statics* s);

int
main(int argc, char** argv)
{

    gflags::ParseCommandLineFlags(&argc, &argv, true);
    RDMA_LOG(3) <<  "sanity check write imm: " << FLAGS_write_imm;

    std::vector<Thread_t*> workers;
    std::vector<Statics> worker_statics(FLAGS_threads); // setup workers

    for (uint i = 0; i < FLAGS_threads; ++i) {
        workers.push_back(
                new Thread_t(std::bind(worker_fn, i, &(worker_statics[i]))));
    }

    // start the workers
    for (auto w : workers) {
        w->start();
    }

    Reporter::report_thpt(worker_statics, FLAGS_run_sec, FLAGS_tick_interval_us);
//    sleep(FLAGS_run_sec);
    running = false; // stop workers

    // wait for workers to join
    for (auto w : workers) {
        w->join();
    }
}

usize
worker_fn(const usize& worker_id, Statics* s)
{

    const usize entry_cnt = 2048;
    // start a controller, so that others may access it using UDP based channel
    int port = worker_id + FLAGS_port;
    RCtrl ctrl(port);
    RDMA_LOG(rdmaio::WARNING) << "Pingping server listenes at localhost:" << port;
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

    // 5. start server

    Arc<rdmaio::qp::RC> recv_qp;
    Arc<rdmaio::qp::RecvEntries<entry_cnt>> recv_rs;
    // wait until connection from client

    RDMA_LOG(rdmaio::INFO) << "get client connection via RCM. client name ["
                           << FLAGS_name_client << "]";
    //auto local_mem = Arc<RMem>(new RMem(4 * 1024 * 1024));
    auto local_region = HugeRegion::create(16 * 1024 * 1024).value();
    auto local_mem = local_region->convert_to_rmem().value();

    auto local_mr = RegHandler::create(local_mem, nic).value();
    char* buf = (char*)(local_mr->get_reg_attr().value().buf);

    size_t recv_seq = 0, recv_cnt = 0, total_msg_cnt = -1;
    r2::rdma::SROp op;
    ibv_wc wcs[entry_cnt];
    Arc<rdmaio::qp::RC> rc_cache[FLAGS_host_len];
    int offset_list[FLAGS_host_len];
    Arc<rdmaio::qp::RecvEntries<entry_cnt>> recv_cache[FLAGS_host_len];
    int post_cnt[FLAGS_host_len];
    for (int i = 0; i < FLAGS_host_len; ++i) {
        post_cnt[i] = 0;
        rc_cache[i] = nullptr;
        offset_list[i] = 0;
        auto recv_mem = Arc<RMem>(new RMem(1024 * 1024 * 16));
        auto handler = RegHandler::create(recv_mem, nic).value();

        SimpleAllocator alloc(recv_mem, handler->get_reg_attr().value().key);

        auto res =
                RecvEntriesFactory<SimpleAllocator, entry_cnt, 4096>::create(alloc);

        recv_cache[i] = res;
    }
    auto local_reg_attr = local_mr->get_reg_attr().value();
    ctrl.start_daemon();

    while (running) {
        uint64_t total = 0;
        for (RecvIter<RC, entry_cnt> iter(recv_cq, wcs); iter.has_msgs();
             iter.next()) {
            auto imm_msg = iter.cur_msg().value();
            auto host_val = static_cast<uint32_t>(std::get<0>(imm_msg));

            recv_qp = rc_cache[host_val];
            if (unlikely(recv_qp == nullptr)) { // first time, cache up the host info
                const std::string host = std::to_string(host_val);
//                 recv_qp = ctrl.registered_qps.query(host).value();
                rdmaio::qp::RC* qp = dynamic_cast<rdmaio::qp::RC*>(ctrl.registered_qps.query(host).value().get());
                recv_qp = Arc<rdmaio::qp::RC>(qp);
                recv_rs = manager.reg_recv_entries.query(host).value();
                recv_qp->bind_local_mr(local_reg_attr);
                rc_cache[host_val] = recv_qp;
            }

            recv_qp->post_recvs(*recv_rs, 1);

            auto msg_buf = static_cast<uint64_t *>(std::get<1>(imm_msg));
            total += *msg_buf;
        }
        if (total > 0) {
//            RDMA_LOG(2) << "Total:" << total;
            s->increment(total);
        }

    }
}
