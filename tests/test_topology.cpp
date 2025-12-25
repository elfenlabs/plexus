#include "Cycles/context.h"
#include "Cycles/graph_builder.h"
#include <gtest/gtest.h>

TEST(TopologyTest, ReadAfterWrite) {
    Cycles::Context ctx;
    auto res_id = ctx.register_resource("BufferA");

    Cycles::GraphBuilder builder(ctx);

    // Node A writes to BufferA
    // Node B reads from BufferA
    // Expectation: A runs in Wave 0, B runs in Wave 1

    bool a_ran = false;
    bool b_ran = false;

    builder.add_node({"WriterA", [&]() { a_ran = true; }, {{res_id, Cycles::Access::WRITE}}});

    builder.add_node({"ReaderB", [&]() { b_ran = true; }, {{res_id, Cycles::Access::READ}}});

    auto graph = builder.bake();

    ASSERT_EQ(graph.waves.size(), 2);
    // Execute Wave 0
    ASSERT_EQ(graph.waves[0].tasks.size(), 1);
    graph.waves[0].tasks[0]();
    EXPECT_TRUE(a_ran);
    EXPECT_FALSE(b_ran);

    // Execute Wave 1
    ASSERT_EQ(graph.waves[1].tasks.size(), 1);
    graph.waves[1].tasks[0]();
    EXPECT_TRUE(b_ran);
}

TEST(TopologyTest, WriteAfterRead) {
    Cycles::Context ctx;
    auto res_id = ctx.register_resource("BufferA");

    Cycles::GraphBuilder builder(ctx);

    // Node A reads BufferA
    // Node B writes BufferA
    // Expectation: A -> B

    builder.add_node({"ReaderA", []() {}, {{res_id, Cycles::Access::READ}}});

    builder.add_node({"WriterB", []() {}, {{res_id, Cycles::Access::WRITE}}});

    auto graph = builder.bake();

    ASSERT_EQ(graph.waves.size(), 2);
    // Since names are lost in execution graph, we infer by wave count and logic
}
