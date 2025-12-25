#include <gtest/gtest.h>
#include "Cycles/context.h"
#include "Cycles/graph_builder.h"

TEST(CycleDetectionTest, SimpleCycle) {
    Cycles::Context ctx;
    auto res_a = ctx.register_resource("A");
    auto res_b = ctx.register_resource("B");
    
    Cycles::GraphBuilder builder(ctx);
    
    // Node 1: Reads A, Writes B
    builder.add_node({
        "Node1",
        [](){},
        {{res_a, Cycles::Access::READ}, {res_b, Cycles::Access::WRITE}}
    });
    
    // Node 2: Reads B, Writes A
    builder.add_node({
        "Node2",
        [](){},
        {{res_b, Cycles::Access::READ}, {res_a, Cycles::Access::WRITE}}
    });
    
    // In the current implementation, dependencies are resolved linearly based on registration order.
    // Node 1 (Registered First) Write B -> Node 2 Read B (Implies Node 1 -> Node 2)
    // Node 2 (Registered Second) Write A -> Node 1 Read A? 
    // NO: Node 1 Read A happens *before* Node 2 Writes A. Node 1 reads initial state.
    // Therefore, the graph is: Node 1 -> Node 2.
    // There is NO cycle.
    
    EXPECT_NO_THROW(builder.bake());
}

TEST(CycleDetectionTest, SelfCycle) {
    Cycles::Context ctx;
    auto res_a = ctx.register_resource("A");
    
    Cycles::GraphBuilder builder(ctx);
    
    // Node reads and writes same resource (Modify).
    // Should depend on previous writer, not itself.
    builder.add_node({
        "Selfie",
        [](){},
        {{res_a, Cycles::Access::READ}, {res_a, Cycles::Access::WRITE}}
    });
    
    EXPECT_NO_THROW(builder.bake());
}
