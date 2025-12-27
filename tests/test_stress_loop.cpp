#include "plexus/executor.h"
#include "plexus/graph_builder.h"
#include "thread_pool.h"
#include <atomic>
#include <gtest/gtest.h>
#include <iostream>
#include <numeric>

TEST(StressLoopTest, DescendantBiasedLoop) {
    for (int run = 0; run < 1000; ++run) {
        Plexus::Context ctx;
        Plexus::GraphBuilder builder(ctx);
        Plexus::ThreadPool pool;
        Plexus::Executor executor(pool);

        std::atomic<int> ticket{0};

        const int NUM_TREES = 50;
        std::vector<int> heavy_root_tickets(NUM_TREES, 0);
        std::vector<int> light_root_tickets(NUM_TREES, 0);

        for (int i = 0; i < NUM_TREES; ++i) {
            auto heavy_root = builder.add_node(
                {"HeavyRoot_" + std::to_string(i), [i, &heavy_root_tickets, &ticket]() {
                     heavy_root_tickets[i] = ticket.fetch_add(1);
                     // std::this_thread::yield();
                     // Removed yield to make it tighter/faster and maybe simpler
                 }});

            for (int c = 0; c < 20; ++c) {
                builder.add_node({"Child", []() {}, {}, {heavy_root}});
            }

            builder.add_node(
                {"LightRoot_" + std::to_string(i), [i, &light_root_tickets, &ticket]() {
                     light_root_tickets[i] = ticket.fetch_add(1);
                 }});
        }

        auto graph = builder.bake();

        // Timeout protection for the test runner?
        // We can't easily timeout inside the test unless we use async + future wait_for
        // But if it hangs, ctest will catch it (eventually) or we see it in output.
        // Let's print progress.
        if (run % 100 == 0) {
            std::cout << "Run " << run << std::endl;
        }

        // if (run == 0) {
        //     graph.dump_debug(std::cout);
        // }

        executor.run(graph);
    }
}
