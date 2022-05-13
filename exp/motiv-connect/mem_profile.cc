#include <gflags/gflags.h>

#include <assert.h>
#include <vector>

#include "rlib/core/lib.hh"
#include "rlib/core/qps/mod.hh"

using namespace rdmaio;
using namespace rdmaio::qp;
using namespace rdmaio::rmem;

// #define WHOLE_PATH 

#ifndef WHOLE_PATH 
#define CREATE_QP 1
#endif
DEFINE_int64(n, 1, "alloc number");
DEFINE_int64(sq_depth, 258, "send sz");
DEFINE_int64(rq_depth, 256, "rq sz");
DEFINE_int64(cq_depth, 257, "cq sz");

int
main(int argc, char **argv) {
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    auto config =
            QPConfig().set_max_send(FLAGS_sq_depth).set_max_recv(FLAGS_rq_depth);
    auto nic = RNic::create(RNicInfo::query_dev_names().at(0)).value();
    ibv_cq *cqs[4096];
    ibv_qp *qps[4096];
#ifdef WHOLE_PATH
    auto ud = UD::create(nic, QPConfig()).value();

    auto ud1 = UD::create(nic, config).value();
    Arc<RC> qps[4096];
    for (int i = 0; i < 4096; ++i) {
        qps[i] = NULL;
    }
    for (int i = 0; i < FLAGS_n; ++i) {
        qps[i] = RC::create(nic, config, nullptr).value();
    }
#else


    for(int i = 0 ;i < FLAGS_n; ++i) {
        cqs[i] = ibv_create_cq(nic->get_ctx(), FLAGS_cq_depth, 
                    nullptr, nullptr, 0);
    }
#if CREATE_QP

    for(int i = 0 ;i < FLAGS_n; ++i) {
        auto res_qp =  Impl::create_qp(nic, IBV_QPT_RC, config, cqs[i]);
        qps[i] = std::get<0>(res_qp.desc);
    }

#endif
    

#endif

    sleep(12);
    for(int i = 0; i < FLAGS_n ; ++i) {
        if (qps[i]) ibv_destroy_qp(qps[i]);
        if (cqs[i]) ibv_destroy_cq(cqs[i]);
    }
}
