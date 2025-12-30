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
            auto heavy_root =
                builder.add_node({.debug_name = "HeavyRoot_" + std::to_string(i),
                                  .work_function = [i, &heavy_root_tickets, &ticket]() {
                                      heavy_root_tickets[i] = ticket.fetch_add(1);
                                  }});

            for (int c = 0; c < 20; ++c) {
                builder.add_node({.debug_name = "Child", .run_after = {heavy_root}});
            }

            builder.add_node({.debug_name = "LightRoot_" + std::to_string(i),
                              .work_function = [i, &light_root_tickets, &ticket]() {
                                  light_root_tickets[i] = ticket.fetch_add(1);
                              }});
        }

        auto graph = builder.bake();

        if (run % 100 == 0) {
            std::cout << "Run " << run << std::endl;
        }

        executor.run(graph);
    }
}
