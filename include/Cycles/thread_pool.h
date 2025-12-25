#pragma once
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace Cycles {

class ThreadPool {
public:
  ThreadPool();
  ~ThreadPool();

  // Prevent copying
  ThreadPool(const ThreadPool &) = delete;
  ThreadPool &operator=(const ThreadPool &) = delete;

  using Task = std::function<void()>;

  // Dispatch a batch of tasks to the workers
  void dispatch(const std::vector<Task> &tasks);

  // Block until all currently dispatched tasks are complete
  void wait();

private:
  void worker_thread();

  std::vector<std::thread> m_workers;
  std::queue<Task> m_queue;

  std::mutex m_mutex;
  std::condition_variable m_cv_work; // Notify workers of new tasks
  std::condition_variable m_cv_done; // Notify main thread when tasks complete

  std::atomic<bool> m_stop = false;
  std::atomic<int> m_active_tasks = 0; // Tasks currently in queue or running
};

} // namespace Cycles
