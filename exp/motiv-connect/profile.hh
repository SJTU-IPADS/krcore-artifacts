#include "rlib/core/lib.hh"
#include <vector>

using namespace rdmaio;

namespace r2 {
class Profile
{
private:
  std::vector<double> passed_us;
  int capacity;
  u64 op_count;
  Timer timer;

public:
  Profile(int capacity = 10)
    : capacity(capacity)
    , op_count(0)
  {
    for (int i = 0; i < capacity; ++i) {
      passed_us.push_back(0.0);
    }
  }

  void start() { this->timer.reset(); }

  void tick_record(usize idx)
  {
    this->passed_us[idx % capacity] += this->timer.passed_msec();
  }

  void increase(u64 op) { this->op_count += op; }

  void report(const std::string title)
  {
    for (int i = 0; i < capacity; ++i) {
      double lat = this->passed_us[i] / this->op_count;
      RDMA_LOG(INFO) << "[" << title << "]"
                     << "[Profile][tick:" << i << "] latency: " << lat
                     << " us/op";
    }
  }

  void append(const Profile& profile)
  {
    for (int i = 0; i < capacity; ++i) {
      this->passed_us[i] += profile.passed_us[i];
    }
    this->op_count += profile.op_count;
  }
};
}