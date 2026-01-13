#pragma once

#include "task_node_pool.h"
#include "work_stealing_queue.h"
#include <algorithm>
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace Plexus {

    /// Shutdown mode for ThreadPool
    enum class ShutdownMode {
        Drain, ///< Finish all queued/running tasks before stopping (default)
        Cancel ///< Stop immediately, dropping queued tasks
    };

    // Thread-local worker context
    inline thread_local size_t t_worker_index = SIZE_MAX;
    inline thread_local size_t t_worker_count = 0;

    /**
     * @brief A high-performance, work-stealing thread pool.
     *
     * Features:
     * - **Lock-Free Per-Worker Queues**: Each worker has a `WorkStealingQueue` (Chase-Lev
     * algorithm) for lock-free local operations.
     * - **Work Stealing**: Idle threads steal work from other threads using the lock-free
     * `steal()` method.
     * - **Per-Worker Inject Queues**: External producers spread tasks across MPSC queues to
     * reduce overflow contention; queues are single-consumer and non-stealable to keep enqueues
     * lock-free.
     * - **Central Overflow Queue**: When worker queues are full, tasks spill to a mutex-protected
     * central queue.
     * - **LIFO Scheduling**: Local workers pop from the back (LIFO) for better cache locality.
     * - **TaskNode Pool**: Uses a pool for task nodes, eliminating per-task allocation.
     */
    class ThreadPool {
    public:
        // Use the FixedFunction from task_node_pool.h
        using Task = FixedFunction<64>;

        ThreadPool(int num_threads = 0) {
            unsigned int count = num_threads ? num_threads : std::thread::hardware_concurrency();
            if (count == 0)
                count = 2;
            if (count > 1)
                count--;

            m_queues.reserve(count);
            for (unsigned int i = 0; i < count; ++i) {
                m_queues.push_back(std::make_unique<WorkQueue>());
            }
            // Threads must be started after queues are initialized
            for (unsigned int i = 0; i < count; ++i) {
                m_threads.emplace_back(&ThreadPool::worker_thread, this, i);
            }
        }

        ~ThreadPool() { shutdown(ShutdownMode::Drain); }

        ThreadPool(const ThreadPool &) = delete;
        ThreadPool &operator=(const ThreadPool &) = delete;

        /**
         * @brief Shutdown the thread pool.
         * @param mode Drain (default) waits for all tasks; Cancel drops queued work.
         */
        void shutdown(ShutdownMode mode = ShutdownMode::Drain) {
            bool expected = false;
            if (!m_shutdown_started.compare_exchange_strong(expected, true,
                                                            std::memory_order_acq_rel))
                return; // Already shutting down

            m_shutdown_mode.store(mode, std::memory_order_relaxed);
            m_accepting.store(false, std::memory_order_release);
            m_stop.store(true, std::memory_order_release);

            // Wake all workers so they re-check conditions
            notify_all_workers();

            // In Cancel mode, drain overflow queue to keep counters consistent
            if (mode == ShutdownMode::Cancel) {
                std::lock_guard<std::mutex> lock(m_overflow_mutex);
                while (!m_overflow_queue.empty()) {
                    TaskNode *node = m_overflow_queue.front();
                    m_overflow_queue.pop_front();
                    node->task.reset();
                    m_pool.free(node);
                    m_active_tasks.fetch_sub(1, std::memory_order_relaxed);
                    m_queued_tasks.fetch_sub(1, std::memory_order_relaxed);
                }
                m_overflow_nonempty.store(false, std::memory_order_relaxed);
            }

            for (auto &t : m_threads) {
                if (t.joinable())
                    t.join();
            }
        }

        void dispatch(std::vector<Task> &&tasks) {
            if (tasks.empty())
                return;
            if (!m_accepting.load(std::memory_order_acquire))
                return; // Reject when shutdown started

            const auto count = static_cast<std::int64_t>(tasks.size());
            m_active_tasks.fetch_add(count, std::memory_order_relaxed);
            m_queued_tasks.fetch_add(count, std::memory_order_relaxed);

            if (t_worker_index < m_queues.size()) {
                // Worker thread: push to OWN queue (lock-free, single-owner)
                size_t worker_idx = t_worker_index;
                for (auto &task : tasks) {
                    TaskNode *node = m_pool.alloc();
                    node->task = std::move(task);
                    if (!m_queues[worker_idx]->queue.push(node)) {
                        // Queue full, spill to central
                        std::lock_guard<std::mutex> lock(m_overflow_mutex);
                        const bool was_empty = m_overflow_queue.empty();
                        m_overflow_queue.push_back(node);
                        if (was_empty) {
                            m_overflow_nonempty.store(true, std::memory_order_relaxed);
                        }
                    }
                }
            } else {
                // External thread: spread across per-worker inject queues (MPSC)
                const size_t worker_count = m_queues.size();
                const size_t task_count = tasks.size();
                const size_t start =
                    m_inject_rr.fetch_add(task_count, std::memory_order_relaxed);

                for (size_t i = 0; i < task_count; ++i) {
                    TaskNode *node = m_pool.alloc();
                    node->task = std::move(tasks[i]);
                    const size_t idx = (start + i) % worker_count;
                    m_queues[idx]->inject.push(node);
                }

                const size_t to_notify = std::min(task_count, worker_count);
                for (size_t i = 0; i < to_notify; ++i) {
                    const size_t idx = (start + i) % worker_count;
                    notify_one_worker(idx);
                }
                return;
            }

            // Always notify at least one worker unconditionally (progress guarantee)
            // The sleeping flag is a hint that can be stale, so unconditional notify is required.
            const auto idx = m_wake_rr.fetch_add(1, std::memory_order_relaxed) % m_queues.size();
            notify_one_worker(idx);

            // Best-effort: wake additional workers for batch dispatch (heuristic)
            if (tasks.size() > 1) {
                size_t tasks_count = tasks.size();
                size_t workers_woken = 1;
                for (size_t i = 0; i < m_queues.size() && workers_woken < tasks_count; ++i) {
                    if (m_queues[i]->sleeping.load(std::memory_order_acquire)) {
                        notify_one_worker(i);
                        ++workers_woken;
                    }
                }
            }
        }

        template <typename F> void enqueue(F &&f) {
            if (!m_accepting.load(std::memory_order_acquire))
                throw std::runtime_error("ThreadPool stopped");

            m_active_tasks.fetch_add(1, std::memory_order_relaxed);
            m_queued_tasks.fetch_add(1, std::memory_order_relaxed);

            TaskNode *node = m_pool.alloc();
            node->task = Task(std::forward<F>(f));

            if (t_worker_index < m_queues.size()) {
                // Worker thread: push to OWN queue (lock-free, single-owner)
                if (!m_queues[t_worker_index]->queue.push(node)) {
                    // Queue full, spill to central
                    std::lock_guard<std::mutex> lock(m_overflow_mutex);
                    const bool was_empty = m_overflow_queue.empty();
                    m_overflow_queue.push_back(node);
                    if (was_empty) {
                        m_overflow_nonempty.store(true, std::memory_order_relaxed);
                    }
                }
            } else {
                // External thread: push to per-worker inject queue (MPSC)
                const size_t idx =
                    m_inject_rr.fetch_add(1, std::memory_order_relaxed) % m_queues.size();
                m_queues[idx]->inject.push(node);
                notify_one_worker(idx);
                return;
            }

            // Always notify at least one worker unconditionally (progress guarantee)
            const auto idx = m_wake_rr.fetch_add(1, std::memory_order_relaxed) % m_queues.size();
            notify_one_worker(idx);
        }

        void wait() {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv_done.wait(
                lock, [this]() { return m_active_tasks.load(std::memory_order_relaxed) == 0; });
        }

        void reserve_task_capacity(size_t capacity) {
            // WorkStealingQueue has fixed capacity, no-op
            (void)capacity;
        }

        /**
         * @brief Returns the number of worker threads.
         */
        size_t worker_count() const { return m_queues.size(); }

    private:
        struct MpscInjectQueue {
            std::atomic<TaskNode *> head{nullptr};
            TaskNode *local{nullptr}; // Consumer-only list; not stealable by other workers

            void push(TaskNode *node) {
                TaskNode *prev = head.load(std::memory_order_relaxed);
                do {
                    node->next = prev;
                } while (!head.compare_exchange_weak(prev, node, std::memory_order_release,
                                                     std::memory_order_relaxed));
            }

            TaskNode *pop() {
                if (!local) {
                    TaskNode *list = head.exchange(nullptr, std::memory_order_acquire);
                    if (!list) {
                        return nullptr;
                    }
                    local = reverse_list(list);
                }
                TaskNode *node = local;
                local = node->next;
                node->next = nullptr;
                return node;
            }

            static TaskNode *reverse_list(TaskNode *node) {
                TaskNode *prev = nullptr;
                while (node) {
                    TaskNode *next = node->next;
                    node->next = prev;
                    prev = node;
                    node = next;
                }
                return prev;
            }
        };

        struct alignas(64) WorkQueue {
            WorkStealingQueue<TaskNode> queue;
            MpscInjectQueue inject;
            std::mutex mutex;                  // Per-worker mutex for CV
            std::condition_variable cv;        // Per-worker condition variable
            std::atomic<bool> sleeping{false}; // Is this worker sleeping?

            explicit WorkQueue(std::size_t capacity = 4096) : queue(capacity) {}
        };

        std::vector<std::unique_ptr<WorkQueue>> m_queues;
        std::vector<std::thread> m_threads;
        TaskNodePool m_pool;

        // Central overflow queue for when worker queues are full
        std::mutex m_overflow_mutex;
        std::deque<TaskNode *> m_overflow_queue;
        std::atomic<bool> m_overflow_nonempty{false}; // Hint to skip overflow try_lock when empty

        // Global synchronization for completion and stopping
        std::mutex m_mutex;
        std::condition_variable m_cv_done;
        std::atomic<bool> m_stop{false};
        std::atomic<bool> m_accepting{true};
        std::atomic<bool> m_shutdown_started{false};
        std::atomic<ShutdownMode> m_shutdown_mode{ShutdownMode::Drain};
        std::atomic<std::int64_t> m_active_tasks{0};
        std::atomic<std::int64_t> m_queued_tasks{0};
        std::atomic<std::uint32_t> m_wake_rr{0}; // Round-robin index for notifications
        std::atomic<std::size_t> m_inject_rr{0};

        void notify_one_worker(std::size_t idx) {
            auto &wq = *m_queues[idx];
            std::lock_guard<std::mutex> lock(wq.mutex);
            wq.cv.notify_one();
        }

        void notify_all_workers() {
            for (auto &q : m_queues) {
                std::lock_guard<std::mutex> lock(q->mutex);
                q->cv.notify_one();
            }
        }

        void worker_thread(int index) {
            t_worker_index = static_cast<size_t>(index);
            t_worker_count = m_queues.size();
            const size_t queue_count = m_queues.size();
            const size_t worker_index = static_cast<size_t>(index);

            while (true) {
                TaskNode *node = nullptr;

                // 1. Try local queue (LIFO for cache locality) - LOCK-FREE
                node = m_queues[index]->queue.pop();

                if (!node) {
                    // 1b. Try per-worker inject queue (MPSC, consumer-local; not stealable)
                    node = m_queues[index]->inject.pop();
                }

                if (!node) {
                    // 2. Steal from other worker queues - LOCK-FREE
                    for (size_t i = 0; !node && i < queue_count - 1; ++i) {
                        size_t steal_idx = (index + i + 1) % queue_count;
                        node = m_queues[steal_idx]->queue.steal();
                    }
                }

                // 3. Try central overflow queue if we still don't have a task
                // Batch-grab: take half of available tasks to reduce contention
                if (!node) {
                    try_pop_overflow(worker_index, node);
                }

                // 4. Exponential backoff spin-wait before blocking
                if (!node) {
                    constexpr int max_spins = 64;
                    for (int spin = 1; spin <= max_spins; spin *= 2) {
                        // Quick check local queue
                        node = m_queues[index]->queue.pop();
                        if (node) {
                            break;
                        }

                        node = m_queues[index]->inject.pop();
                        if (node) {
                            break;
                        }

                        // Quick check overflow queue with batch-grab
                        if (try_pop_overflow_hint(worker_index, node)) {
                            break;
                        }

                        // Backoff: yield multiple times based on spin iteration
                        for (int y = 0; y < spin; ++y) {
                            std::this_thread::yield();
                        }
                    }
                }

                // 5. Wait for work (blocking) - use per-worker CV
                if (!node) {
                    std::unique_lock<std::mutex> lock(m_queues[index]->mutex);
                    m_queues[index]->sleeping.store(true, std::memory_order_relaxed);
                    m_queues[index]->cv.wait(lock, [this]() {
                        return m_stop.load(std::memory_order_relaxed) ||
                               m_queued_tasks.load(std::memory_order_relaxed) > 0;
                    });
                    m_queues[index]->sleeping.store(false, std::memory_order_relaxed);

                    if (m_stop.load(std::memory_order_relaxed)) {
                        auto mode = m_shutdown_mode.load(std::memory_order_relaxed);

                        if (mode == ShutdownMode::Cancel) {
                            // Drain local queue before exiting (keep counters consistent)
                            while (TaskNode *n = m_queues[index]->queue.pop()) {
                                n->task.reset();
                                m_pool.free(n);
                                m_active_tasks.fetch_sub(1, std::memory_order_relaxed);
                                m_queued_tasks.fetch_sub(1, std::memory_order_relaxed);
                            }
                            while (TaskNode *n = m_queues[index]->inject.pop()) {
                                n->task.reset();
                                m_pool.free(n);
                                m_active_tasks.fetch_sub(1, std::memory_order_relaxed);
                                m_queued_tasks.fetch_sub(1, std::memory_order_relaxed);
                            }
                            return;
                        }

                        // Drain mode: check if any work remains to process
                        if (m_queued_tasks.load(std::memory_order_relaxed) == 0 &&
                            m_active_tasks.load(std::memory_order_relaxed) == 0)
                            return;
                        // Otherwise continue to process remaining work
                    }

                    continue;
                }

                // Decrement queued count on task acquisition (not batched)
                m_queued_tasks.fetch_sub(1, std::memory_order_relaxed);

                // Execute task
                assert(node != nullptr && "node must be valid before execution");
                try {
                    node->task();
                } catch (...) {
                    // Task threw
                }

                // Destroy task on worker thread, then return node to pool
                node->task.reset();
                m_pool.free(node);

                std::int64_t prev = m_active_tasks.fetch_sub(1, std::memory_order_release);
                if (prev == 1) {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_cv_done.notify_all();

                    // In Drain shutdown, wake all workers so they can exit
                    if (m_stop.load(std::memory_order_relaxed)) {
                        notify_all_workers();
                    }
                }
            }
        }

        bool try_pop_overflow(size_t index, TaskNode *&node) {
            std::unique_lock<std::mutex> lock(m_overflow_mutex, std::try_to_lock);
            if (!lock) {
                return false;
            }
            if (m_overflow_queue.empty()) {
                m_overflow_nonempty.store(false, std::memory_order_relaxed);
                return false;
            }
            m_overflow_nonempty.store(true, std::memory_order_relaxed);

            node = m_overflow_queue.front();
            m_overflow_queue.pop_front();

            size_t remaining = m_overflow_queue.size();
            size_t to_grab = std::min(remaining / 2, size_t{64});

            for (size_t i = 0; i < to_grab; ++i) {
                TaskNode *extra = m_overflow_queue.front();
                if (!m_queues[index]->queue.push(extra)) {
                    break;
                }
                m_overflow_queue.pop_front();
            }

            if (m_overflow_queue.empty()) {
                m_overflow_nonempty.store(false, std::memory_order_relaxed);
            }
            return true;
        }

        bool try_pop_overflow_hint(size_t index, TaskNode *&node) {
            if (!m_overflow_nonempty.load(std::memory_order_relaxed)) {
                return false;
            }
            return try_pop_overflow(index, node);
        }
    };

} // namespace Plexus
