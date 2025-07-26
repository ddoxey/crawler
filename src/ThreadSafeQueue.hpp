#ifndef THREAD_SAFE_QUEUE_HPP
#define THREAD_SAFE_QUEUE_HPP

#include <queue>
#include <mutex>
#include <condition_variable>

template <typename T>
class ThreadSafeQueue {
 public:
  void push(const T& item) {
    std::unique_lock<std::mutex> lock(mtx);
    q.push(item);
    cv.notify_one();
  }

  T pop() {
    std::unique_lock<std::mutex> lock(mtx);
    cv.wait(lock, [this] { return !q.empty(); });
    T item = q.front();
    q.pop();
    return item;
  }

 private:
  std::queue<T> q;
  std::mutex mtx;
  std::condition_variable cv;
};

#endif  // THREAD_SAFE_QUEUE_HPP
