#include "../../../include/syscall.h"
#include "../../huge_region.hh"
#include "../src/rdma/sop.hh"
#include "../../reporter.hh"
#include "rlib/benchs/thread.hh"
#include "rlib/core/lib.hh"
#include "rlib/core/qps/rc_recv_manager.hh"
#include "rlib/core/qps/recv_iter.hh"
#include <random>

#include "../src/random.hh"
using namespace nvm;

#include <gflags/gflags.h>
#include <vector>

// int64
DEFINE_int64(threads, 1, "#Threads used.");
DEFINE_int64(port, 8888, "Server listener (UDP) port.");
DEFINE_int64(host_len, 1, "Client host len");
DEFINE_int64(use_nic_idx, 0, "Which NIC to create QP");
DEFINE_int64(payload_sz, 64, "Payload size (bytes)");
DEFINE_int64(mem_sz, 4194304, "Mr size");       // 4M
DEFINE_int64(reg_nic_name, 73, "The name to register an opened NIC at rctrl.");
DEFINE_int64(reg_mem_name, 73, "The name to register an MR at rctrl.");
DEFINE_int64(seq_limit, 4096, "seq size for overflow test");
DEFINE_int64(silent_sec, 2, "silent time used to overflow");
DEFINE_int64(write_imm, 0, "write imm");
DEFINE_int64(id, 0, "node id");
DEFINE_int64(run_sec, 10, "running seconds");
DEFINE_int64(worker_num, 2, "Worker_num");

// string
DEFINE_string(name_recv, "server-qp", "Server address to connect to.");
DEFINE_string(name_client, "client-qp", "Server address to connect to.");
DEFINE_uint64(magic_num, 0xdeadbeaf, "The magic number read by the client");
DEFINE_string(addr, "localhost", "Host of the machine");
DEFINE_string(kv_addr, "localhost:8888", "RDMA KV host");
DEFINE_string(server_gid,
              "fe80:0000:0000:0000:248a:0703:009c:7c94",
              "server gid to connect and send");
using namespace rdmaio;
using namespace rdmaio::rmem;
using namespace rdmaio::qp;
using namespace r2;
using namespace r2::rdma;
using Thread_t = bench::Thread<usize>;
bool volatile running = true;

