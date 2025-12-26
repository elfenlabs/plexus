#pragma once
#include <array>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace Plexus {

    /**
     * @brief A high-performance thread pool using a work-stealing scheduler.
     *
     * Uses a fixed number of worker threads (std::thread::hardware_concurrency - 1).
     * Each thread maintains its own local task queue to minimize contention.
     * When a thread is idle, it attempts to "steal" work from other threads' queues.
     * Supports a bulk dispatch and a barrier wait mechanism.
     */
    class ThreadPool {
    public:
        ThreadPool();
        ~ThreadPool();

        ThreadPool(const ThreadPool &) = delete;
        ThreadPool &operator=(const ThreadPool &) = delete;

        using Task = std::function<void()>;

        /**
         * @brief Dispatches a batch of tasks to the worker queue.
         *
         * This function is thread-safe.
         *
         * @param tasks A vector of void() functions to execute.
         */
        /**
         * @brief Dispatches a batch of tasks to the worker queue.
         * Default priority (4) is used for all tasks.
         */
        void dispatch(const std::vector<Task> &tasks);

        /**
         * @brief Enqueues a single task with an optional priority.
         *
         * @param task The void() function to execute.
         * @param priority Priority level [0-7]. 0 is Lowest, 7 is Highest. Default is 4 (Normal).
         */
        void enqueue(Task task, int priority = 4);

        // ... (wait method unchanged)

        void wait();

    private:
        static constexpr int PRIORITY_LEVELS = 8;

        struct Worker {
            // [0] = Lowest Priority, [7] = Highest Priority
            std::array<std::deque<Task>, PRIORITY_LEVELS> tasks;
            std::mutex mutex;
        };

        void worker_thread(int index);
        void push_random(Task task, int priority);

        // One worker data per thread
        std::vector<std::unique_ptr<Worker>> m_workers_data;
        std::vector<std::thread> m_threads;

        std::mutex m_cv_mutex;             // Protects condition variables and global stop state
        std::condition_variable m_cv_work; // Global signal for new work
        std::condition_variable m_cv_done; // Notify main thread when all tasks completed

        std::atomic<bool> m_stop = false;
        std::atomic<int> m_active_tasks = 0;
        int m_queued_tasks = 0;
    };

}
