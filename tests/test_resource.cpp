#include "plexus/context.h"
#include "plexus/executor.h"
#include "plexus/graph_builder.h"
#include "plexus/resource.h"
#include <gtest/gtest.h>
#include <vector>

using namespace Plexus;

// Test basic Resource wrapper functionality
TEST(ResourceTest, BasicResourceWrapper) {
    Context ctx;
    Resource<int> counter(ctx, "Counter", 42);

    EXPECT_EQ(counter.get(), 42);
    counter.get_mut() = 100;
    EXPECT_EQ(counter.get(), 100);
}

// Test Resource convenience dependency methods
TEST(ResourceTest, DependencyHelpers) {
    Context ctx;
    Resource<int> counter(ctx, "Counter");

    auto read_dep = counter.read();
    EXPECT_EQ(read_dep.access, Access::READ);

    auto write_dep = counter.write();
    EXPECT_EQ(write_dep.access, Access::WRITE);
}

// Test automatic inference with READ access (const&)
TEST(ResourceTest, AutoInferenceRead) {
    Context ctx;
    Resource<std::vector<int>> buffer(ctx, "Buffer");
    buffer.get_mut() = {1, 2, 3};

    GraphBuilder builder(ctx);

    int observed_size = 0;
    builder.add_auto_node(
        "Reader",
        [&observed_size](const std::vector<int>& buf) { observed_size = buf.size(); },
        buffer // Inferred: READ (const&)
    );

    auto graph = builder.bake();
    Executor executor;
    executor.run(graph);

    EXPECT_EQ(observed_size, 3);
}

// Test automatic inference with WRITE access (&)
TEST(ResourceTest, AutoInferenceWrite) {
    Context ctx;
    Resource<int> counter(ctx, "Counter", 0);

    GraphBuilder builder(ctx);

    builder.add_auto_node(
        "Writer", [](int& count) { count = 42; }, counter // Inferred: WRITE (&)
    );

    auto graph = builder.bake();
    Executor executor;
    executor.run(graph);

    EXPECT_EQ(counter.get(), 42);
}

// Test automatic inference with mixed READ/WRITE access
TEST(ResourceTest, AutoInferenceMixedAccess) {
    Context ctx;
    Resource<std::vector<int>> bufA(ctx, "BufferA");
    Resource<std::vector<int>> bufB(ctx, "BufferB");
    Resource<int> counter(ctx, "Counter", 0);

    bufA.get_mut() = {1, 2, 3};
    bufB.get_mut() = {4, 5, 6, 7};

    GraphBuilder builder(ctx);

    builder.add_auto_node(
        "CountElements",
        [](const std::vector<int>& a, // READ
           const std::vector<int>& b, // READ
           int& count) {              // WRITE
            count = a.size() + b.size();
        },
        bufA,   // Inferred: READ
        bufB,   // Inferred: READ
        counter // Inferred: WRITE
    );

    auto graph = builder.bake();
    Executor executor;
    executor.run(graph);

    EXPECT_EQ(counter.get(), 7);
}

// Test explicit Read()/Write() tags
TEST(ResourceTest, ExplicitTags) {
    Context ctx;
    Resource<std::vector<int>> buffer(ctx, "Buffer");
    Resource<int> counter(ctx, "Counter", 0);

    buffer.get_mut() = {1, 2, 3, 4, 5};

    GraphBuilder builder(ctx);

    builder.add_typed_node(
        "Process",
        [](const std::vector<int>& buf, int& cnt) { cnt = buf.size(); },
        Read(buffer),  // Explicit READ
        Write(counter) // Explicit WRITE
    );

    auto graph = builder.bake();
    Executor executor;
    executor.run(graph);

    EXPECT_EQ(counter.get(), 5);
}

// Test proper dependency ordering with automatic inference
TEST(ResourceTest, AutoInferenceDependencyOrdering) {
    Context ctx;
    Resource<int> value(ctx, "Value", 0);

    GraphBuilder builder(ctx);

    // Writer runs first
    builder.add_auto_node(
        "Writer", [](int& val) { val = 100; }, value // WRITE
    );

    // Reader runs after writer
    int observed_value = 0;
    builder.add_auto_node(
        "Reader",
        [&observed_value](const int& val) { observed_value = val; },
        value // READ
    );

    auto graph = builder.bake();
    Executor executor;
    executor.run(graph);

    EXPECT_EQ(observed_value, 100);
}

// Test multiple writers with automatic inference
TEST(ResourceTest, AutoInferenceMultipleWriters) {
    Context ctx;
    Resource<int> counter(ctx, "Counter", 0);

    GraphBuilder builder(ctx);

    builder.add_auto_node(
        "Increment1", [](int& cnt) { cnt += 10; }, counter);
    builder.add_auto_node(
        "Increment2", [](int& cnt) { cnt += 20; }, counter);
    builder.add_auto_node(
        "Increment3", [](int& cnt) { cnt += 30; }, counter);

    auto graph = builder.bake();
    Executor executor;
    executor.run(graph);

    EXPECT_EQ(counter.get(), 60);
}

// Test parallel readers with automatic inference
TEST(ResourceTest, AutoInferenceParallelReaders) {
    Context ctx;
    Resource<int> value(ctx, "Value", 42);

    GraphBuilder builder(ctx);

    // Writer sets the value
    builder.add_auto_node(
        "Writer", [](int& val) { val = 100; }, value);

    // Multiple readers can run in parallel
    std::atomic<int> read_count{0};
    std::atomic<int> correct_reads{0};

    for (int i = 0; i < 10; ++i) {
        builder.add_auto_node(
            "Reader",
            [&read_count, &correct_reads](const int& val) {
                read_count++;
                if (val == 100) {
                    correct_reads++;
                }
            },
            value);
    }

    auto graph = builder.bake();
    Executor executor;
    executor.run(graph);

    EXPECT_EQ(read_count, 10);
    EXPECT_EQ(correct_reads, 10);
}

// Test backward compatibility with manual approach
TEST(ResourceTest, BackwardCompatibility) {
    Context ctx;
    Resource<int> counter(ctx, "Counter", 0);

    GraphBuilder builder(ctx);

    // Old-style manual approach still works
    builder.add_node({.debug_name = "Manual",
                      .work_function = [&counter]() { counter.get_mut() = 42; },
                      .dependencies = {counter.write()}});

    auto graph = builder.bake();
    Executor executor;
    executor.run(graph);

    EXPECT_EQ(counter.get(), 42);
}

// Test complex graph with automatic inference
TEST(ResourceTest, ComplexGraphAutoInference) {
    Context ctx;
    Resource<std::vector<int>> input(ctx, "Input");
    Resource<std::vector<int>> filtered(ctx, "Filtered");
    Resource<int> sum(ctx, "Sum", 0);

    input.get_mut() = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

    GraphBuilder builder(ctx);

    // Node 1: Filter even numbers
    builder.add_auto_node(
        "Filter",
        [](const std::vector<int>& in, std::vector<int>& out) {
            for (int val : in) {
                if (val % 2 == 0) {
                    out.push_back(val);
                }
            }
        },
        input,    // READ
        filtered  // WRITE
    );

    // Node 2: Sum filtered values
    builder.add_auto_node(
        "Sum",
        [](const std::vector<int>& filt, int& total) {
            for (int val : filt) {
                total += val;
            }
        },
        filtered, // READ
        sum       // WRITE
    );

    auto graph = builder.bake();
    Executor executor;
    executor.run(graph);

    EXPECT_EQ(sum.get(), 2 + 4 + 6 + 8 + 10); // 30
}
