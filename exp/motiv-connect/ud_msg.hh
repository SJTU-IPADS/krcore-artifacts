#pragma once

#include "rlib/core/qps/ud.hh"
#include <unordered_map>

using namespace rdmaio;

static Arc<RNic> default_nic =
  RNic::create(RNicInfo::query_dev_names().at(0)).value();

namespace r2 {
namespace UDAdapter {

using rpc_id_t = u32;
using namespace qp;

class SimpleAllocator : AbsRecvAllocator
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

  Option<std::pair<rmem::RMem::raw_ptr_t, rmem::mr_key_t>> alloc_one(
    const usize& sz) override
  {
    if (total_mem < sz)
      return {};
    auto ret = buf;
    buf = static_cast<char*>(buf) + sz;
    total_mem -= sz;
    return std::make_pair(ret, key);
  }

  Option<std::pair<rmem::RMem::raw_ptr_t, rmem::RegAttr>> alloc_one_for_remote(
    const usize& sz) override
  {
    return {};
  }
};

enum UDRpcIDType : rpc_id_t
{
  CreateRC = 1, // create RC
  DeleteRC,
  FetchMR,
};

struct CreateRCPayload
{
  rpc_id_t rpc_id;
  QPAttr lattr;
  proto::RCReq rc_req;
};

struct FetchMRReqeust
{
  rpc_id_t rpc_id;
  QPAttr lattr;
  int mr_hint;
};

struct CreateRCRes
{
  u64 key;
};

struct FetchMRReply
{
  ::rdmaio::rmem::RegAttr attr;
};

struct DeleteRCPayload
{
  rpc_id_t rpc_id;
  QPAttr lattr;
  char name[::rdmaio::qp::kMaxQPNameLen + 1];
  u64 key;
};

struct Reply
{
  ByteBuffer byte_buffer;
};

static QPAttr
connect(const Arc<RNic>& nic, const std::string addr, const std::string ud_name)
{
  ConnectManager cm(addr);
  if (cm.wait_ready(500000, 4) ==
      IOCode::Timeout) // wait 1 second for server to ready, retry 2 times
    RDMA_LOG(4) << "connect to the " << addr << " timeout!";

  auto fetch_qp_attr_res = cm.fetch_qp_attr(ud_name);
  RDMA_ASSERT(fetch_qp_attr_res == IOCode::Ok)
    << "fetch qp attr error: " << fetch_qp_attr_res.code.name() << " "
    << std::get<0>(fetch_qp_attr_res.desc);
  return std::get<1>(fetch_qp_attr_res.desc);
}

static int
send(const Arc<qp::UD>& ud,
     const qp::QPAttr& attr,
     void* buf,
     ibv_ah* ah,
     u32 length,
     u32 lkey)
{
  ibv_send_wr wr;
  ibv_sge sge;
  struct ibv_send_wr* bad_sr = nullptr;

  wr.opcode = IBV_WR_SEND_WITH_IMM;
  wr.num_sge = 1;
  wr.imm_data = 73;
  wr.next = nullptr;
  wr.sg_list = &sge;

  wr.wr.ud.ah = ah;
  wr.wr.ud.remote_qpn = attr.qpn;
  wr.wr.ud.remote_qkey = attr.qkey;
  wr.send_flags = IBV_SEND_SIGNALED;

  sge.addr = (uintptr_t)(buf);
  sge.length = length;
  sge.lkey = lkey;

  return ibv_post_send(ud->qp, &wr, &bad_sr);
}

template<usize N = 1>
struct SendParam
{
  QPAttr remote_ud_attr;
  Arc<UD> ud;
  Arc<UD> recv_ud;
  Arc<RecvEntries<N>> recv_rs;
  Arc<RegHandler> mr;

