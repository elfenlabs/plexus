#pragma once

#include <atomic>
#include <cstddef>
#include <mutex>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

namespace Plexus {

    /**
     * @brief Fixed-size, type-erased callable with small-buffer optimization.
     *
     * Avoids heap allocation for small lambdas (up to StorageSize bytes).
     */
    template <std::size_t StorageSize = 64> class FixedFunction {
    public:
        FixedFunction() = default;

        template <typename F> FixedFunction(F &&f) {
            static_assert(sizeof(F) <= StorageSize, "Task too large for FixedFunction");
            static_assert(alignof(F) <= alignof(std::max_align_t),
                          "Task alignment too large for FixedFunction");
            static_assert(std::is_trivially_copyable_v<F> || std::is_move_constructible_v<F>,
                          "Task must be movable");

            new (m_storage) F(std::forward<F>(f));
            m_invoke = [](void *storage) { (*reinterpret_cast<F *>(storage))(); };
            m_dtor = [](void *storage) { reinterpret_cast<F *>(storage)->~F(); };
            m_move = [](void *dest, void *src) {
                auto *s = reinterpret_cast<F *>(src);
                new (dest) F(std::move(*s));
                s->~F(); // Destroy source after move
            };
        }

        FixedFunction(FixedFunction &&other) noexcept {
            if (other.m_invoke) {
                other.m_move(m_storage, other.m_storage);
                m_invoke = other.m_invoke;
                m_dtor = other.m_dtor;
                m_move = other.m_move;
                other.m_invoke = nullptr;
                other.m_dtor = nullptr;
                other.m_move = nullptr;
            }
        }

        FixedFunction &operator=(FixedFunction &&other) noexcept {
            if (this != &other) {
                reset();
                if (other.m_invoke) {
                    other.m_move(m_storage, other.m_storage);
                    m_invoke = other.m_invoke;
                    m_dtor = other.m_dtor;
                    m_move = other.m_move;
                    other.m_invoke = nullptr;
                    other.m_dtor = nullptr;
                    other.m_move = nullptr;
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

        void reset() {
            if (m_invoke) {
                m_dtor(m_storage);
                m_invoke = nullptr;
                m_dtor = nullptr;
                m_move = nullptr;
            }
        }

    private:
        alignas(std::max_align_t) std::byte m_storage[StorageSize]{};
        void (*m_invoke)(void *) = nullptr;
        void (*m_dtor)(void *) = nullptr;
        void (*m_move)(void *dest, void *src) = nullptr;
    };

    /**
     * @brief A node for the task pool, containing a task and freelist pointer.
     *
     * TaskNodes have stable addresses and are never freed during execution,
     * only returned to the pool. This eliminates use-after-free in the
     * work-stealing queue.
     */
    struct TaskNode {
        TaskNode *next{nullptr}; // For freelist (protected by mutex)
        FixedFunction<64> task;
    };

    /**
     * @brief Thread-safe pool for TaskNode allocation.
     *
     * Uses mutex + simple freelist to avoid ABA issues inherent in lock-free
     * Treiber stacks. Nodes are never freed until pool destruction.
     */
    class TaskNodePool {
    public:
        TaskNodePool() = default;

        ~TaskNodePool() {
            // Free all nodes in the freelist
            std::lock_guard<std::mutex> lock(m_mutex);
            TaskNode *node = m_head;
            while (node) {
                TaskNode *next = node->next;
                delete node;
                node = next;
            }
            // Note: nodes currently in queues are NOT freed here.
            // The ThreadPool must ensure all workers are stopped before destruction.
        }

        TaskNodePool(const TaskNodePool &) = delete;
        TaskNodePool &operator=(const TaskNodePool &) = delete;

        /**
         * @brief Allocate a TaskNode from the pool.
         */
        TaskNode *alloc() {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_head) {
                TaskNode *node = m_head;
                m_head = node->next;
                node->next = nullptr;
                return node;
            }
            ++m_allocated;
            return new TaskNode();
        }

        /**
         * @brief Return a TaskNode to the pool.
         */
        void free(TaskNode *node) {
            if (!node)
                return;
            std::lock_guard<std::mutex> lock(m_mutex);
            node->next = m_head;
            m_head = node;
        }

        /**
         * @brief Returns approximate number of nodes ever allocated.
         */
        std::size_t allocated_count() const {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_allocated;
        }

    private:
        mutable std::mutex m_mutex;
        TaskNode *m_head{nullptr};
        std::size_t m_allocated{0};
    };

} // namespace Plexus
