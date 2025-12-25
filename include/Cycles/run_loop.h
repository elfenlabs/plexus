#pragma once
#include "Cycles/execution_graph.h"
#include "Cycles/thread_pool.h"

namespace Cycles {

class RunLoop {
public:
  explicit RunLoop(ThreadPool &pool);

  void set_fixed_graph(ExecutionGraph &&graph);
  void set_render_graph(ExecutionGraph &&graph);

  // Updates the loop. Call this once per frame from the main loop.
  // delta_time: Time elapsed since last frame in seconds.
  void run_one_frame(double delta_time);

  // Configuration
  void set_fixed_step(double step) { m_fixed_step = step; }

  using ProfilerCallback =
      std::function<void(const char *wave_name, double duration_ms)>;
  void set_profiler_callback(ProfilerCallback callback) {
    m_profiler_callback = std::move(callback);
  }

  // Helpers for testing
  double get_accumulator() const { return m_accumulator; }

private:
  void execute_graph(const ExecutionGraph &graph, const char *debug_label);

  ThreadPool &m_pool;
  ExecutionGraph m_fixed_graph;
  ExecutionGraph m_render_graph;

  ProfilerCallback m_profiler_callback;

  double m_accumulator = 0.0;
  double m_fixed_step = 1.0 / 60.0; // Default 60Hz
};

} // namespace Cycles
