#include "Cycles/run_loop.h"
#include <atomic>
#include <gtest/gtest.h>

TEST(RunLoopTest, Accumulator) {
  Cycles::ThreadPool pool;
  Cycles::RunLoop loop(pool);

  // 60Hz = ~0.0166s
  loop.set_fixed_step(0.016);

  std::atomic<int> fixed_count{0};
  std::atomic<int> render_count{0};

  // Setup Fixed Graph (Simple task counting executions)
  Cycles::ExecutionGraph fixed_graph;
  fixed_graph.waves.push_back({{[&]() { fixed_count++; }}});
  loop.set_fixed_graph(std::move(fixed_graph));

  // Setup Render Graph
  Cycles::ExecutionGraph render_graph;
  render_graph.waves.push_back({{[&]() { render_count++; }}});
  loop.set_render_graph(std::move(render_graph));

  // 1. Small delta (less than step) -> 0 Fixed, 1 Render
  loop.run_one_frame(0.010);
  EXPECT_EQ(fixed_count, 0);
  EXPECT_EQ(render_count, 1);

  // 2. Add enough time to trigger 1 fixed update (0.010 + 0.007 = 0.017 >
  // 0.016) Accumulator was 0.010. New total 0.017. Run 1 fixed (remain 0.001).
  // Run 1 render.
  loop.run_one_frame(0.007);
  EXPECT_EQ(fixed_count, 1);
  EXPECT_EQ(render_count, 2);

  // 3. Large delta (e.g. lag spike, 3 steps worth)
  // 3 * 0.016 = 0.048.
  loop.run_one_frame(0.050);
  // Previous remainder 0.001 + 0.050 = 0.051.
  // 0.051 / 0.016 = 3.18 -> 3 steps.
  // Total fixed: 1 + 3 = 4.
  // Total render: 2 + 1 = 3.
  EXPECT_EQ(fixed_count, 4);
  EXPECT_EQ(render_count, 3);
}

TEST(RunLoopTest, Profiling) {
  Cycles::ThreadPool pool;
  Cycles::RunLoop loop(pool);

  bool callback_invoked = false;
  loop.set_profiler_callback([&](const char *name, double duration) {
    callback_invoked = true;
    EXPECT_GE(duration, 0.0);
  });

  Cycles::ExecutionGraph graph;
  graph.waves.push_back({{[]() {}}});
  loop.set_render_graph(std::move(graph));

  loop.run_one_frame(0.016);
  EXPECT_TRUE(callback_invoked);
}
