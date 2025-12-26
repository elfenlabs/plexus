#include "plexus/thread_pool.h"
#include <random>

namespace Plexus {

    // Thread-local index to identify which pool worker the current thread is.
    // -1 means it's an external thread (like the main thread).
    static thread_local int t_worker_index = -1;

    ThreadPool::ThreadPool() {
        // Leave one core for the main thread/OS
        unsigned int count = std::thread::hardware_concurrency();
        if (count == 0)
            count = 2; // Fallback
        if (count > 1)
            count--;

        for (unsigned int i = 0; i < count; ++i) {
            m_workers_data.push_back(std::make_unique<Worker>());
        }

        for (unsigned int i = 0; i < count; ++i) {
            m_threads.emplace_back(&ThreadPool::worker_thread, this, i);
        }
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

    void ThreadPool::dispatch(const std::vector<Task> &tasks) {
        if (tasks.empty())
            return;

        int num_workers = static_cast<int>(m_workers_data.size());
        if (num_workers == 0)
            return; // Should not happen given constructor

        // Round-robin distribution for initial load balancing
        // We do this under the protection of each worker's lock individually to avoid global
        // contention But for batch efficiency we might lock each only once if we grouped them. For
        // simplicity and correctness with work-stealing, we just push one by one. Increment active
        // tasks all at once to minimize atomic overhead? No, let's keep it simple: atomic add total
        // size, then push.

        m_active_tasks += static_cast<int>(tasks.size());

        for (size_t i = 0; i < tasks.size(); ++i) {
            int worker_idx = i % num_workers;
            {
                std::lock_guard<std::mutex> lock(m_workers_data[worker_idx]->mutex);
                m_workers_data[worker_idx]->tasks.push_back(tasks[i]);
            }
        }
        m_cv_work.notify_all();
    }

    void ThreadPool::enqueue(Task task) {
        m_active_tasks++;

        // If called from a worker, push to its own queue (lock-free optimization possible later,
        // but now just lock local mutex which is effectively contention-free)
        if (t_worker_index >= 0 && t_worker_index < static_cast<int>(m_workers_data.size())) {
            Worker &worker = *m_workers_data[t_worker_index];
            {
                std::lock_guard<std::mutex> lock(worker.mutex);
                worker.tasks.push_back(std::move(task));
            }
            // Signal one other thread to wake up in case it's sleeping and can steal this
            m_cv_work.notify_one();
        } else {
            // External thread: push to a random worker
            push_random(std::move(task));
        }
    }

    void ThreadPool::push_random(Task task) {
        // Simple random choice
        static thread_local std::mt19937 generator(std::random_device{}());
        std::uniform_int_distribution<int> distribution(0, static_cast<int>(m_workers_data.size()) -
                                                               1);
        int idx = distribution(generator);

        {
            std::lock_guard<std::mutex> lock(m_workers_data[idx]->mutex);
            m_workers_data[idx]->tasks.push_back(std::move(task));
        }
        m_cv_work.notify_one();
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
                if (!my_worker.tasks.empty()) {
                    task = std::move(my_worker.tasks.front());
                    my_worker.tasks.pop_front();
                    found_task = true;
                }
            }

            // 2. Try stealing
            if (!found_task) {
                int num_workers = static_cast<int>(m_workers_data.size());
                for (int i = 0; i < num_workers; ++i) {
                    if (i == index)
                        continue; // Don't steal from self (already checked)

                    // Simple logic: steal from whoever we find first.
                    // Could be randomized to avoid stampedes.
                    int victim_idx = (index + i + 1) % num_workers;
                    Worker &victim = *m_workers_data[victim_idx];

                    if (victim.mutex.try_lock()) {
                        std::lock_guard<std::mutex> lock(victim.mutex, std::adopt_lock);
                        if (!victim.tasks.empty()) {
                            // Steal from BACK
                            task = std::move(victim.tasks.back());
                            victim.tasks.pop_back();
                            found_task = true;
                        }
                    }
                    if (found_task)
                        break;
                }
            }

            if (!found_task) {
                // 3. Wait if no work found anywhere
                if (m_stop) {
                    // Check if *really* all empty before quiting?
                    // For now, if we found nothing and stop is true, we exit.
                    // But to be strict about draining:
                    // We might miss tasks if we just exit.
                    // Let's rely on active_tasks? No, active_tasks includes running ones.
                    // If we couldn't steal, it's likely empty.
                    // Let's do a strict check or just wait.
                    // In a stop scenario, we usually want to finish everything.
                    // Let's do standard CV wait.
                    if (m_active_tasks == 0)
                        return;
                }

                std::unique_lock<std::mutex> lock(m_cv_mutex);
                // Wait for signal or stop
                m_cv_work.wait(lock, [this, &my_worker]() {
                    return m_stop || m_active_tasks > 0; // Wake up if there is *any* work in system
                });

                if (m_stop && m_active_tasks == 0) {
                    return;
                }
                // If woken up, loop back to try finding work again.
            } else {
                // Execute task
                task();

                // Decrement active tasks
                int prev = m_active_tasks.fetch_sub(1);
                if (prev == 1) {
                    std::lock_guard<std::mutex> lock(m_cv_mutex);
                    m_cv_done.notify_all();
                }
            }
        }
    }

}
