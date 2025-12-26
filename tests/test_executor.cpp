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

    double high_avg = std::accumulate(high_tickets.begin(), high_tickets.end(), 0.0) / N;
    double low_avg = std::accumulate(low_tickets.begin(), low_tickets.end(), 0.0) / N;

    EXPECT_LT(high_avg, low_avg);
    EXPECT_LT(high_avg, 100.0);
    EXPECT_GT(low_avg, 100.0);
}

TEST(ExecutorTest, DescendantBiased) {
    Plexus::Context ctx;
    Plexus::GraphBuilder builder(ctx);
    Plexus::ThreadPool pool;
    Plexus::Executor executor(pool);

    std::atomic<int> ticket{0};

    // We create many independent trees.
    // Half are "Heavy" (Root + 20 children)
    // Half are "Light" (Root + 0 children)
    // We verify "Heavy Roots" run before "Light Roots" on average.

    const int NUM_TREES = 50;
    std::vector<int> heavy_root_tickets(NUM_TREES, 0);
    std::vector<int> light_root_tickets(NUM_TREES, 0);

    for (int i = 0; i < NUM_TREES; ++i) {
        // Heavy Tree
        auto heavy_root = builder.add_node(
            {"HeavyRoot_" + std::to_string(i), [i, &heavy_root_tickets, &ticket]() {
                 heavy_root_tickets[i] = ticket.fetch_add(1);
                 std::this_thread::yield();
             }});

        // 20 Children for Heavy Root (increases its descendant count)
        for (int c = 0; c < 20; ++c) {
            builder.add_node({"Child", []() {}, {}, {heavy_root}});
        }

        // Light Tree (Just a root)
        builder.add_node({"LightRoot_" + std::to_string(i), [i, &light_root_tickets, &ticket]() {
                              light_root_tickets[i] = ticket.fetch_add(1);
                              std::this_thread::yield();
                          }});
    }

    auto graph = builder.bake();
    executor.run(graph);

    double heavy_avg =
        std::accumulate(heavy_root_tickets.begin(), heavy_root_tickets.end(), 0.0) / NUM_TREES;
    double light_avg =
        std::accumulate(light_root_tickets.begin(), light_root_tickets.end(), 0.0) / NUM_TREES;

    // Heavy roots should have higher calculated priority -> Lower ticket number
    EXPECT_LT(heavy_avg, light_avg);
}
