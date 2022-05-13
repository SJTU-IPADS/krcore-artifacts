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

#include "../huge_region.hh"
#include "../val/cpu.hh"
using namespace nvm;

using namespace xstore::platforms;
#endif

using namespace rdmaio; // warning: should not use it in a global space often
using namespace rdmaio::rmem;
using namespace rdmaio::qp;
using namespace r2;
using namespace r2::rdma;
using Thread_t = bench::Thread<usize>;

DEFINE_int64(threads, 12, "#Threads used.");
DEFINE_int64(payload_sz, 8, "Payload size (bytes)");
DEFINE_int64(write_imm, 0, "write imm");
// For RDMA
DEFINE_string(addr, "localhost", "Server address to connect to.");
DEFINE_int64(port, 8888, "Server address to connect to.");
DEFINE_int64(host, 0, "Server address to connect to.");
DEFINE_int64(use_nic_idx, 0, "Which NIC to create QP");
DEFINE_int64(reg_nic_name, 73, "The name to register an opened NIC at rctrl.");
DEFINE_int64(reg_mem_name, 73, "The name to register an MR at rctrl.");
DEFINE_int64(mem_sz, 1024, "Mr size");
DEFINE_uint32(nic_mod, 2, "NIC use mod");
DEFINE_int64(worker_num, 2, "Worker_num");
DEFINE_int64(
        or_sz,
1,
"Serve for the outstanding request. We use batch size to annote the step");

DEFINE_string(name_recv, "server-qp", "Server recv qp name");
DEFINE_string(name_client, "client-qp", "client qp name");
DEFINE_int64(seq_limit, 4096, "seq size for overflow test");
DEFINE_int64(id, 0, "node id");
DEFINE_int64(run_sec, 10, "running seconds");
DEFINE_string(server_gid,
              "fe80:0000:0000:0000:248a:0703:009c:7c94",
              "server gid to connect and send");
DEFINE_string(worker_addrs, "localhost",
              "addr list"); // split by one blank
#define MR_SIZE 1024 * 1024 * 16

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

usize
worker_fn(const usize& worker_id, Statics* s);

bool volatile running = true;
int
main(int argc, char** argv)
{
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  std::vector<Thread_t*> workers;
  std::vector<Statics> worker_statics(1); // setup workers

  for (uint i = 0; i < 1; ++i) {
    workers.push_back(
      new Thread_t(std::bind(worker_fn, i, &(worker_statics[i]))));
  }

  // start the workers
  for (auto w : workers) {
    w->start();
  }
  sleep(1);
  running = false; // stop workers

  // wait for workers to join
  for (auto w : workers) {
    w->join();
  }
}

// main func for every single worker
usize
worker_fn(const usize& worker_id, Statics* s)
{
  int nic_idx = FLAGS_use_nic_idx;
#ifdef VAL
  nic_idx = (worker_id) % 2;
  VALBinder::bind(nic_idx, worker_id % 24);
#endif
  // 1. create a local QP to use
  r2::util::FastRandom r(0xdeafbeaf + worker_id + 73 * FLAGS_id);

  std::vector<std::string> gid_workers = split(FLAGS_worker_addrs, " ");
  const int worker_num = gid_workers.size();
  std::string name = std::to_string(FLAGS_host);
  auto nic = RNic::create(RNicInfo::query_dev_names().at(nic_idx)).value();

  auto recv_cq_res =
    ::rdmaio::qp::Impl::create_cq(nic, 16); // buffer with [entry_cnt]
  RDMA_ASSERT(recv_cq_res == IOCode::Ok);
  auto recv_cq = std::get<0>(recv_cq_res.desc);

  auto region = HugeRegion::create(MR_SIZE).value();
  auto local_mem = region->convert_to_rmem().value();
  auto local_mr = RegHandler::create(local_mem, nic).value();
  char* buf = (char*)(local_mr->get_reg_attr().value().buf);

  auto recv_mem = Arc<RMem>(new RMem(MR_SIZE));
  auto handler = RegHandler::create(recv_mem, nic).value();

  std::vector<ConnectManager> cm_list;
  std::vector<Arc<RC>> rc_list;
  std::vector<int> keys;
  std::vector<std::string> names;

  // for each worker address
  for (auto addr : gid_workers) {

    auto qp = RC::create(nic, QPConfig(), recv_cq).value();

    std::string addr_port =
      addr + ":" + std::to_string((worker_id + FLAGS_port));
    name = addr_port + "@" + std::to_string(FLAGS_host);
    ;
    names.push_back(name);
    // 2. create the pair QP at server using CM
    ConnectManager cm(addr_port);
    cm_list.push_back(cm);
    rc_list.push_back(qp);
    if (cm.wait_ready(1000000, 50) ==
        IOCode::Timeout) // wait 1 second for server to ready, retry 2 times
      RDMA_ASSERT(false) << "cm connect to server timeout";

    int key = 0;
    while (running) {
      auto qp_res = cm.cc_rc_msg(
        name, FLAGS_name_recv, 1024, qp, FLAGS_reg_nic_name, QPConfig());
      if (qp_res == IOCode::Ok) {
        key = std::get<1>(qp_res.desc);
        break;
      }
    }

    auto fetch_res =
      cm.fetch_remote_mr(FLAGS_reg_mem_name); // rpc call to get remote mr
    RDMA_ASSERT(fetch_res == IOCode::Ok) << std::get<0>(fetch_res.desc);
    rmem::RegAttr remote_attr = std::get<1>(fetch_res.desc);

    qp->bind_remote_mr(remote_attr);
    qp->bind_local_mr(local_mr->get_reg_attr().value());

    keys.push_back(key);
    sleep(0.1);
  }
  SROp op;
  int mod = MR_SIZE / FLAGS_payload_sz;

  // setup operation
  op.set_imm(FLAGS_host)
    .set_payload(buf, FLAGS_payload_sz)
    .set_remote_addr(0)
    .set_op(IBV_WR_SEND_WITH_IMM);

  // r2::util::FastRandom r(0xdeafbeaf + worker_id + FLAGS_id * 73);
  r2::compile_fence();
  sleep(3);
  for (int i = 0; i < worker_num; ++i) {
    RDMA_ASSERT(op.execute_sync(rc_list[i], IBV_SEND_SIGNALED) == IOCode::Ok);
  }

  for (int i = 0; i < worker_num; ++i) {
    auto cm = cm_list[i];
    auto del_res = cm.delete_remote_rc(names[i], keys[i]);
  }

  return 0;
}