  SendParam(QPAttr& remote_ud_attr,
            Arc<UD>& ud,
            Arc<UD>& recv_ud,
            Arc<RecvEntries<N>>& recv_rs,
            Arc<RegHandler>& mr)
    : remote_ud_attr(remote_ud_attr)
    , ud(ud)
    , recv_ud(recv_ud)
    , recv_rs(recv_rs)
    , mr(mr){};
};

static inline int
ud_send(Arc<UD>& ud, const QPAttr& remote_ud_attr, Arc<RegHandler>& mr, int sz)
{

  auto ah = ud->create_ah(remote_ud_attr);
  void* buf = (void*)(mr->get_reg_attr().value().buf);

  auto res = UDAdapter::send(
    ud, remote_ud_attr, buf, ah, sz, mr->get_reg_attr().value().key);
  RDMA_ASSERT(res == 0);
  // wait one completion
  auto ret_r = ud->wait_one_comp();
  RDMA_ASSERT(ret_r == IOCode::Ok) << UD::wc_status(ret_r.desc);
  return 0;
}

template<usize N = 1>
static UDAdapter::CreateRCRes
call_cc(SendParam<N>& param, const QPAttr& rc_attr, const std::string name)
{
  RDMA_ASSERT(param.recv_ud->post_recvs(*param.recv_rs, 1) == IOCode::Ok);

  void* buf = (void*)(param.mr->get_reg_attr().value().buf);
  int sz = sizeof(UDAdapter::CreateRCPayload);
  proto::RCReq req = {};
  memcpy(req.name, name.data(), name.size());
  req.whether_create = 1;
  req.whether_recv = 0;
  req.attr = rc_attr;
  // req.attr = qp->my_attr();
  UDAdapter::CreateRCPayload data = { .rpc_id = UDAdapter::CreateRC,
                                      .lattr = param.recv_ud->my_attr(),
                                      .rc_req = req };
  memcpy(buf, &data, sz);

  ud_send(param.ud, param.remote_ud_attr, param.mr, sz);

  // receive
  bool recv_msg = false;
  UDAdapter::CreateRCRes* reply;
  while (!recv_msg) {
    for (RecvIter<UD, N> iter(param.recv_ud, param.recv_rs); iter.has_msgs();
         iter.next()) {
      recv_msg = true;
      auto imm_msg = iter.cur_msg().value();
      reply =
        (UDAdapter::CreateRCRes*)(static_cast<char*>(std::get<1>(imm_msg)) +
                                  kGRHSz);
    }
  }
  return *reply;
}

template<usize N = 1>
static int
call_disconnect(SendParam<N>& param, const std::string name, const int key)
{
  void* buf = (void*)(param.mr->get_reg_attr().value().buf);
  int sz = sizeof(UDAdapter::DeleteRCPayload);
  // req.attr = qp->my_attr();
  UDAdapter::DeleteRCPayload data;
  data.key = key;
  data.lattr = param.recv_ud->my_attr();
  data.rpc_id = UDAdapter::DeleteRC;
  memcpy(data.name, name.data(), name.size());
  memcpy(buf, &data, sz);

  // send out reqeuest
  ud_send(param.ud, param.remote_ud_attr, param.mr, sz);
  return 0;
}

template<usize N = 1>
static UDAdapter::FetchMRReply
call_fetch_mr(SendParam<N>& param, const int id)
{
  RDMA_ASSERT(param.recv_ud->post_recvs(*param.recv_rs, 1) == IOCode::Ok);

  void* buf = (void*)(param.mr->get_reg_attr().value().buf);
  int sz = sizeof(UDAdapter::FetchMRReqeust);
  // req.attr = qp->my_attr();
  UDAdapter::FetchMRReqeust data = { .rpc_id = UDAdapter::FetchMR,
                                     .lattr = param.recv_ud->my_attr(),
                                     .mr_hint = id };
  memcpy(buf, &data, sz);

  ud_send(param.ud, param.remote_ud_attr, param.mr, sz);

  // receive
  bool recv_msg = false;
  UDAdapter::FetchMRReply* reply;
  while (!recv_msg) {
    for (RecvIter<UD, N> iter(param.recv_ud, param.recv_rs); iter.has_msgs();
         iter.next()) {
      recv_msg = true;
      auto imm_msg = iter.cur_msg().value();
      reply =
        (UDAdapter::FetchMRReply*)(static_cast<char*>(std::get<1>(imm_msg)) +
                                   kGRHSz);
    }
  }
  return *reply;
}

// ========== reply handler =============
static void
handle_create_rc(RCtrl& ctrl,
                 Arc<UD>& ud,
                 Arc<RegHandler>& mr,
                 const UDAdapter::CreateRCPayload& payload)
{
  proto::RCReq rc_req = payload.rc_req;
  void* mr_buf = (void*)(mr->get_reg_attr().value().buf);

  int sz = sizeof(UDAdapter::CreateRCPayload);
  if (rc_req.whether_create == 1) {
    // 1.0 find the Nic to create this QP
    ibv_cq* recv_cq = nullptr;

    Arc<RC> rc = qp::RC::create(default_nic, rc_req.config, recv_cq).value();

        {
            Impl::bring_qp_to_init(rc->qp, rc_req.config, default_nic);
        }
    // 1.2 finally we connect the QP
    if (rc->connect(rc_req.attr) != IOCode::Ok) {
      RDMA_LOG(ERROR) << "connect error";
    }

    auto rc_status = ctrl.registered_qps.reg(rc_req.name, rc);

    // start send reply
    UDAdapter::CreateRCRes reply = { .key = rc_status.value() };
    memcpy(mr_buf, &reply, sizeof(UDAdapter::CreateRCRes));

    ud_send(ud, payload.lattr, mr, sz);

    ctrl.registered_qps.dereg(rc_req.name, rc_status.value());
  }
}

static void
handle_delete_rc(RCtrl& ctrl,
                 Arc<UD>& ud,
                 Arc<RegHandler>& mr,
                 UDAdapter::DeleteRCPayload& payload)
{
  ctrl.registered_qps.dereg(payload.name, payload.key);
}

static void
handle_fetch_mr(RCtrl& ctrl,
                Arc<UD>& ud,
                Arc<RegHandler>& mr,
                const UDAdapter::FetchMRReqeust& payload)
{
  void* mr_buf = (void*)(mr->get_reg_attr().value().buf);

  auto o_mr = ctrl.registered_mrs.query(payload.mr_hint);
  if (o_mr) {
    auto attr = o_mr.value()->get_reg_attr().value();
    // start send reply
    UDAdapter::FetchMRReply reply = { .attr = attr };
    int sz = sizeof(UDAdapter::FetchMRReply);
    memcpy(mr_buf, &reply, sz);

    ud_send(ud, payload.lattr, mr, sz);
  }
}

static void
handle_request(RCtrl& ctrl, Arc<UD>& ud, Arc<RegHandler>& mr, const char* buf)
{
  UDAdapter::rpc_id_t rpc_id = *(UDAdapter::rpc_id_t*)(buf);

  switch (rpc_id) {
    case CreateRC:
      handle_create_rc(ctrl, ud, mr, *(UDAdapter::CreateRCPayload*)(buf));
      break;
    case DeleteRC:
      handle_delete_rc(ctrl, ud, mr, *(UDAdapter::DeleteRCPayload*)(buf));
      break;
    case FetchMR:
      handle_fetch_mr(ctrl, ud, mr, *(UDAdapter::FetchMRReqeust*)(buf));
      break;
  }

  return;
}

} // namespace UD

} // namespace r2