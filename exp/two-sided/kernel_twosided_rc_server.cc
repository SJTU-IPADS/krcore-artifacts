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

#include "./val/cpu.hh"

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
DEFINE_string(gid, "fe80:0000:0000:0000:ec0d:9a03:0078:645e", "Gid target");
DEFINE_int64(threads, 1, "#Threads used.");
DEFINE_int64(port, 8888, "#Port base");
DEFINE_int64(host_len, 1, "Client host len");
DEFINE_int64(vid, 1, "#Vid");
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
int k_cnt = 0;

bool volatile running = true;
static int max_pop_cnt = 0;

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
  sleep(FLAGS_run_sec);
  //  Reporter::report_thpt(worker_statics, 10); // report for 10 seconds

  running = false; // stop workers

  // wait for workers to join
  for (auto w : workers) {
    w->join();
  }

  r2::compile_fence();
  // gProfile.report();
  LOG(4) << "max cnt:" << max_pop_cnt;
  LOG(4) << "done";
}

#define SINGLE 0

usize
worker_fn(const usize& worker_id, Statics* s)
{
  int qd = queue();
  int res = 0;
  int vid = 0;
  const int pop_cnt = FLAGS_or_sz;
  int pop_cache[FLAGS_host_len + 1];
  for (int i = 0; i < FLAGS_host_len + 1; ++i)
    pop_cache[i] = 0;

  pop_reply_t reply;
  Statics& ss = *s;

  const char* gid_str = FLAGS_gid.c_str();
  const int port = FLAGS_port + worker_id;
  assert(1 == qbind(qd, port));

  core_req_t req_list[pop_cnt];

  for (int i = 0; i < pop_cnt; ++i) {
    req_list[i] = { .addr = 1024,
                    .length = 0,
                    .lkey = 32,
                    .remote_addr = 2048,
                    .rkey = 32,
                    .send_flags = 0,
                    .vid = 0,
                    .type = SendImm };
  }

  while (running) {
    reply.pop_count = 0;
    if (qpop_msgs(qd, &reply, pop_cnt, FLAGS_payload_sz) == ok &&
        reply.pop_count > 0) { // accept and poll recv cq
      int act_pop_cnt = reply.pop_count;
      assert(act_pop_cnt <= pop_cnt);
#if 1
      for (int i = 0; i < act_pop_cnt; ++i) {
        vid = reply.wc[i].imm_data;
        assert(vid != 0);
        req_list[i].vid = vid;
        req_list[i].remote_addr = reply.wc[i].wc_wr_id;
      }
      push_core_req_t req = { .req_len = act_pop_cnt, .req_list = req_list };
      assert(ok == qpush(qd, &req, 1, act_pop_cnt));
#endif
    }
  }
exit:
  assert(1 == qunbind(qd, port));
  return 0;
}
