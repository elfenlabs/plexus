#pragma once
#include "ring_buffer.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <random>
#include <thread>
#include <vector>

namespace Plexus {

    inline thread_local int t_worker_index = -1;

    /**
     * @brief A high-performance thread pool using a work-stealing scheduler.
     */
    class ThreadPool {
    public:
        using Task = std::function<void()>;

        ThreadPool() {
            unsigned int count = std::thread::hardware_concurrency();
            if (count == 0)
                count = 2;
            if (count > 1)
                count--;

            for (unsigned int i = 0; i < count; ++i) {
                m_workers_data.push_back(std::make_unique<Worker>());
            }

            for (unsigned int i = 0; i < count; ++i) {
                m_threads.emplace_back(&ThreadPool::worker_thread, this, i);
            }

            // Initialize with default capacity to prevent infinite spin on enqueue
            reserve_task_capacity(1024);
        }

        ~ThreadPool() {
            m_stop = true;
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
            int num_workers = static_cast<int>(m_workers_data.size());
            if (num_workers == 0)
                return;

            m_active_tasks += static_cast<int>(tasks.size());
            {
                std::lock_guard<std::mutex> lock(m_cv_mutex);
                m_queued_tasks += static_cast<int>(tasks.size());
            }

            const int priority = 4;

            for (size_t i = 0; i < tasks.size(); ++i) {
                int worker_idx = i % num_workers;
                bool pushed = false;
                while (!pushed) {
                    {
                        std::lock_guard<std::mutex> lock(m_workers_data[worker_idx]->mutex);
                        // tasks is const, so we copy.
                        pushed = m_workers_data[worker_idx]->tasks[priority].push(tasks[i]);
                    }
                    if (!pushed)
                        std::this_thread::yield();
                }
            }
            m_cv_work.notify_all();
        }

        void enqueue(Task task, int priority = 4) {
            m_active_tasks++;
            {
                std::lock_guard<std::mutex> lock(m_cv_mutex);
                m_queued_tasks++;
            }

            int p = std::max(0, std::min(priority, PRIORITY_LEVELS - 1));

            if (t_worker_index >= 0 && t_worker_index < static_cast<int>(m_workers_data.size())) {
                Worker &worker = *m_workers_data[t_worker_index];
                bool pushed = false;
                {
                    std::lock_guard<std::mutex> lock(worker.mutex);
                    if (!worker.tasks[p].full()) {
                        pushed = worker.tasks[p].push(std::move(task));
                    }
                }
                if (pushed) {
                    m_cv_work.notify_one();
                    return;
                }
            }

            push_random(std::move(task), p);
        }

        void wait() {
            std::unique_lock<std::mutex> lock(m_cv_mutex);
            m_cv_done.wait(lock, [this]() { return m_active_tasks == 0; });
        }

        void reserve_task_capacity(size_t total_tasks) {
            if (m_workers_data.empty())
                return;

            size_t per_worker = (total_tasks / m_workers_data.size()) * RING_BUFFER_GROWTH_FACTOR;
            if (per_worker < 128)
                per_worker = 128; // Minimum sanity

            for (auto &worker : m_workers_data) {
                std::lock_guard<std::mutex> lock(worker->mutex);
                for (auto &queue : worker->tasks) {
                    queue.resize(per_worker);
                }
            }
        }

    private:
        static constexpr int PRIORITY_LEVELS = 8;
        static constexpr size_t RING_BUFFER_GROWTH_FACTOR = 2;

        struct Worker {
            std::array<RingBuffer<Task>, PRIORITY_LEVELS> tasks;
            std::mutex mutex;
        };

        std::vector<std::unique_ptr<Worker>> m_workers_data;
        std::vector<std::thread> m_threads;

        std::mutex m_cv_mutex;
        std::condition_variable m_cv_work;
        std::condition_variable m_cv_done;

        std::atomic<bool> m_stop = false;
        std::atomic<int> m_active_tasks = 0;
        int m_queued_tasks = 0;

        void worker_thread(int index) {
            t_worker_index = index;
            Worker &my_worker = *m_workers_data[index];

            while (true) {
                Task task;
                bool found_task = false;

                // 1. Try local queue
                {
                    std::lock_guard<std::mutex> lock(my_worker.mutex);
                    for (int p = PRIORITY_LEVELS - 1; p >= 0; --p) {
                        if (!my_worker.tasks[p].empty()) {
                            my_worker.tasks[p].pop(task);
                            found_task = true;
                            break;
                        }
                    }
                }

                if (found_task) {
                    std::lock_guard<std::mutex> lock(m_cv_mutex);
                    m_queued_tasks--;
                }

                // 2. Try stealing
                if (!found_task) {
                    int num_workers = static_cast<int>(m_workers_data.size());
                    for (int i = 1; i < num_workers; ++i) {
                        int victim_idx = (index + i) % num_workers;
                        Worker &victim = *m_workers_data[victim_idx];

                        if (victim.mutex.try_lock()) {
                            std::lock_guard<std::mutex> lock(victim.mutex, std::adopt_lock);
                            for (int p = PRIORITY_LEVELS - 1; p >= 0; --p) {
                                if (!victim.tasks[p].empty()) {
                                    victim.tasks[p].pop(task);
                                    found_task = true;
                                    break;
                                }
                            }
                        }
                        if (found_task) {
                            std::lock_guard<std::mutex> lock(m_cv_mutex);
                            m_queued_tasks--;
                            break;
                        }
                    }
                }

                if (!found_task) {
                    std::unique_lock<std::mutex> lock(m_cv_mutex);
                    m_cv_work.wait(lock, [this]() { return m_stop || m_queued_tasks > 0; });
                    if (m_stop && m_active_tasks == 0)
                        return;
                } else {
                    try {
                        task();
                    } catch (...) {
                        // Inherit exception handling policy or just log?
                        // For a generic thread pool, we might want to store it,
                        // but here we just ensure we don't kill the thread
                        // and crucially, we MUST decrement the active count.
                    }
                    int prev = m_active_tasks.fetch_sub(1);
                    if (prev == 1) {
                        std::lock_guard<std::mutex> lock(m_cv_mutex);
                        m_cv_done.notify_all();
                    }
                }
            }
        }

        void push_random(Task task, int priority) {
            static thread_local std::mt19937 generator(std::random_device{}());
            std::uniform_int_distribution<int> distribution(
                0, static_cast<int>(m_workers_data.size()) - 1);

            while (true) {
                int idx = distribution(generator);
                bool pushed = false;
                {
                    std::lock_guard<std::mutex> lock(m_workers_data[idx]->mutex);
                    if (!m_workers_data[idx]->tasks[priority].full()) {
                        pushed = m_workers_data[idx]->tasks[priority].push(std::move(task));
                    }
                }

                if (pushed) {
                    m_cv_work.notify_one();
                    return;
                }

                std::this_thread::yield();
            }
        }
    };

}
