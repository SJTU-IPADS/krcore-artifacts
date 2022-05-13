#include <gflags/gflags.h>

#include "rlib/core/lib.hh"
#include "rlib/core/qps/recv_iter.hh"
#include "ud_msg.hh"
DEFINE_int64(port, 8888, "Server listener (UDP) port.");
DEFINE_int64(use_nic_idx, 0, "Which NIC to create QP");
DEFINE_int64(reg_mem_name, 73, "The name to register an MR at rctrl.");
DEFINE_int64(run_sec, 15, "Running seconds");

using namespace r2;
using namespace rdmaio;
using namespace rdmaio::rmem;
using namespace rdmaio::qp;
using SimpleAllocator = r2::UDAdapter::SimpleAllocator;

int
main(int argc, char** argv)
{

  gflags::ParseCommandLineFlags(&argc, &argv, true);

  RCtrl ctrl(FLAGS_port);
  RDMA_LOG(4) << "(UD) Pingping server listenes at localhost:" << FLAGS_port;

  // first we open the NIC
  auto nic =
    RNic::create(RNicInfo::query_dev_names().at(FLAGS_use_nic_idx)).value();
  // prepare the message buf
  auto mem =
    Arc<RMem>(new RMem(16 * 1024 * 1024)); // allocate a memory with 4M bytes

  auto mr = RegHandler::create(mem, nic).value();
  SimpleAllocator alloc(mem, mr->get_reg_attr().value().key);

  void* mr_buf = (void*)(mr->get_reg_attr().value().buf);

  // prepare buffer, contain 16 recv entries, each has 4096 bytes
  auto recv_rs = RecvEntriesFactory<SimpleAllocator, 2048, 4096>::create(alloc);
  ctrl.registered_mrs.reg(FLAGS_reg_mem_name, mr);

  auto ud = UD::create(nic, QPConfig().set_qkey(73)).value();
  ctrl.registered_qps.reg("server_ud", ud);

  // post these recvs to the UD
  {
    recv_rs->sanity_check();
    auto res = ud->post_recvs(*recv_rs, 128);
    RDMA_ASSERT(res == IOCode::Ok);
  }

  ctrl.start_daemon();
    Timer timer;
  while (1) {
    for (RecvIter<UD, 2048> iter(ud, recv_rs); iter.has_msgs(); iter.next()) {
      auto imm_msg = iter.cur_msg().value();
      UDAdapter::handle_request(
        ctrl, ud, mr, static_cast<char*>(std::get<1>(imm_msg)) + kGRHSz);
    }

    if (timer.passed_sec() >= FLAGS_run_sec) {
        break;
    }
  }
  return 0;
}
