#pragma once
#include "plexus/execution_graph.h"
#include <atomic>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <vector>

namespace Plexus {

    class ThreadPool;

    /**
     * @brief Execution mode for the Executor.
     */
    enum class ExecutionMode {
        Parallel,  ///< Use thread pool for parallel execution (default)
        Sequential ///< Run all tasks on the calling thread (for debugging)
    };

    /**
     * @brief Handle for asynchronous graph execution.
     *
     * Returned by Executor::run_async(). Use wait() to block until completion,
     * or is_done() to poll the execution status.
     */
    class AsyncHandle {
    public:
        AsyncHandle(AsyncHandle &&) = default;
        AsyncHandle &operator=(AsyncHandle &&) = default;
        AsyncHandle(const AsyncHandle &) = delete;
        AsyncHandle &operator=(const AsyncHandle &) = delete;

        /**
         * @brief Blocks until the graph execution completes.
         * @throws Re-throws any exception that occurred during execution.
         */
        void wait();

        /**
         * @brief Checks if the graph execution has completed.
         * @return true if execution is done, false otherwise.
         */
        bool is_done() const;

        /**
         * @brief Gets any exceptions that occurred during execution.
         * @return Vector of exception pointers. Empty if no exceptions occurred.
         * @note Only valid after wait() returns or is_done() returns true.
         */
        const std::vector<std::exception_ptr> &get_exceptions() const;

    private:
        friend class Executor;
        struct State;
        std::shared_ptr<State> m_state;

        explicit AsyncHandle(std::shared_ptr<State> state);
    };

    /**
     * @brief A generic executor for processing ExecutionGraphs.
     *
     * The Executor takes a static ExecutionGraph and processes it dynamically using
     * the provided ThreadPool. It blocks until the entire graph has finished execution.
     */
    class Executor {
    public:
        /**
         * @brief Constructs an Executor with a default internal ThreadPool.
         * The internal pool uses (hardware_concurrency - 1) threads.
         */
        Executor();

        /**
         * @brief Constructs an Executor using an externally managed ThreadPool.
         * @param pool The thread pool to use for task execution.
         */
        explicit Executor(ThreadPool &pool);

        /**
         * @brief Destructor.
         */
        ~Executor();

        /**
         * @brief Executes the given graph.
         *
         * This function blocks the calling thread until all tasks in the graph have
         * completed.
         *
         * @param graph The dependency graph to execute.
         * @param mode Execution mode. Use Sequential for single-threaded debugging.
         */
        void run(const ExecutionGraph &graph, ExecutionMode mode = ExecutionMode::Parallel);

        /**
         * @brief Executes the given graph asynchronously.
         *
         * Returns immediately with a handle that can be used to wait for completion
         * or poll the execution status.
         *
         * @param graph The dependency graph to execute.
         * @param mode Execution mode. Use Sequential for single-threaded debugging.
         * @return An AsyncHandle that can be used to wait for completion.
         *
         * @note The graph must remain valid until the execution completes.
         * @note If any node has ThreadAffinity::Main, this function will block
         *       until all main-thread tasks complete, as they must run on the
         *       calling thread. For truly async execution, ensure no nodes have
         *       main-thread affinity.
         */
        [[nodiscard]] AsyncHandle run_async(const ExecutionGraph &graph,
                                            ExecutionMode mode = ExecutionMode::Parallel);

        /**
         * @brief Callback for profiling.
         * @param name Optional label.
         * @param duration_ms Duration in milliseconds.
         */
        using ProfilerCallback = std::function<void(const char *name, double duration_ms)>;

        void set_profiler_callback(ProfilerCallback callback) {
            m_profiler_callback = std::move(callback);
        }

    private:
        void run_task(const ExecutionGraph &graph, std::atomic<int> *counters, int node_idx,
                      std::shared_ptr<AsyncHandle::State> async_state);
        void run_sequential(const ExecutionGraph &graph,
                            std::shared_ptr<AsyncHandle::State> async_state);
        void run_parallel(const ExecutionGraph &graph,
                          std::shared_ptr<AsyncHandle::State> async_state);

        std::unique_ptr<ThreadPool> m_owned_pool;
        ThreadPool *m_pool;
        ProfilerCallback m_profiler_callback;
    };
}
