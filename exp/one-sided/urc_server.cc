#include <gflags/gflags.h>

#include "rlib/core/lib.hh"
#include "../benchs/huge_region.hh"
using namespace r2::bench;

DEFINE_int64(port, 8888, "Server listener (UDP) port.");
DEFINE_int64(use_nic_idx, 1, "Which NIC to create QP");
DEFINE_int64(reg_nic_name, 73, "The name to register an opened NIC at rctrl.");
DEFINE_int64(reg_mem_name, 73, "The name to register an MR at rctrl.");
DEFINE_uint64(magic_num, 0xdeadbeaf, "The magic number read by the client");
DEFINE_int64(rdma_op, 0, "RDMA operation");
DEFINE_int64(run_sec, 10, "Running seconds");

using namespace rdmaio;
using namespace rdmaio::rmem;

int main(int argc, char **argv) {
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    // start a controler, so that others may access it using UDP based channel
    RCtrl ctrl(FLAGS_port);
    RDMA_LOG(4) << "Pingping server listenes at localhost:" << FLAGS_port;

    for(int nic_idx = 0 ; nic_idx < 2; ++nic_idx) {
        // first we open the NIC
        {
            auto nic =
                    RNic::create(RNicInfo::query_dev_names().at(nic_idx)).value();

            // register the nic with name 0 to the ctrl
            RDMA_ASSERT(ctrl.opened_nics.reg(FLAGS_reg_nic_name + nic_idx, nic));
        }

        {
            auto mem = HugeRegion::create(4 * 1024 * 1024).value();
            ctrl.registered_mrs.create_then_reg(
                    FLAGS_reg_mem_name + nic_idx, mem->convert_to_rmem().value(),
                    ctrl.opened_nics.query(FLAGS_reg_nic_name + nic_idx).value());
        }
    }


    // initialzie the value so as client can sanity check its content
    u64 *reg_mem = (u64 *)(ctrl.registered_mrs.query(FLAGS_reg_mem_name)
                                   .value()
                                   ->get_reg_attr()
                                   .value()
                                   .buf);
    // setup the value
    for (uint i = 0; i < 10000; ++i) {
        reg_mem[i] = FLAGS_magic_num + i;
        asm volatile("" ::: "memory");
    }

    // start the listener thread so that client can communicate w it
    ctrl.start_daemon();

    RDMA_LOG(2) << "thpt bench server started!";
    // run for 20 sec
    for (uint i = 0; i < FLAGS_run_sec; ++i) {
        // server does nothing because it is RDMA
        // client will read the reg_mem using RDMA
        sleep(1);
    }
    RDMA_LOG(4) << "server exit!";
}
