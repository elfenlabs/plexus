#include "thread_pool.h"
#include <atomic>
#include <benchmark/benchmark.h>
#include <vector>

static void BM_ThreadPool_Throughput(benchmark::State &state) {
    Plexus::ThreadPool pool;
    // Pre-reserve capacity to avoid measuring allocation time if that's what we want,
    // but default usage might expect allocations.
    // pool.reserve_task_capacity(state.range(0));

    for (auto _ : state) {
        for (int i = 0; i < state.range(0); ++i) {
            pool.enqueue([]() {
                // Minimal work to ensure it's not optimized away entirely
                // but small enough to measure overhead
                benchmark::DoNotOptimize(1);
            });
        }
        pool.wait();
    }
    state.SetItemsProcessed(state.iterations() * state.range(0));
}

BENCHMARK(BM_ThreadPool_Throughput)->Range(100, 10000);

static void BM_ThreadPool_EnqueueOnly(benchmark::State &state) {
    Plexus::ThreadPool pool;

    // Use a blocking flag to prevent tasks from completing during measurement
    std::atomic<bool> block{true};

    for (auto _ : state) {
        // Enqueue tasks that will block until we release them
        for (int i = 0; i < state.range(0); ++i) {
            pool.enqueue([&block]() {
                // Spin until released
                while (block.load(std::memory_order_relaxed)) {
                    std::this_thread::yield();
                }
                benchmark::DoNotOptimize(1);
            });
        }

        // Stop timing before we release the tasks
        state.PauseTiming();
        block.store(false, std::memory_order_relaxed);
        pool.wait();
        block.store(true, std::memory_order_relaxed);
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_ThreadPool_EnqueueOnly)->Range(100, 10000);

static void BM_ThreadPool_ExecutionOverhead(benchmark::State &state) {
    Plexus::ThreadPool pool;

    for (auto _ : state) {
        for (int i = 0; i < state.range(0); ++i) {
            pool.enqueue([]() {
                // Minimal work - just measure the overhead of task execution
                benchmark::DoNotOptimize(1);
            });
        }
        pool.wait();
    }
    state.SetItemsProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_ThreadPool_ExecutionOverhead)->Range(100, 10000);
