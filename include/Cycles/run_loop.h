#pragma once
#include "Cycles/execution_graph.h"
#include "Cycles/thread_pool.h"

namespace Cycles {

    /**
     * @brief Manages the application loop, including fixed updates (physics) and
     * rendering.
     *
     * Implements a "Free the Physics" style accumulator loop where fixed updates
     * run at a constant timestep (e.g., 60Hz) regardless of the actual frame rate,
     * ensuring deterministic simulation.
     */
    class RunLoop {
    public:
        explicit RunLoop(ThreadPool &pool);

        /**
         * @brief Sets the graph to be executed during the Fixed Update phase.
         * @param graph The baked execution graph.
         */
        void set_fixed_graph(ExecutionGraph &&graph);

        /**
         * @brief Sets the graph to be executed once per frame (Render).
         * @param graph The baked execution graph.
         */
        void set_render_graph(ExecutionGraph &&graph);

        /**
         * @brief Updates the loop. Call this once per frame from your main
         * application loop.
         *
         * @param delta_time Time elapsed since the last frame in seconds.
         */
        void run_one_frame(double delta_time);

        /**
         * @brief Configures the fixed timestep for the physics loop.
         * @param step Time in seconds (default is 1.0 / 60.0).
         */
        void set_fixed_step(double step) { m_fixed_step = step; }

        /**
         * @brief Callback for profiling wave execution.
         * @param wave_name Name of the phase (e.g. "FixedUpdate").
         * @param duration_ms Duration in milliseconds.
         */
        using ProfilerCallback = std::function<void(const char *wave_name, double duration_ms)>;

        /**
         * @brief Sets a callback to receive timing information for each graph
         * execution.
         */
        void set_profiler_callback(ProfilerCallback callback) {
            m_profiler_callback = std::move(callback);
        }

        // Helpers for testing
        double get_accumulator() const { return m_accumulator; }

    private:
        void execute_graph(const ExecutionGraph &graph, const char *debug_label);
        void run_task(const ExecutionGraph &graph, std::atomic<int> *counters, int node_idx);

        ThreadPool &m_pool;
        ExecutionGraph m_fixed_graph;
        ExecutionGraph m_render_graph;

        ProfilerCallback m_profiler_callback;

        double m_accumulator = 0.0;
        double m_fixed_step = 1.0 / 60.0; // Default 60Hz
    };

}
