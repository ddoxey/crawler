#include <mutex>
#include <condition_variable>

class Gate {
 public:
  explicit Gate(size_t permits) : avail_(permits) {
  }
  void acquire() {
    std::unique_lock<std::mutex> lk(m_);
    cv_.wait(lk, [&] { return avail_ > 0; });
    --avail_;
  }
  void release() {
    std::lock_guard<std::mutex> lk(m_);
    ++avail_;
    cv_.notify_one();
  }

 private:
  std::mutex m_;
  std::condition_variable cv_;
  size_t avail_;
};
