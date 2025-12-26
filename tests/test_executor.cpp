#include "plexus/executor.h"
#include "plexus/graph_builder.h"
#include <atomic>
#include <gtest/gtest.h>
#include <numeric>

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

TEST(ExecutorTest, Prioritization) {
    Plexus::Context ctx;
    Plexus::GraphBuilder builder(ctx);
    Plexus::ThreadPool pool;
    Plexus::Executor executor(pool);

    std::atomic<int> ticket{0};

    // We will store the execution ticket for each task type
    // Since verify vector push is not thread safe, we use pre-allocated arrays
    const int N = 100;
    std::vector<int> high_tickets(N, 0);
    std::vector<int> low_tickets(N, 0);

    // Create 100 High Priority Tasks
    for (int i = 0; i < N; ++i) {
        Plexus::NodeConfig cfg;
        cfg.debug_name = "High_" + std::to_string(i);
        cfg.priority = 10; // User High
        cfg.work_function = [i, &high_tickets, &ticket]() {
            high_tickets[i] = ticket.fetch_add(1);
            // Simulate tiny work to allow stealing/scheduling drift
            std::this_thread::yield();
        };
        builder.add_node(cfg);
    }

    // Create 100 Low Priority Tasks
    for (int i = 0; i < N; ++i) {
        Plexus::NodeConfig cfg;
        cfg.debug_name = "Low_" + std::to_string(i);
        cfg.priority = -10; // User Low
        cfg.work_function = [i, &low_tickets, &ticket]() {
            low_tickets[i] = ticket.fetch_add(1);
            std::this_thread::yield();
        };
        builder.add_node(cfg);
    }

    auto graph = builder.bake();
    executor.run(graph);

    // Calculate averages
    double high_avg = std::accumulate(high_tickets.begin(), high_tickets.end(), 0.0) / N;
    double low_avg = std::accumulate(low_tickets.begin(), low_tickets.end(), 0.0) / N;

    // High priority tasks should run earlier, so their tickets should be smaller.
    // Given 200 tasks:
    // Ideally High = 0..99 (Avg 49.5)
    // Low = 100..199 (Avg 149.5)
    // We expect High Avg < Low Avg significantly.

    EXPECT_LT(high_avg, low_avg);

    // Check also that at least some High tasks ran before Low tasks (Overlap check)
    // Actually, just ensuring the means are separated by a margin is safer for flakes.
    EXPECT_LT(high_avg, 100.0); // Should be in the first half mostly
    EXPECT_GT(low_avg, 100.0);  // Should be in the second half mostly
}
