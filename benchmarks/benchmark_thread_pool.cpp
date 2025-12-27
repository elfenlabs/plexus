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

// Multi-Producer Contention: Multiple threads enqueueing simultaneously
static void BM_ThreadPool_MultiProducer(benchmark::State &state) {
    Plexus::ThreadPool pool;
    const int num_producers = state.range(0);
    const int tasks_per_producer = 1000;

    for (auto _ : state) {
        std::vector<std::thread> producers;
        std::atomic<int> ready_count{0};
        std::atomic<bool> start{false};

        // Create producer threads
        for (int p = 0; p < num_producers; ++p) {
            producers.emplace_back([&]() {
                // Wait for all threads to be ready
                ready_count.fetch_add(1);
                while (!start.load(std::memory_order_acquire)) {
                    std::this_thread::yield();
                }

                // All threads enqueue simultaneously
                for (int i = 0; i < tasks_per_producer; ++i) {
                    pool.enqueue([]() { benchmark::DoNotOptimize(1); });
                }
            });
        }

        // Wait for all threads to be ready
        while (ready_count.load() < num_producers) {
            std::this_thread::yield();
        }

        // Start all producers simultaneously
        start.store(true, std::memory_order_release);

        // Wait for producers to finish
        for (auto &t : producers) {
            t.join();
        }

        // Wait for all tasks to complete
        pool.wait();
    }

    state.SetItemsProcessed(state.iterations() * num_producers * tasks_per_producer);
}
BENCHMARK(BM_ThreadPool_MultiProducer)->DenseRange(1, 8, 1)->Unit(benchmark::kMillisecond);

// Work Stealing Effectiveness: Measure load balancing
static void BM_ThreadPool_WorkStealing(benchmark::State &state) {
    Plexus::ThreadPool pool;
    const int num_tasks = state.range(0);

    for (auto _ : state) {
        // Enqueue all tasks rapidly from main thread
        // This should cause them to land in one queue initially,
        // then get stolen by idle workers
        for (int i = 0; i < num_tasks; ++i) {
            pool.enqueue([]() {
                // Use a small amount of work per task to make stealing more likely
                volatile int sum = 0;
                for (int j = 0; j < 100; ++j) {
                    sum += j;
                }
                benchmark::DoNotOptimize(sum);
            });
        }
        pool.wait();
    }

    state.SetItemsProcessed(state.iterations() * num_tasks);
}
BENCHMARK(BM_ThreadPool_WorkStealing)->Range(100, 10000);

// Scalability: Test performance with different thread counts
static void BM_ThreadPool_Scalability(benchmark::State &state) {
    const int num_threads = state.range(0);
    const int num_tasks = 10000;

    Plexus::ThreadPool pool(num_threads);

    for (auto _ : state) {
        for (int i = 0; i < num_tasks; ++i) {
            pool.enqueue([]() {
                // Use realistic work that benefits from parallelism
                volatile int sum = 0;
                for (int j = 0; j < 1000; ++j) {
                    sum += j * j;
                }
                benchmark::DoNotOptimize(sum);
            });
        }
        pool.wait();
    }

    state.SetItemsProcessed(state.iterations() * num_tasks);
}
// Test with 1, 2, 4, 8, 16 threads
BENCHMARK(BM_ThreadPool_Scalability)
    ->Arg(1)
    ->Arg(2)
    ->Arg(4)
    ->Arg(8)
    ->Arg(16)
    ->Unit(benchmark::kMillisecond);
