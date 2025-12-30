#include "plexus/executor.h"
#include "plexus/graph_builder.h"
#include "thread_pool.h"
#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <numeric>
#include <thread>

TEST(ExecutorTest, BasicExecution) {
    Plexus::ThreadPool pool;
    Plexus::Executor executor(pool);

    std::atomic<int> count{0};

    // Setup Graph (Simple task counting executions)
    Plexus::ExecutionGraph graph;
    graph.nodes.push_back({[&]() { count++; }, {}, 0});
    graph.entry_nodes.push_back(0);

    executor.run(graph);
    EXPECT_EQ(count, 1);
}

TEST(ExecutorTest, Profiling) {
    Plexus::ThreadPool pool;
    Plexus::Executor executor(pool);

    bool callback_invoked = false;
    executor.set_profiler_callback([&](const char *name, double duration) {
        callback_invoked = true;
        EXPECT_GE(duration, 0.0);
    });

    Plexus::ExecutionGraph graph;
    graph.nodes.push_back({[]() {}, {}, 0});
    graph.entry_nodes.push_back(0);

    executor.run(graph);
    EXPECT_TRUE(callback_invoked);
}

TEST(ExecutorTest, MultiNodeGraph) {
    Plexus::ThreadPool pool;
    Plexus::Executor executor(pool);

    std::atomic<int> counter{0};

    // Graph: A -> B
    Plexus::ExecutionGraph graph;

    // Node 0 (A)
    graph.nodes.push_back({
        [&]() {
            int expected = 0;
            counter.compare_exchange_strong(expected, 1);
        },
        {1}, // Dependents: Node 1
        0    // Initial deps: 0
    });

    // Node 1 (B)
    graph.nodes.push_back({
        [&]() {
            int expected = 1;
            counter.compare_exchange_strong(expected, 2);
        },
        {}, // Dependents: None
        1   // Initial deps: 1 (Node 0)
    });

    graph.entry_nodes.push_back(0);

    executor.run(graph);

    EXPECT_EQ(counter, 2);
}

TEST(ExecutorTest, RunAsync) {
    Plexus::ThreadPool pool;
    Plexus::Executor executor(pool);

    std::atomic<int> count{0};
    std::atomic<bool> task_started{false};

    Plexus::ExecutionGraph graph;
    graph.nodes.push_back({[&]() {
                               task_started = true;
                               count++;
                           },
                           {},
                           0});
    graph.entry_nodes.push_back(0);

    // Run asynchronously
    auto handle = executor.run_async(graph);

    // Wait for completion
    handle.wait();

    // Now should be done
    EXPECT_TRUE(handle.is_done());
    EXPECT_EQ(count, 1);
    EXPECT_TRUE(task_started);
}

TEST(ExecutorTest, RunAsyncMultipleTasks) {
    Plexus::ThreadPool pool;
    Plexus::Executor executor(pool);

    std::atomic<int> counter{0};
    constexpr int NUM_TASKS = 100;

    Plexus::ExecutionGraph graph;
    for (int i = 0; i < NUM_TASKS; ++i) {
        graph.nodes.push_back({[&]() { counter++; }, {}, 0});
        graph.entry_nodes.push_back(i);
    }

    auto handle = executor.run_async(graph);
    handle.wait();

    EXPECT_TRUE(handle.is_done());
    EXPECT_EQ(counter, NUM_TASKS);
}

TEST(ExecutorTest, RunAsyncDoWorkWhileWaiting) {
    Plexus::ThreadPool pool;
    Plexus::Executor executor(pool);

    std::atomic<int> graph_counter{0};
    std::atomic<int> main_counter{0};

    Plexus::ExecutionGraph graph;
    graph.nodes.push_back({[&]() {
                               std::this_thread::sleep_for(std::chrono::milliseconds(100));
                               graph_counter++;
                           },
                           {},
                           0});
    graph.entry_nodes.push_back(0);

    auto handle = executor.run_async(graph);

    // Do work on main thread while graph executes
    while (!handle.is_done()) {
        main_counter++;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_EQ(graph_counter, 1);
    EXPECT_GT(main_counter, 0); // Should have done some work
}

TEST(ExecutorTest, RunAsyncEmptyGraph) {
    Plexus::Executor executor;

    Plexus::ExecutionGraph graph;

    auto handle = executor.run_async(graph);

    // Empty graph should be immediately done
    EXPECT_TRUE(handle.is_done());
    handle.wait(); // Should not block
}
