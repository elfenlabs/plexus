#include "plexus/thread_pool.h"
#include <algorithm>
#include <random>

namespace Plexus {

    static thread_local int t_worker_index = -1;

    ThreadPool::ThreadPool() {
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

    ThreadPool::~ThreadPool() {
        m_stop = true;
        m_cv_work.notify_all();
        for (auto &t : m_threads) {
            if (t.joinable()) {
                t.join();
            }
        }
    }

    void ThreadPool::reserve_task_capacity(size_t total_tasks) {
        if (m_workers_data.empty())
            return;

        size_t per_worker = (total_tasks / m_workers_data.size()) * RING_BUFFER_GROWTH_FACTOR;
        if (per_worker < 128)
            per_worker = 128; // Minimum sanity

        for (auto &worker : m_workers_data) {
            std::lock_guard<std::mutex> lock(worker->mutex);
            for (auto &queue : worker->tasks) {
                // We don't know priority distribution, so we reserve uniform large buffers?
                // Or we reserve total_capacity for EACH priority? No that's huge.
                // We assume tasks are distributed somewhat evenly.
                // Let's be safe: each priority queue gets `per_worker` capacity.
                queue.resize(per_worker);
            }
        }
    }

    void ThreadPool::dispatch(const std::vector<Task> &tasks) {
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

    void ThreadPool::enqueue(Task task, int priority) {
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
                pushed = worker.tasks[p].push(std::move(task));
            }
            if (pushed) {
                m_cv_work.notify_one();
                return;
            }
            // If local full, fall through to random/retry logic
        }

        // Push Random / Retry
        // Since we moved 'task' above if pushed, we need to be careful.
        // Actually, if !pushed, 'task' was NOT moved from.
        push_random(std::move(task), p);
    }

    void ThreadPool::push_random(Task task, int priority) {
        static thread_local std::mt19937 generator(std::random_device{}());
        std::uniform_int_distribution<int> distribution(0, static_cast<int>(m_workers_data.size()) -
                                                               1);

        while (true) {
            int idx = distribution(generator);
            bool pushed = false;
            {
                std::lock_guard<std::mutex> lock(m_workers_data[idx]->mutex);
                pushed = m_workers_data[idx]->tasks[priority].push(std::move(task));
            }

            if (pushed) {
                m_cv_work.notify_one();
                return;
            }

            // If full, yield and try again (possibly different worker if we re-roll, but keeping
            // simple) Ideally we re-roll to find an empty queue.
            std::this_thread::yield();
        }
    }

    void ThreadPool::wait() {
        std::unique_lock<std::mutex> lock(m_cv_mutex);
        m_cv_done.wait(lock, [this]() { return m_active_tasks == 0; });
    }

    void ThreadPool::worker_thread(int index) {
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
                        // RingBuffer pop returns bool, and fills task
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
                for (int i = 0; i < num_workers; ++i) {
                    if (i == index)
                        continue;

                    int victim_idx = (index + i + 1) % num_workers;
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
                task();
                int prev = m_active_tasks.fetch_sub(1);
                if (prev == 1) {
                    std::lock_guard<std::mutex> lock(m_cv_mutex);
                    m_cv_done.notify_all();
                }
            }
        }
    }

}