const u64 kMsgArea = 1024 * 8;
static inline unsigned long get_random() {
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
worker_fn(const usize& worker_id, Statics* s);

int
main(int argc, char** argv)
{

  gflags::ParseCommandLineFlags(&argc, &argv, true);

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

  sleep(FLAGS_run_sec);
  //    Reporter::report_thpt(worker_statics, 10); // report for 10 seconds
  running = false; // stop workers

  // wait for workers to join
  for (auto w : workers) {
    w->join();
  }
}

inline void
user_verbs_connect(int worker_id)
{
  int nic_idx = FLAGS_use_nic_idx;
  int remote_nic_idx = nic_idx;
  auto nic =
            RNic::create(RNicInfo::query_dev_names().at(nic_idx)).value();
  // 1. create a local QP to use
  r2::util::FastRandom r(0xdeafbeaf + worker_id);
  std::string name = "client-qp-" + std::to_string(worker_id) + "-" +
                     std::to_string(get_random());

  auto qp = RC::create(nic, QPConfig()).value();

  // 2. create the pair QP at server using CM
  ConnectManager cm(FLAGS_kv_addr);
  if (cm.wait_ready(1000000, 50) ==
      IOCode::Timeout) // wait 1 second for server to ready, retry 2 times
    RDMA_ASSERT(false) << "cm connect to server timeout";

  while(running) {
      auto qp_res =
              cm.cc_rc(name, qp, FLAGS_reg_nic_name + remote_nic_idx, QPConfig());
      if (qp_res == IOCode::Ok) {
          auto key = std::get<1>(qp_res.desc);
          auto del_res = cm.delete_remote_rc(name, key);
          break;
      }
  }

}

usize
worker_fn(const usize& worker_id, Statics* s)
{

  const usize entry_cnt = 2048;
  // start a controller, so that others may access it using UDP based channel
  int port = worker_id + FLAGS_port;
  std::string name = FLAGS_addr + ":" + std::to_string(port);

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

  // 5. start server

  Arc<rdmaio::qp::RC> recv_qp;
  Arc<rdmaio::qp::RecvEntries<entry_cnt>> recv_rs;
  // wait until connection from client

  auto local_region = HugeRegion::create(4 * 1024 * 1024).value();
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
    auto recv_mem = Arc<RMem>(new RMem(1024 * 1024));
    auto handler = RegHandler::create(recv_mem, nic).value();

    SimpleAllocator alloc(recv_mem, handler->get_reg_attr().value().key);

    auto res =
      RecvEntriesFactory<SimpleAllocator, entry_cnt, 64>::create(alloc);

    recv_cache[i] = res;
  }
  auto local_reg_attr = local_mr->get_reg_attr().value();
  ctrl.start_daemon();
  auto total_buf = kMsgArea;
  auto buf_start = total_buf * FLAGS_id + worker_id * kMsgArea;
  auto offset = 0;

  op.set_op(IBV_WR_SEND_WITH_IMM).set_payload(buf, 8);
  int i = 0;
  while (running) {

    for (RecvIter<RC, entry_cnt> iter(recv_cq, wcs); iter.has_msgs();
         iter.next()) {
      // Do connect work
      for(int k = 0 ; k < FLAGS_worker_num - 1; ++k){
        user_verbs_connect(worker_id);
      }

      // Send back to the trigger
      auto imm_msg = iter.cur_msg().value();
      auto host_val = static_cast<uint32_t>(std::get<0>(imm_msg));
      // offset = (offset + 8) % total_buf;
      offset_list[host_val] = (offset_list[host_val] + 64) % kMsgArea;
      recv_qp = rc_cache[host_val];
      if (unlikely(recv_qp == nullptr)) { // first time, cache up the host info
        //          const std::string host = std::to_string(host_val);
        const std::string host = name + "@" + std::to_string(host_val);
        // recv_qp = ctrl.registered_qps.query(host).value();
        rdmaio::qp::RC* qp = dynamic_cast<rdmaio::qp::RC*>(ctrl.registered_qps.query(host).value().get());
        recv_qp = Arc<rdmaio::qp::RC>(qp);
        recv_rs = manager.reg_recv_entries.query(host).value();
        recv_qp->bind_local_mr(local_reg_attr);
        auto msg_buf = static_cast<char*>(std::get<1>(imm_msg));
        auto remote_mr = *((rmem::RegAttr*)(msg_buf));
        // RDMA_LOG(4) << "sanity check remote mr key: " << remote_mr.key << "@"
        // << worker_id;
        recv_qp->bind_remote_mr(remote_mr);

        if (FLAGS_write_imm) {
          op.set_remote_addr(offset);
        }

        RDMA_ASSERT(op.execute_my(recv_qp, IBV_SEND_SIGNALED) == IOCode::Ok);
        rc_cache[host_val] = recv_qp;
      } else {
        if (FLAGS_write_imm) {
          op.set_remote_addr(offset_list[host_val] + worker_id * kMsgArea);
        }

        int flag = post_cnt[host_val] == 0 ? IBV_SEND_SIGNALED : 0;
        flag |= IBV_SEND_INLINE;
        op.set_payload(buf + 64 * (i++ % 64), 8);
        RDMA_ASSERT(op.execute_my(recv_qp, flag) == IOCode::Ok);
      }
      recv_qp->post_recvs(*recv_rs, 1);
      ++post_cnt[host_val];
    }

    for (int i = 0; i < FLAGS_host_len; ++i) {
      if (nullptr != rc_cache[i] && post_cnt[i] > 0) {
        rc_cache[i]->wait_one_comp();
        post_cnt[i] = 0;
      }
    }
  }
}
