#include "plexus/executor.h"
#include "plexus/graph_builder.h"
#include "thread_pool.h"
#include <gtest/gtest.h>
#include <thread>
#include <vector>

TEST(AffinityTest, MainThreadPinning) {
    Plexus::Context ctx;
    Plexus::GraphBuilder builder(ctx);
    Plexus::ThreadPool pool;
    Plexus::Executor executor(pool);

    std::thread::id main_thread_id = std::this_thread::get_id();
    std::thread::id executed_thread_id;
    bool ran = false;

    Plexus::NodeConfig cfg;
    cfg.debug_name = "PinnedNode";
    cfg.thread_affinity = Plexus::ThreadAffinity::Main;
    cfg.work_function = [&]() {
        executed_thread_id = std::this_thread::get_id();
        ran = true;
    };
    builder.add_node(cfg);

    auto graph = builder.bake();
    executor.run(graph);

    EXPECT_TRUE(ran);
    EXPECT_EQ(executed_thread_id, main_thread_id);
}

TEST(AffinityTest, MixedAffinity) {
    Plexus::Context ctx;
    Plexus::GraphBuilder builder(ctx);
    Plexus::ThreadPool pool;
    Plexus::Executor executor(pool);

    std::thread::id main_thread_id = std::this_thread::get_id();
    std::vector<std::thread::id> worker_ids;
    std::mutex mutex;

    // Node A: Main Thread
    Plexus::NodeConfig cfgA;
    cfgA.debug_name = "MainNode";
    cfgA.thread_affinity = Plexus::ThreadAffinity::Main;
    cfgA.work_function = [&]() { EXPECT_EQ(std::this_thread::get_id(), main_thread_id); };
    auto nodeA = builder.add_node(cfgA);

    // Node B: Worker Thread (implicitly Any)
    // We force it to depend on A so it runs after
    Plexus::NodeConfig cfgB;
    cfgB.debug_name = "WorkerNode";
    cfgB.run_after = {nodeA};
    cfgB.work_function = [&]() {
        std::lock_guard<std::mutex> lock(mutex);
        worker_ids.push_back(std::this_thread::get_id());
    };
    builder.add_node(cfgB);

    auto graph = builder.bake();
    executor.run(graph);

    // Note: It IS possible for the worker node to run on the main thread if the pool steals or if
    // we implement local execution fallbacks. But currently Executor only runs Main tasks locally.
    // Worker tasks go to pool. So Node B MUST run on a pool thread. Unless pool size is 0?
    // ThreadPool defaults to hardware concurrency.

    ASSERT_FALSE(worker_ids.empty());
    EXPECT_NE(worker_ids[0], main_thread_id);
}
