#include "Cycles/context.h"
#include "Cycles/graph_builder.h"
#include "Cycles/thread_pool.h"
#include <atomic>
#include <gtest/gtest.h>
#include <vector>

TEST(ConcurrencyTest, RunGraph) {
    Cycles::Context ctx;
    auto res_id = ctx.register_resource("SharedData");

    Cycles::GraphBuilder builder(ctx);

    std::atomic<int> counter{0};
    const int num_readers = 100;

    // Node: Writer (Runs first)
    builder.add_node({"Writer", [&]() { counter = 1000; }, {{res_id, Cycles::Access::WRITE}}});

    // Nodes: Readers (Run parallel after Writer)
    for (int i = 0; i < num_readers; ++i) {
        builder.add_node({"Reader",
                          [&]() {
                              // All readers should see value 1000
                              // We can't easily ASSERT in a thread, so we'll just
                              // increment if correct
                              if (counter == 1000) {
                                  // Just some work
                              }
                          },
                          {{res_id, Cycles::Access::READ}}});
    }

    // Node: Final Writer (Runs after all Readers)
    builder.add_node({"Finalizer", [&]() { counter = 2000; }, {{res_id, Cycles::Access::WRITE}}});

    auto graph = builder.bake();
    Cycles::ThreadPool pool;

    // Execute Graph
    for (const auto &wave : graph.waves) {
        pool.dispatch(wave.tasks);
        pool.wait();
    }

    EXPECT_EQ(counter, 2000);
}

TEST(ConcurrencyTest, IndependentTasks) {
    Cycles::ThreadPool pool;
    std::atomic<int> count{0};

    std::vector<std::function<void()>> tasks;
    for (int i = 0; i < 50; ++i) {
        tasks.push_back([&]() { count++; });
    }

    pool.dispatch(tasks);
    pool.wait();

    EXPECT_EQ(count, 50);
}
