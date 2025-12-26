#include "plexus/executor.h"
#include <iostream>
#include <memory>

namespace Plexus {

    Executor::Executor(ThreadPool &pool) : m_pool(pool) {}

    void Executor::run(const ExecutionGraph &graph) {
        if (graph.nodes.empty())
            return;

        auto start = std::chrono::high_resolution_clock::now();

        // Reset State
        m_cancel_graph_execution = false;
        {
            std::lock_guard<std::mutex> lock(m_exception_mutex);
            m_exceptions.clear();
        }

        // 0. Pre-allocate Thread Pool Ring Buffers
        // We ensure enough capacity for all nodes to be enqueued without blocking/allocating.
        m_pool.reserve_task_capacity(graph.nodes.size());

        // 1. Initialize State
        // Zero-Allocation: Reuse atomic counters if capacity allows
        if (m_counter_cache_size < graph.nodes.size()) {
            m_counter_cache.reset(new std::atomic<int>[graph.nodes.size()]);
            m_counter_cache_size = graph.nodes.size();
        }

        std::atomic<int> *counters_ptr = m_counter_cache.get();

        for (size_t i = 0; i < graph.nodes.size(); ++i) {
            // Relaxed store is fine because we haven't published this memory to threads yet
            counters_ptr[i].store(graph.nodes[i].initial_dependencies, std::memory_order_relaxed);
        }

        // 2. Submit Entry Nodes
        for (int node_idx : graph.entry_nodes) {
            m_pool.enqueue([this, &graph, counters_ptr,
                            node_idx]() { run_task(graph, counters_ptr, node_idx); },
                           graph.nodes[node_idx].priority);
        }

        m_pool.wait();

        if (m_profiler_callback) {
            auto end = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double, std::milli> diff = end - start;
            m_profiler_callback("Executor::run", diff.count());
        }

        // Rethrow exceptions if any occurred that warranted stopping
        // Logic: if we have exceptions, throw the first one?
        // Or only throw if policy was NOT Continue?
        // Actually, users might want to know about Continue exceptions too.
        // But throwing blindly might mask partial success?
        // Let's stick to: If we have exceptions, throw the first one.
        std::lock_guard<std::mutex> lock(m_exception_mutex);
        if (!m_exceptions.empty()) {
            std::rethrow_exception(m_exceptions.front());
        }
    }

    void Executor::run_task(const ExecutionGraph &graph, std::atomic<int> *counters, int node_idx) {
        // Run user work with Exception Handling
        try {
            if (graph.nodes[node_idx].work) {
                graph.nodes[node_idx].work();
            }
        } catch (...) {
            std::lock_guard<std::mutex> lock(m_exception_mutex);
            m_exceptions.push_back(std::current_exception());

            auto policy = graph.nodes[node_idx].error_policy;

            if (policy == ErrorPolicy::CancelGraph) {
                m_cancel_graph_execution = true;
                return; // Stop processing dependents
            } else if (policy == ErrorPolicy::CancelDependents) {
                return; // Stop processing dependents
            }
            // If Continue, proceed to trigger dependents as if success
        }

        // Check global cancellation before triggering dependents
        if (m_cancel_graph_execution) {
            return;
        }

        // Decrement dependents
        for (int dep_idx : graph.nodes[node_idx].dependents) {
            // fetch_sub returns PREVIOUS value.
            // If prev was 1, it becomes 0, meaning dependencies are met.
            int prev = counters[dep_idx].fetch_sub(1, std::memory_order_release);
            if (prev == 1) {
                // Double check cancel before enqueueing
                if (m_cancel_graph_execution)
                    return;

                // Ensure visibility
                std::atomic_thread_fence(std::memory_order_acquire);
                m_pool.enqueue(
                    [this, &graph, counters, dep_idx]() { run_task(graph, counters, dep_idx); },
                    graph.nodes[dep_idx].priority);
            }
        }
    }

}
