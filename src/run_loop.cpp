#include "Cycles/run_loop.h"

namespace Cycles {

RunLoop::RunLoop(ThreadPool &pool) : m_pool(pool) {}

void RunLoop::set_fixed_graph(ExecutionGraph &&graph) {
  m_fixed_graph = std::move(graph);
}

void RunLoop::set_render_graph(ExecutionGraph &&graph) {
  m_render_graph = std::move(graph);
}

void RunLoop::run_one_frame(double delta_time) {
  m_accumulator += delta_time;

  // Fixed Update Loop: Catch up with physics/simulation
  while (m_accumulator >= m_fixed_step) {
    execute_graph(m_fixed_graph, "FixedUpdate");
    m_accumulator -= m_fixed_step;
  }

  // Render Update: Once per frame
  execute_graph(m_render_graph, "RenderUpdate");
}

void RunLoop::execute_graph(const ExecutionGraph &graph,
                            const char *debug_label) {
  if (graph.waves.empty())
    return;

  int wave_idx = 0;
  for (const auto &wave : graph.waves) {
    auto start = std::chrono::high_resolution_clock::now();

    m_pool.dispatch(wave.tasks);
    m_pool.wait();

    if (m_profiler_callback) {
      auto end = std::chrono::high_resolution_clock::now();
      std::chrono::duration<double, std::milli> diff = end - start;
      // Ideally construct a name like "FixedUpdate_Wave_0"
      // For now simple callback
      m_profiler_callback(debug_label, diff.count());
    }
    wave_idx++;
  }
}

} // namespace Cycles
