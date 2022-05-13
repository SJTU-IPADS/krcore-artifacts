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

int
main(int argc, char** argv)
{
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  Profile profile = Profile(3);

  profile.start();
  RDMA_ASSERT(ibv_fork_init() == 0);
  profile.tick_record(0);
  // =======
  usize nic_idx = 0;
  r2::compile_fence();
  auto nic = RNic::create(RNicInfo::query_dev_names().at(nic_idx))
               .value(); // open device
#if 1
  auto qp = RC::create(nic, QPConfig()).value();
  // 1. create the local QP to send
  auto ud = UD::create(nic, QPConfig().set_qkey(73)).value();
  auto recv_ud = UD::create(nic, QPConfig().set_qkey(73)).value();

  auto mem = Arc<RMem>(new RMem(1024 * 1024 * 4));
  auto local_mem = Arc<RMem>(new RMem(1024 * 1024 * 4));

  auto mr = RegHandler::create(mem, nic).value();
#endif
  profile.tick_record(1);
  // ========= Fork ==========
  pid_t pid;
  pid = fork();
  if (pid == 0) { // child
    auto nn = RNic::create(RNicInfo::query_dev_names().at(nic_idx))
                .value(); // open device
    auto qp = RC::create(nn, QPConfig()).value();
    RDMA_LOG(INFO) << "Child process";
  } else {
    profile.tick_record(2);
    profile.increase(1);
    profile.report("end-to-end");
    RDMA_LOG(INFO) << "Parent process";
  }
  LOG(4) << "done";
  return 0;
}
