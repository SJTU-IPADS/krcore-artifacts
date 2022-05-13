#include <gflags/gflags.h>

#include <assert.h>
#include <vector>

#include "../../include/syscall.h"
#include "../src/random.hh"
#include "../src/rdma/async_op.hh"
#include "profile.hh"
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
DEFINE_int64(run_sec, 10, "running seconds");

usize
worker_fn(const usize& worker_id, Statics* s);

bool volatile running = true;

static double volatile passed_us = 0;
static Profile gProfile = Profile(1);

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
worker_fn(const usize& worker_id, Statics* s) {
    int nic_idx = FLAGS_use_nic_idx;
#ifdef VAL
    nic_idx = (worker_id) % 2;
    VALBinder::bind(nic_idx, worker_id % 24);
#endif
    Statics &ss = *s;
    const char *gid_str = FLAGS_gid.c_str();
    Profile profile = Profile(1);
//   sleep(1);

    int qd = queue();
    assert(qd >= 0);
    int port = (worker_id) % 24;
    qconnect(qd, gid_str, strlen(gid_str), port);

    profile.start();
    while (running) {
        qconnect(qd, gid_str, strlen(gid_str), port);
        profile.increase(1);
        ss.increment();
    }
    profile.tick_record(0);
    profile.report("end-to-end");
}