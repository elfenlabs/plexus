#pragma once
#include "ring_buffer.h"
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace Plexus {

    inline thread_local int t_worker_index = -1;

    /**
     * @brief A simplified thread pool with a single global queue using a ring buffer.
     * Guaranteed zero-allocation if reserve_task_capacity is used appropriately.
     */
    class ThreadPool {
    public:
        // Fixed storage size to accommodate Executor's capture (approx 28-32 bytes) +
        // vtable/overhead
        static constexpr size_t kTaskStorageSize = 64;

        // Custom Type Erasure for fixed-size storage to avoid heap allocations
        class FixedFunction {
        public:
            FixedFunction() = default;

            template <typename F> FixedFunction(F &&f) {
                static_assert(sizeof(F) <= kTaskStorageSize, "Task too large for FixedFunction");
                static_assert(std::is_trivially_copyable_v<F> || std::is_move_constructible_v<F>,
                              "Task must be movable");

                new (m_storage) F(std::forward<F>(f));
                m_invoke = [](void *storage) { (*reinterpret_cast<F *>(storage))(); };
                m_dtor = [](void *storage) { reinterpret_cast<F *>(storage)->~F(); };
                m_move = [](void *dest, void *src) {
                    new (dest) F(std::move(*reinterpret_cast<F *>(src)));
                };
            }

            FixedFunction(FixedFunction &&other) noexcept {
                if (other.m_invoke) {
                    other.m_move(m_storage, other.m_storage);
                    m_invoke = other.m_invoke;
                    m_dtor = other.m_dtor;
                    m_move = other.m_move;
                    other.m_invoke = nullptr; // Mark source as empty
                }
            }

            FixedFunction &operator=(FixedFunction &&other) noexcept {
                if (this != &other) {
                    if (m_invoke)
                        m_dtor(m_storage);
                    if (other.m_invoke) {
                        other.m_move(m_storage, other.m_storage);
                        m_invoke = other.m_invoke;
                        m_dtor = other.m_dtor;
                        m_move = other.m_move;
                        other.m_invoke = nullptr;
                    } else {
                        m_invoke = nullptr;
                    }
                }
                return *this;
            }

            ~FixedFunction() {
                if (m_invoke)
                    m_dtor(m_storage);
            }

            void operator()() {
                if (m_invoke)
                    m_invoke(m_storage);
            }

            explicit operator bool() const { return m_invoke != nullptr; }

        private:
            alignas(std::max_align_t) std::byte m_storage[kTaskStorageSize];
            void (*m_invoke)(void *) = nullptr;
            void (*m_dtor)(void *) = nullptr;
            void (*m_move)(void *dest, void *src) = nullptr;
        };

        using Task = FixedFunction;

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

        void dispatch(std::vector<Task> &&tasks) {
            if (tasks.empty())
                return;

            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_active_tasks += static_cast<int>(tasks.size());
                for (auto &task : tasks) {
                    m_queue.push(std::move(task));
                }
            }
            m_cv_work.notify_all();
        }

        template <typename F> void enqueue(F &&f, int priority = 4) {
            (void)priority;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_stop)
                    throw std::runtime_error("ThreadPool stopped");
                m_active_tasks++;
                // RingBuffer::push might auto-resize (allocate), but we aim to pre-reserve.
                m_queue.push(Task(std::forward<F>(f)));
            }
            m_cv_work.notify_one();
        }

        void wait() {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv_done.wait(lock, [this]() { return m_active_tasks == 0; });
        }

        void reserve_task_capacity(size_t capacity) {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_queue.resize(capacity);
        }

    private:
        std::vector<std::thread> m_threads;
        RingBuffer<Task> m_queue; // Using RingBuffer locally
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

                    // RingBuffer internal check
                    if (!m_queue.empty()) {
                        m_queue.pop(task);
                    } else {
                        continue;
                    }
                }

                try {
                    task();
                } catch (...) {
                    // Task threw
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
