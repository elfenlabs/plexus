#include "plexus/context.h"
#include "plexus/graph_builder.h"
#include <gtest/gtest.h>

TEST(TopologyTest, ReadAfterWrite) {
    Plexus::Context ctx;
    auto res_id = ctx.register_resource("BufferA");

    Plexus::GraphBuilder builder(ctx);

    bool a_ran = false;
    bool b_ran = false;

    builder.add_node({.debug_name = "WriterA",
                      .work_function = [&]() { a_ran = true; },
                      .dependencies = {{res_id, Plexus::Access::WRITE}}});
    builder.add_node({.debug_name = "ReaderB",
                      .work_function = [&]() { b_ran = true; },
                      .dependencies = {{res_id, Plexus::Access::READ}}});

    auto graph = builder.bake();

    ASSERT_FALSE(graph.nodes.empty());
    bool a_to_b = false;
    for (int dep : graph.nodes[0].dependents)
        if (dep == 1)
            a_to_b = true;
    EXPECT_TRUE(a_to_b);
    EXPECT_GE(graph.nodes[1].initial_dependencies, 1);
}

TEST(TopologyTest, WriteAfterRead) {
    Plexus::Context ctx;
    auto res_id = ctx.register_resource("BufferA");

    Plexus::GraphBuilder builder(ctx);

    builder.add_node({.debug_name = "ReaderA", .dependencies = {{res_id, Plexus::Access::READ}}});
    builder.add_node({.debug_name = "WriterB", .dependencies = {{res_id, Plexus::Access::WRITE}}});

    auto graph = builder.bake();

    bool a_to_b = false;
    for (int dep : graph.nodes[0].dependents)
        if (dep == 1)
            a_to_b = true;
    EXPECT_TRUE(a_to_b);
    EXPECT_GE(graph.nodes[1].initial_dependencies, 1);
}

TEST(PriorityTest, DescendantCount) {
    Plexus::Context ctx;
    Plexus::GraphBuilder builder(ctx);

    // Node A: 2 Children (B, C) -> High Descendant Count
    auto A = builder.add_node({.debug_name = "A"});
    auto B = builder.add_node({.debug_name = "B", .run_after = {A}});
    auto C = builder.add_node({.debug_name = "C", .run_after = {A}});

    // Node D: 0 Children -> Low Descendant Count
    auto D = builder.add_node({.debug_name = "D"});

    auto graph = builder.bake();

    // Expectation: Priority(A) > Priority(D)
    EXPECT_GT(graph.nodes[A].priority, graph.nodes[D].priority);
}

TEST(PriorityTest, CriticalPath) {
    Plexus::Context ctx;
    Plexus::GraphBuilder builder(ctx);

    // Chain 1: A -> B -> C (Length 3)
    auto A = builder.add_node({.debug_name = "A"});
    auto B = builder.add_node({.debug_name = "B", .run_after = {A}});
    auto C = builder.add_node({.debug_name = "C", .run_after = {B}});

    // Chain 2: D -> E (Length 2)
    auto D = builder.add_node({.debug_name = "D"});
    auto E = builder.add_node({.debug_name = "E", .run_after = {D}});

    auto graph = builder.bake();

    // Expectation: Priority(A) > Priority(D)
    EXPECT_GT(graph.nodes[A].priority, graph.nodes[D].priority);
}

TEST(PriorityTest, UserOverride) {
    Plexus::Context ctx;
    Plexus::GraphBuilder builder(ctx);

    // Node A: Massive structural advantage (Chain 10)
    Plexus::NodeID prev = builder.add_node({.debug_name = "A"});
    auto A = prev;
    for (int i = 0; i < 10; ++i) {
        prev = builder.add_node({.debug_name = "Child", .run_after = {prev}});
    }

    // Node B: User override (High Priority) but no structure
    auto B = builder.add_node({.debug_name = "B", .priority = 5});

    auto graph = builder.bake();

    // Expectation: Priority(B) > Priority(A)
    // because User Priority 5 * 1000 = 5000
    // A maxes out around 20-30 structural score.
    EXPECT_GT(graph.nodes[B].priority, graph.nodes[A].priority);
}
