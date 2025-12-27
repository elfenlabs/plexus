#pragma once
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace Plexus {

    inline thread_local int t_worker_index = -1;

    /**
     * @brief A simplified thread pool with a single global queue.
     * This version removes work-stealing to debug the core synchronization issue.
     */
    class ThreadPool {
    public:
        using Task = std::function<void()>;

        ThreadPool(int num_threads = 0) {
            unsigned int count = num_threads ? num_threads : std::thread::hardware_concurrency();
            if (count == 0)
                count = 2;
            if (count > 1)
                count--;

            for (unsigned int i = 0; i < count; ++i) {
                m_threads.emplace_back(&ThreadPool::worker_thread, this, i);
            }
        }

        ~ThreadPool() {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_stop = true;
            }
            m_cv_work.notify_all();
            for (auto &t : m_threads) {
                if (t.joinable()) {
                    t.join();
                }
            }
        }

        ThreadPool(const ThreadPool &) = delete;
        ThreadPool &operator=(const ThreadPool &) = delete;

        void dispatch(const std::vector<Task> &tasks) {
            if (tasks.empty())
                return;

            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_active_tasks += static_cast<int>(tasks.size());
                for (const auto &task : tasks) {
                    m_queue.push(task);
                }
            }
            m_cv_work.notify_all();
        }

        void enqueue(Task task, int priority = 4) {
            (void)priority; // Ignored in simple version

            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_stop)
                    throw std::runtime_error("ThreadPool stopped");
                m_active_tasks++;
                m_queue.push(std::move(task));
            }
            m_cv_work.notify_all();
        }

        void wait() {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv_done.wait(lock, [this]() { return m_active_tasks == 0; });
        }

        void reserve_task_capacity(size_t) {
            // No-op for std::queue
        }

    private:
        std::vector<std::thread> m_threads;
        std::queue<Task> m_queue;
        std::mutex m_mutex;
        std::condition_variable m_cv_work;
        std::condition_variable m_cv_done;
        bool m_stop = false;
        int m_active_tasks = 0;

        void worker_thread(int index) {
            t_worker_index = index;

            while (true) {
                Task task;

                {
                    std::unique_lock<std::mutex> lock(m_mutex);
                    m_cv_work.wait(lock, [this]() { return m_stop || !m_queue.empty(); });

                    if (m_stop && m_queue.empty())
                        return;

                    if (!m_queue.empty()) {
                        task = std::move(m_queue.front());
                        m_queue.pop();
                    } else {
                        continue; // Spurious wakeup, go back to wait
                    }
                }

                // Execute task outside the lock
                try {
                    task();
                } catch (...) {
                    // Task threw - still decrement active count
                }

                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_active_tasks--;
                    if (m_active_tasks == 0) {
                        m_cv_done.notify_all();
                    }
                }
            }
        }
    };

}
