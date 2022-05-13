#include <gflags/gflags.h>

#include <assert.h>
#include <vector>

#include "../src/random.hh"
#include "../src/rdma/async_op.hh"
#include "profile.hh"
#include "../reporter.hh"
#include "rlib/benchs/thread.hh"
#include "rlib/core/lib.hh"
#include "rlib/core/qps/mod.hh"
#include "rlib/core/qps/recv_iter.hh"
#include "ud_msg.hh"

//#define VAL

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
DEFINE_int64(threads, 1, "#Threads used.");
DEFINE_int64(coroutines, 10, "#Coroutines used.");
DEFINE_string(client_name, "localhost", "Unique name to identify machine.");
DEFINE_int64(use_nic_idx, 0, "Which NIC to create QP");
DEFINE_int64(reg_nic_name, 73, "The name to register an opened NIC at rctrl.");
DEFINE_int64(reg_mem_name, 73, "The name to register an MR at rctrl.");
DEFINE_int64(op_count, 10, "The name to register an MR at rctrl.");
DEFINE_int64(run_sec, 10, "Running seconds");

usize
worker_fn(const usize &worker_id, Statics *s);

bool volatile running = true;

static double volatile passed_us = 0;
static u64 volatile count = 0;
static Profile gProfile = Profile(1);

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

    Statics &ss = *s;

    int run_cnt = 50;
    auto nic = RNic::create(RNicInfo::query_dev_names().at(nic_idx)).value();
    auto config = QPConfig();
    config.set_max_send(256).set_max_recv(256);
#if 1
    Profile profile = Profile(7);

    while(running) {
        r2::compile_fence();
        profile.start();
        auto nic = RNic::create(RNicInfo::query_dev_names().at(nic_idx)).value();
        // 1. driver context
        auto mem = Arc<RMem>(new RMem(1024 * 1024 * 4));
        auto local_mem = Arc<RMem>(new RMem(1024 * 1024 * 4));

        auto mr = RegHandler::create(mem, nic).value();

        SimpleAllocator alloc(mem, mr->get_reg_attr().value().key);
        auto recv_rs = RecvEntriesFactory<SimpleAllocator, 1, 4096>::create(alloc);

        // 2. ud communication bootstrap (handshake)
        auto ud = UD::create(nic, QPConfig().set_qkey(73)).value();
        auto recv_ud = UD::create(nic, QPConfig().set_qkey(73)).value();
        profile.tick_record(0);
        auto remote_ud_attr = UDAdapter::connect(nic, FLAGS_addr, "server_ud");
        UDAdapter::SendParam<1> param(remote_ud_attr, ud, recv_ud, recv_rs, mr);
        profile.tick_record(1);
        // 3. rc create
        auto qp = RC::create(nic, config).value();
        profile.tick_record(2);

        {
            Impl::bring_qp_to_init(qp->qp, config, nic);
        }
        profile.tick_record(3);

        // 4. connect
        std::string name = "client-rc" + std::to_string(worker_id);
        UDAdapter::CreateRCRes reply =
                UDAdapter::call_cc(param, qp->my_attr(), name);
        profile.tick_record(4);
        {
            // remote mr
            UDAdapter::FetchMRReply fetch_mr_res =
                    UDAdapter::call_fetch_mr(param, FLAGS_reg_mem_name);
            // start send request
            auto local_mr = RegHandler::create(local_mem, nic).value();
            qp->bind_local_mr(local_mr->get_reg_attr().value());
            qp->bind_remote_mr(fetch_mr_res.attr);
            profile.tick_record(5);

            qp->connect(qp->my_attr());
        }
        profile.tick_record(6);

        // 5. disconnect
        UDAdapter::call_disconnect(param, name, reply.key);
        ss.increment();         // finish one request
    profile.increase(1);
    }
    r2::compile_fence();
    // gProfile.append(profile);
#else
    Profile profile = Profile(3);

    for(int i  =0 ; i < 10; ++i) {
        auto qp = RC::create(nic, config).value();
            Impl::bring_qp_to_init(qp->qp, config, nic);
            qp->connect(qp->my_attr());
    }

    for (int i = 0; i < run_cnt; ++i) {
        profile.start();
        auto qp = RC::create(nic, config).value();
        profile.tick_record(0);
        {
            Impl::bring_qp_to_init(qp->qp, config, nic);
        }
        profile.tick_record(1);

        auto attr = qp->my_attr();
        {
            qp->connect(attr);
        }

        profile.tick_record(2);
    }
    profile.increase(run_cnt);
#endif
//    profile.report("end-to-end");
    return 0;
}
