#include "plexus/graph_builder.h"
#include <gtest/gtest.h>

using namespace Plexus;

TEST(TopologyTest, ExplicitOrdering) {
    Context ctx;
    GraphBuilder builder(ctx);

    // Node A (Execution order should be 0)
    auto idA = builder.add_node({.debug_name = "NodeA"});

    // Node B (Execution order should be 1), depends on A explicitly
    auto idB = builder.add_node({.debug_name = "NodeB", .run_after = {idA}});

    auto graph = builder.bake();

    // Check we have nodes
    ASSERT_FALSE(graph.nodes.empty());

    // Since B depends on A, A must have B in dependents.
    bool found_edge = false;
    for (int dep : graph.nodes[idA].dependents) {
        if (dep == idB)
            found_edge = true;
    }
    EXPECT_TRUE(found_edge) << "Node A should have edge to Node B";
    EXPECT_GT(graph.nodes[idB].initial_dependencies, 0);
}

TEST(TopologyTest, ExplicitOrderingRunBefore) {
    Context ctx;
    GraphBuilder builder(ctx);

    // To test run_before, we create the dependent node first so we have its ID.
    // Then we create the prerequisite node and point it to the dependent.

    // 1. Add "Dependent" (B)
    auto idDependent = builder.add_node({.debug_name = "B"});

    // 2. Add "Prerequisite" (A) that says "I run before B"
    builder.add_node({.debug_name = "A", .run_before = {idDependent}});

    auto graph = builder.bake();

    // With A -> B dependency, A must point to B.
    bool found_edge = false;
    // IDs: Dependent=0, Prereq=1.
    int idPrereq = 1;

    for (int dep : graph.nodes[idPrereq].dependents) {
        if (dep == idDependent)
            found_edge = true;
    }
    EXPECT_TRUE(found_edge);
}

TEST(TopologyTest, ExplicitCycle) {
    Context ctx;
    GraphBuilder builder(ctx);

    // Test a circular dependency: A depends on B, and B depends on A.
    // Note: We rely on deterministic NodeID assignment (0, 1, 2...) here
    // because we need to reference Node B before it is added.

    // 1. Add A (ID 0). Declare it runs after B (ID 1).
    builder.add_node({.debug_name = "NodeA", .run_after = {1}});

    // 2. Add B (ID 1). Declare it runs after A (ID 0).
    builder.add_node({.debug_name = "NodeB", .run_after = {0}});

    EXPECT_THROW({ builder.bake(); }, std::runtime_error);
}
