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

// Test with varying number of tasks
BENCHMARK(BM_ThreadPool_Throughput)->Range(100, 10000);

static void BM_ThreadPool_EnqueueOnly(benchmark::State &state) {
    Plexus::ThreadPool pool;
    // Determine total tasks to avoid resizing during the loop as much as possible
    // or we accept resizing as part of the benchmark.

    // We want to measure just the enqueue cost.
    // This is tricky because the workers will start draining immediately.

    for (auto _ : state) {
        // We pause timing to clear the pool
        state.PauseTiming();
        pool.wait();
        state.ResumeTiming();

        for (int i = 0; i < state.range(0); ++i) {
            pool.enqueue([]() { benchmark::DoNotOptimize(1); });
        }
    }
    state.SetItemsProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_ThreadPool_EnqueueOnly)->Range(100, 10000);
