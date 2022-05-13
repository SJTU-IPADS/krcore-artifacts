#pragma once

#include <vector>
#include "rlib/benchs/statics.hh"
#include "rlib/core/utils/timer.hh"
struct Reporter {
    static double report_thpt(std::vector <rdmaio::Statics> &statics, int epoches, int tick_interval_us = 1000000, int batch = 1) {
        const int thread_num = statics.size();
        std::vector <rdmaio::Statics> old_statics(statics.size());

        rdmaio::Timer timer;
        epoches *= 1000000 / tick_interval_us; // fixme: this!
        for (int epoch = 0; epoch < epoches; epoch += 1) {
            usleep(tick_interval_us);

            rdmaio::u64 sum = 0;
            // now report the throughput
            for (uint i = 0; i < statics.size(); ++i) {
                auto temp = statics[i].data.counter;
                sum += (temp - old_statics[i].data.counter);
                old_statics[i].data.counter = temp;
            }

            double passed_msec = timer.passed_msec();
            double thpt = static_cast<double>(sum) / passed_msec * 1000000.0;
            double lat = batch * thread_num * passed_msec / static_cast<double>(sum);
            ::rdmaio::compile_fence();
            timer.reset();

            RDMA_LOG(2) << "epoch @ " << epoch << ": thpt: " << thpt << " reqs/sec."
                        << passed_msec << " msec passed since last epoch. "
                        << lat << " us";
        }
        return 0.0;
    }
};
